#include "live_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef __APPLE__

#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
    AUDIO_BUFFERS = 4,
    AUDIO_FRAMES = 1024,
    AUDIO_INPUT_SAMPLES = 524288,
    MIDI_EVENTS = 256
};

static AudioQueueRef audio_queue;
static AudioQueueBufferRef free_audio[AUDIO_BUFFERS];
static unsigned int free_audio_count;
static AudioQueueBufferRef filling_audio;
static size_t filling_frames;
static unsigned int queued_audio;
static int audio_started;
static int audio_stopping;
static int audio_disabled;
static uint32_t audio_output_rate;
static uint64_t audio_disabled_next_time_ns;
static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t audio_condition = PTHREAD_COND_INITIALIZER;
static int16_t audio_input_samples[AUDIO_INPUT_SAMPLES];
static size_t audio_input_read;
static size_t audio_input_write;
static size_t audio_input_count;
static uint32_t audio_input_rate;
static uint64_t audio_input_phase;
static int16_t audio_input_last_sample;
static uint64_t audio_input_time_ns;
static uint64_t audio_input_packets;
static uint64_t audio_input_samples_received;
static unsigned int audio_input_peak;
static uint64_t audio_input_samples_consumed;
static unsigned int audio_input_consumed_peak;
static char audio_input_session[64];
static uint64_t audio_input_rejected_packets;
static pthread_mutex_t audio_input_mutex = PTHREAD_MUTEX_INITIALIZER;

static MIDIClientRef midi_client;
static MIDIPortRef midi_port;
static LiveMidiEvent midi_events[MIDI_EVENTS];
static unsigned int midi_read;
static unsigned int midi_write;
static unsigned int midi_count;
static pthread_mutex_t midi_mutex = PTHREAD_MUTEX_INITIALIZER;

static char input_buffer[512];
static size_t input_length;
static char command_lines[32][512];
static unsigned int command_read;
static unsigned int command_write;
static unsigned int command_count;
static pthread_mutex_t command_mutex = PTHREAD_MUTEX_INITIALIZER;
static char last_display[23];
static uint32_t last_decimal_mask;
static int last_cursor_start = -1;
static int last_cursor_end = -1;
static char last_panel_tx_hex[129];
static uint16_t last_adc_values[8];
static unsigned int last_adc_reads[8];
static unsigned int last_duart_opr;
static unsigned int last_es5505_page;
static pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;
static int http_socket = -1;
static pthread_t http_thread;

static uint64_t monotonic_time_ns(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000000000ULL + (uint64_t)time.tv_nsec;
}

static void audio_input_submit(const uint8_t *bytes, size_t length) {
    if (length < 4) return;
    uint32_t sample_rate = (uint32_t)bytes[0] |
                           ((uint32_t)bytes[1] << 8) |
                           ((uint32_t)bytes[2] << 16) |
                           ((uint32_t)bytes[3] << 24);
    if (sample_rate < 8000 || sample_rate > 192000) return;
    pthread_mutex_lock(&audio_input_mutex);
    uint64_t now_ns = monotonic_time_ns();
    if (audio_input_rate != sample_rate) {
        audio_input_rate = sample_rate;
        audio_input_phase = 0;
    }
    unsigned int packet_peak = 0;
    size_t packet_samples = 0;
    for (size_t offset = 4; offset + 1 < length; offset += 2) {
        int16_t sample = (int16_t)((uint16_t)bytes[offset] |
                                   ((uint16_t)bytes[offset + 1] << 8));
        unsigned int magnitude = sample < 0 ? (unsigned int)(-(int)sample)
                                            : (unsigned int)sample;
        if (magnitude > packet_peak) packet_peak = magnitude;
        ++packet_samples;
        /* Keep latency bounded if the browser briefly outruns the emulated
           converter: discard the oldest sample, never the newest audio. */
        if (audio_input_count == AUDIO_INPUT_SAMPLES) {
            audio_input_read = (audio_input_read + 1) % AUDIO_INPUT_SAMPLES;
            --audio_input_count;
        }
        audio_input_samples[audio_input_write] = sample;
        audio_input_write = (audio_input_write + 1) % AUDIO_INPUT_SAMPLES;
        ++audio_input_count;
    }
    ++audio_input_packets;
    audio_input_samples_received += packet_samples;
    audio_input_peak = packet_peak;
    audio_input_time_ns = now_ns;
    pthread_mutex_unlock(&audio_input_mutex);
}

static int audio_input_session_valid(const char *session) {
    if (!session || !*session || strlen(session) >= sizeof(audio_input_session)) return 0;
    for (const char *character = session; *character; ++character)
        if (!((*character >= 'a' && *character <= 'z') ||
              (*character >= 'A' && *character <= 'Z') ||
              (*character >= '0' && *character <= '9') || *character == '-')) return 0;
    return 1;
}

static void audio_input_activate(const char *session) {
    pthread_mutex_lock(&audio_input_mutex);
    snprintf(audio_input_session, sizeof(audio_input_session), "%s", session);
    audio_input_read = audio_input_write;
    audio_input_count = 0;
    audio_input_phase = 0;
    audio_input_last_sample = 0;
    audio_input_consumed_peak = 0;
    pthread_mutex_unlock(&audio_input_mutex);
}

static int audio_input_session_matches(const char *session) {
    int matches;
    pthread_mutex_lock(&audio_input_mutex);
    matches = audio_input_session[0] && !strcmp(audio_input_session, session);
    if (!matches) ++audio_input_rejected_packets;
    pthread_mutex_unlock(&audio_input_mutex);
    return matches;
}

static void audio_input_deactivate(const char *session) {
    pthread_mutex_lock(&audio_input_mutex);
    if (audio_input_session[0] && !strcmp(audio_input_session, session)) {
        audio_input_session[0] = '\0';
        audio_input_read = audio_input_write;
        audio_input_count = 0;
        audio_input_phase = 0;
        audio_input_last_sample = 0;
    }
    pthread_mutex_unlock(&audio_input_mutex);
}

static void command_enqueue(const char *line) {
    pthread_mutex_lock(&command_mutex);
    if (command_count < 32) {
        snprintf(command_lines[command_write], sizeof(command_lines[command_write]),
                 "%s", line);
        command_write = (command_write + 1) % 32;
        ++command_count;
    }
    pthread_mutex_unlock(&command_mutex);
}

static const char *content_type(const char *path) {
    const char *extension = strrchr(path, '.');
    if (extension && !strcmp(extension, ".css")) return "text/css";
    if (extension && !strcmp(extension, ".js")) return "text/javascript";
    return "text/html; charset=utf-8";
}

static void http_send(int client, const char *status, const char *type,
                      const void *body, size_t length) {
    char header[512];
    int count = snprintf(header, sizeof(header),
                         "HTTP/1.1 %s\r\nContent-Type: %s\r\n"
                         "Content-Length: %zu\r\nCache-Control: no-store\r\n"
                         "Connection: close\r\n\r\n",
                         status, type, length);
    send(client, header, (size_t)count, 0);
    if (length) send(client, body, length, 0);
}

static void http_serve_file(int client, const char *request_path) {
    const char *name = !strcmp(request_path, "/") ? "index.html" : request_path + 1;
    if (strcmp(name, "index.html") && strcmp(name, "style.css") &&
        strcmp(name, "menu-catalog.js") && strcmp(name, "panel.js") &&
        strcmp(name, "live.js")) {
        http_send(client, "404 Not Found", "text/plain", "Not found", 9);
        return;
    }
    const char *directory = getenv("EPS16_PANEL_DIR");
    if (!directory || !*directory) directory = "panel";
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        http_send(client, "404 Not Found", "text/plain", "Not found", 9);
        return;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *data = length > 0 ? malloc((size_t)length) : NULL;
    if (length < 0 || (length && (!data || fread(data, 1, (size_t)length, file) != (size_t)length))) {
        fclose(file);
        free(data);
        http_send(client, "500 Internal Server Error", "text/plain", "Read error", 10);
        return;
    }
    fclose(file);
    http_send(client, "200 OK", content_type(name), data, (size_t)length);
    free(data);
}

static void *http_server(void *unused) {
    (void)unused;
    while (!audio_stopping) {
        int client = accept(http_socket, NULL, NULL);
        if (client < 0) continue;
        /* Normal browser blocks are small, but a larger bounded body also
           permits deterministic full-path PCM diagnostics without hundreds
           of HTTP connection gaps.  Static storage keeps it off the thread
           stack. */
        static char request[1048576];
        size_t length = 0;
        size_t needed = 0;
        while (length + 1 < sizeof(request)) {
            ssize_t received = recv(client, request + length,
                                    sizeof(request) - length - 1, 0);
            if (received <= 0) break;
            length += (size_t)received;
            request[length] = '\0';
            char *header_end = strstr(request, "\r\n\r\n");
            if (!header_end) continue;
            size_t header_length = (size_t)(header_end + 4 - request);
            char *content_length = strcasestr(request, "Content-Length:");
            needed = header_length + (content_length
                         ? (size_t)strtoul(content_length + 15, NULL, 10) : 0);
            if (length >= needed) break;
        }
        if (!length || (needed && length < needed)) { close(client); continue; }
        char method[8], path[256];
        if (sscanf(request, "%7s %255s", method, path) != 2) {
            close(client);
            continue;
        }
        if (!strcmp(method, "GET") && !strcmp(path, "/api/state")) {
            char display[23];
            uint32_t decimal_mask;
            int cursor_start;
            int cursor_end;
            char panel_tx_hex[129];
            pthread_mutex_lock(&display_mutex);
            memcpy(display, last_display, sizeof(display));
            decimal_mask = last_decimal_mask;
            cursor_start = last_cursor_start;
            cursor_end = last_cursor_end;
            memcpy(panel_tx_hex, last_panel_tx_hex, sizeof(panel_tx_hex));
            pthread_mutex_unlock(&display_mutex);
            uint16_t adc_values[8];
            unsigned int adc_reads[8];
            unsigned int duart_opr;
            unsigned int es5505_page;
            pthread_mutex_lock(&display_mutex);
            memcpy(adc_values, last_adc_values, sizeof(adc_values));
            memcpy(adc_reads, last_adc_reads, sizeof(adc_reads));
            duart_opr = last_duart_opr;
            es5505_page = last_es5505_page;
            pthread_mutex_unlock(&display_mutex);
            uint32_t input_rate;
            size_t input_buffered;
            uint64_t input_packets;
            uint64_t input_samples;
            unsigned int input_peak;
            uint64_t input_consumed;
            unsigned int input_consumed_peak;
            uint64_t input_age_ms;
            int input_active;
            uint64_t input_rejected;
            pthread_mutex_lock(&audio_input_mutex);
            input_rate = audio_input_rate;
            input_buffered = audio_input_count;
            input_packets = audio_input_packets;
            input_samples = audio_input_samples_received;
            input_peak = audio_input_peak;
            input_consumed = audio_input_samples_consumed;
            input_consumed_peak = audio_input_consumed_peak;
            input_active = audio_input_session[0] != '\0';
            input_rejected = audio_input_rejected_packets;
            uint64_t now_ns = monotonic_time_ns();
            input_age_ms = audio_input_time_ns && now_ns >= audio_input_time_ns
                               ? (now_ns - audio_input_time_ns) / 1000000ULL
                               : UINT64_MAX;
            pthread_mutex_unlock(&audio_input_mutex);
            char json[2048];
            unsigned int adc_mux;
            switch (duart_opr & 0xf0) {
                case 0xf0: adc_mux = 0; break;
                case 0xb0: adc_mux = 1; break;
                case 0xe0: adc_mux = 2; break;
                case 0x90: adc_mux = 3; break;
                case 0xd0: adc_mux = 4; break;
                case 0xa0: adc_mux = 5; break;
                default: adc_mux = 7; break;
            }
            int json_length = snprintf(json, sizeof(json),
                                       "{\"display\":\"%-22.22s\",\"decimalMask\":%u,"
                                       "\"cursorStart\":%d,"
                                       "\"cursorEnd\":%d,\"panelTx\":\"%s\",\"live\":true,"
                                       "\"adc\":[%u,%u,%u,%u,%u,%u,%u,%u],"
                                       "\"adcReads\":[%u,%u,%u,%u,%u,%u,%u,%u],"
                                       "\"duartOpr\":%u,\"adcMux\":%u,\"es5505Page\":%u,"
                                       "\"audioInputRate\":%u,\"audioInputBuffered\":%zu,"
                                       "\"audioInputPackets\":%llu,\"audioInputSamples\":%llu,"
                                       "\"audioInputPeak\":%u,\"audioInputConsumed\":%llu,"
                                       "\"audioInputConsumedPeak\":%u,\"audioInputAgeMs\":%llu,"
                                       "\"audioInputActive\":%s,\"audioInputRejectedPackets\":%llu}",
                                       display, decimal_mask, cursor_start, cursor_end,
                                       panel_tx_hex,
                                       adc_values[0] >> 6, adc_values[1] >> 6,
                                       adc_values[2] >> 6, adc_values[3] >> 6,
                                       adc_values[4] >> 6, adc_values[5] >> 6,
                                       adc_values[6] >> 6, adc_values[7] >> 6,
                                       adc_reads[0], adc_reads[1], adc_reads[2], adc_reads[3],
                                       adc_reads[4], adc_reads[5], adc_reads[6], adc_reads[7],
                                       duart_opr, adc_mux, es5505_page,
                                       input_rate, input_buffered,
                                       (unsigned long long)input_packets,
                                       (unsigned long long)input_samples,
                                       input_peak, (unsigned long long)input_consumed,
                                       input_consumed_peak,
                                       (unsigned long long)input_age_ms,
                                       input_active ? "true" : "false",
                                       (unsigned long long)input_rejected);
            http_send(client, "200 OK", "application/json", json, (size_t)json_length);
        } else if (!strcmp(method, "POST") &&
                   !strncmp(path, "/api/audio-input/activate/", 26)) {
            const char *session = path + 26;
            if (audio_input_session_valid(session)) audio_input_activate(session);
            http_send(client, "204 No Content", "text/plain", NULL, 0);
        } else if (!strcmp(method, "POST") &&
                   !strncmp(path, "/api/audio-input/deactivate/", 28)) {
            const char *session = path + 28;
            if (audio_input_session_valid(session)) audio_input_deactivate(session);
            http_send(client, "204 No Content", "text/plain", NULL, 0);
        } else if (!strcmp(method, "POST") &&
                   !strncmp(path, "/api/audio-input/data/", 22)) {
            const char *session = path + 22;
            char *body = strstr(request, "\r\n\r\n");
            if (body && audio_input_session_valid(session) &&
                audio_input_session_matches(session))
                audio_input_submit((const uint8_t *)(body + 4),
                                   needed - (size_t)(body + 4 - request));
            http_send(client, "204 No Content", "text/plain", NULL, 0);
        } else if (!strcmp(method, "POST") && !strcmp(path, "/api/event")) {
            char *body = strstr(request, "\r\n\r\n");
            if (body) command_enqueue(body + 4);
            http_send(client, "204 No Content", "text/plain", NULL, 0);
        } else if (!strcmp(method, "GET")) {
            char *query = strchr(path, '?');
            if (query) *query = '\0';
            http_serve_file(client, path);
        } else {
            http_send(client, "405 Method Not Allowed", "text/plain", "Method", 6);
        }
        close(client);
    }
    return NULL;
}

static int http_start(void) {
    const char *port_text = getenv("EPS16_HTTP_PORT");
    unsigned int port = port_text && *port_text ? (unsigned int)strtoul(port_text, NULL, 10)
                                                 : 8160U;
    if (!port || port > 65535) port = 8160U;
    http_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (http_socket < 0) return 0;
    int reuse = 1;
    setsockopt(http_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)port);
    if (bind(http_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("EPS live panel bind");
        close(http_socket);
        http_socket = -1;
        return 0;
    }
    if (listen(http_socket, 8) < 0) {
        perror("EPS live panel listen");
        close(http_socket);
        http_socket = -1;
        return 0;
    }
    if (pthread_create(&http_thread, NULL, http_server, NULL)) return 0;
    fprintf(stderr, "EPS live panel: http://127.0.0.1:%u\n", port);
    return 1;
}

static void audio_finished(void *context, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    (void)context;
    (void)queue;
    pthread_mutex_lock(&audio_mutex);
    if (!audio_stopping && free_audio_count < AUDIO_BUFFERS)
        free_audio[free_audio_count++] = buffer;
    pthread_cond_signal(&audio_condition);
    pthread_mutex_unlock(&audio_mutex);
}

static void midi_enqueue(uint8_t status, uint8_t data1, uint8_t data2) {
    pthread_mutex_lock(&midi_mutex);
    if (midi_count < MIDI_EVENTS) {
        midi_events[midi_write] = (LiveMidiEvent){status, data1, data2};
        midi_write = (midi_write + 1) % MIDI_EVENTS;
        ++midi_count;
    }
    pthread_mutex_unlock(&midi_mutex);
}

static void midi_received(const MIDIPacketList *packets, void *context, void *source) {
    (void)context;
    (void)source;
    const MIDIPacket *packet = &packets->packet[0];
    for (UInt32 packet_index = 0; packet_index < packets->numPackets; ++packet_index) {
        size_t offset = 0;
        while (offset < packet->length) {
            uint8_t status = packet->data[offset++];
            if (!(status & 0x80)) break;
            uint8_t kind = status & 0xf0;
            size_t needed = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
            if (offset + needed > packet->length) break;
            uint8_t data1 = packet->data[offset++];
            uint8_t data2 = needed == 2 ? packet->data[offset++] : 0;
            if (kind == 0x80 || kind == 0x90 || kind == 0xa0 ||
                kind == 0xb0 || kind == 0xe0)
                midi_enqueue(status, data1, data2);
        }
        packet = MIDIPacketNext(packet);
    }
}

int live_host_start(uint32_t sample_rate) {
    const char *disable_audio = getenv("EPS16_LIVE_NO_AUDIO");
    audio_disabled = disable_audio && *disable_audio && atoi(disable_audio) != 0;
    audio_output_rate = sample_rate;
    audio_disabled_next_time_ns = 0;
    AudioStreamBasicDescription format;
    memset(&format, 0, sizeof(format));
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                          kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = 4;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 4;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 16;
    if (!audio_disabled) {
        if (AudioQueueNewOutput(&format, audio_finished, NULL, NULL, NULL, 0,
                                &audio_queue) != noErr)
            return 0;
        for (unsigned int index = 0; index < AUDIO_BUFFERS; ++index) {
            AudioQueueBufferRef buffer;
            if (AudioQueueAllocateBuffer(audio_queue, AUDIO_FRAMES * 4, &buffer) != noErr)
                return 0;
            free_audio[free_audio_count++] = buffer;
        }
    }

    if (MIDIClientCreate(CFSTR("EPS-16 Live Test"), NULL, NULL, &midi_client) == noErr &&
        MIDIInputPortCreate(midi_client, CFSTR("Input"), midi_received, NULL,
                            &midi_port) == noErr) {
        ItemCount sources = MIDIGetNumberOfSources();
        for (ItemCount index = 0; index < sources; ++index)
            MIDIPortConnectSource(midi_port, MIDIGetSource(index), NULL);
        fprintf(stderr, "MIDI inputs connected: %lu\n", (unsigned long)sources);
    }
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    if (!http_start()) fprintf(stderr, "Warning: live browser panel could not start\n");
    fprintf(stderr,
            "Live commands: up, down, enter, track1, instrument, edit,\n"
            "  0..9, note MIDI VELOCITY, off MIDI, data 0..1023, raw HEX, display, quit\n");
    return 1;
}

void live_host_stop(void) {
    pthread_mutex_lock(&audio_mutex);
    audio_stopping = 1;
    pthread_cond_broadcast(&audio_condition);
    pthread_mutex_unlock(&audio_mutex);
    if (http_socket >= 0) {
        shutdown(http_socket, SHUT_RDWR);
        close(http_socket);
        http_socket = -1;
        pthread_join(http_thread, NULL);
    }
    if (audio_queue) {
        AudioQueueStop(audio_queue, 1);
        AudioQueueDispose(audio_queue, 1);
        audio_queue = NULL;
    }
    if (midi_port) MIDIPortDispose(midi_port);
    if (midi_client) MIDIClientDispose(midi_client);
}

void live_host_write(const int16_t *samples, size_t frames) {
    if (audio_disabled) {
        /* A silent diagnostic must preserve the same real-time CPU/DAC pace
           as CoreAudio.  Returning immediately makes the 68000 outrun a live
           wall-clock ADC and cannot validate recording duration. */
        if (!audio_output_rate || !frames) return;
        uint64_t now_ns = monotonic_time_ns();
        if (!audio_disabled_next_time_ns ||
            now_ns > audio_disabled_next_time_ns + 100000000ULL)
            audio_disabled_next_time_ns = now_ns;
        audio_disabled_next_time_ns +=
            ((uint64_t)frames * 1000000000ULL) / audio_output_rate;
        if (audio_disabled_next_time_ns > now_ns) {
            uint64_t wait_ns = audio_disabled_next_time_ns - now_ns;
            struct timespec wait = {
                .tv_sec = (time_t)(wait_ns / 1000000000ULL),
                .tv_nsec = (long)(wait_ns % 1000000000ULL)
            };
            nanosleep(&wait, NULL);
        }
        return;
    }
    while (frames && !audio_stopping) {
        if (!filling_audio) {
            pthread_mutex_lock(&audio_mutex);
            while (!free_audio_count && !audio_stopping)
                pthread_cond_wait(&audio_condition, &audio_mutex);
            if (!audio_stopping)
                filling_audio = free_audio[--free_audio_count];
            pthread_mutex_unlock(&audio_mutex);
            filling_frames = 0;
            if (!filling_audio) return;
        }
        size_t available = AUDIO_FRAMES - filling_frames;
        size_t count = frames < available ? frames : available;
        memcpy((int16_t *)filling_audio->mAudioData + filling_frames * 2,
               samples, count * 4);
        samples += count * 2;
        frames -= count;
        filling_frames += count;
        if (filling_frames == AUDIO_FRAMES) {
            filling_audio->mAudioDataByteSize = AUDIO_FRAMES * 4;
            AudioQueueEnqueueBuffer(audio_queue, filling_audio, 0, NULL);
            filling_audio = NULL;
            filling_frames = 0;
            if (++queued_audio >= 2 && !audio_started) {
                AudioQueueStart(audio_queue, NULL);
                audio_started = 1;
            }
        }
    }
}

int live_host_poll_line(char *line, size_t size) {
    pthread_mutex_lock(&command_mutex);
    if (command_count) {
        snprintf(line, size, "%s", command_lines[command_read]);
        command_read = (command_read + 1) % 32;
        --command_count;
        pthread_mutex_unlock(&command_mutex);
        return 1;
    }
    pthread_mutex_unlock(&command_mutex);
    char bytes[128];
    ssize_t count = read(STDIN_FILENO, bytes, sizeof(bytes));
    if (count <= 0) return 0;
    for (ssize_t index = 0; index < count; ++index) {
        char value = bytes[index];
        if (value == '\n' || value == '\r') {
            if (input_length) {
                input_buffer[input_length] = '\0';
                command_enqueue(input_buffer);
                input_length = 0;
                break;
            }
        } else if (input_length + 1 < sizeof(input_buffer)) {
            input_buffer[input_length++] = value;
        }
    }
    pthread_mutex_lock(&command_mutex);
    if (!command_count) {
        pthread_mutex_unlock(&command_mutex);
        return 0;
    }
    snprintf(line, size, "%s", command_lines[command_read]);
    command_read = (command_read + 1) % 32;
    --command_count;
    pthread_mutex_unlock(&command_mutex);
    return 1;
}

int live_host_poll_midi(LiveMidiEvent *event) {
    int available = 0;
    pthread_mutex_lock(&midi_mutex);
    if (midi_count) {
        *event = midi_events[midi_read];
        midi_read = (midi_read + 1) % MIDI_EVENTS;
        --midi_count;
        available = 1;
    }
    pthread_mutex_unlock(&midi_mutex);
    return available;
}

int live_host_audio_input_sample(uint32_t target_rate,
                                 uint64_t conversion_cycle,
                                 int16_t *result) {
    (void)conversion_cycle;
    pthread_mutex_lock(&audio_input_mutex);
    int16_t sample = 0;
    int available = 0;
    /* Buffered PCM remains valid until it has actually been consumed.  The
       packet timestamp is useful for the UI meter, but using it as a hard
       500 ms audio gate discarded an already queued tail as silence whenever
       browser delivery paused briefly. */
    if (audio_input_count && audio_input_rate && target_rate) {
        available = 1;
        audio_input_phase += audio_input_rate;
        size_t advance = (size_t)(audio_input_phase / target_rate);
        audio_input_phase %= target_rate;
        if (!advance) {
            sample = audio_input_last_sample;
        } else {
            if (advance > audio_input_count) advance = audio_input_count;
            while (advance--) {
                sample = audio_input_samples[audio_input_read];
                audio_input_read = (audio_input_read + 1) % AUDIO_INPUT_SAMPLES;
                --audio_input_count;
            }
            audio_input_last_sample = sample;
        }
    }
    unsigned int magnitude = sample < 0 ? (unsigned int)(-(int)sample)
                                        : (unsigned int)sample;
    if (magnitude > audio_input_consumed_peak)
        audio_input_consumed_peak = magnitude;
    ++audio_input_samples_consumed;
    pthread_mutex_unlock(&audio_input_mutex);
    if (result) *result = sample;
    return available;
}

void live_host_audio_input_prepare_recording(void) {
    pthread_mutex_lock(&audio_input_mutex);
    /* Chrome submits 1024-frame Web Audio blocks. Keep a bounded transport
       cushion at RECORD entry so the ADC does not underflow before the next
       HTTP block, while discarding older inactive microphone history. */
    const size_t cushion = 4 * 1024;
    if (audio_input_count > cushion) {
        audio_input_count = cushion;
        audio_input_read = (audio_input_write + AUDIO_INPUT_SAMPLES - cushion) %
                           AUDIO_INPUT_SAMPLES;
    }
    audio_input_phase = 0;
    audio_input_consumed_peak = 0;
    pthread_mutex_unlock(&audio_input_mutex);
}

void live_host_display(const char display[23], uint32_t decimal_mask,
                       int cursor_start, int cursor_end) {
    pthread_mutex_lock(&display_mutex);
    if (!memcmp(display, last_display, 22) && decimal_mask == last_decimal_mask &&
        cursor_start == last_cursor_start &&
        cursor_end == last_cursor_end) {
        pthread_mutex_unlock(&display_mutex);
        return;
    }
    memcpy(last_display, display, 22);
    last_display[22] = '\0';
    last_decimal_mask = decimal_mask;
    last_cursor_start = cursor_start;
    last_cursor_end = cursor_end;
    pthread_mutex_unlock(&display_mutex);
}

void live_host_clear_display_hold(void) {
}

void live_host_panel_tx(uint8_t value) {
    static const char hex[] = "0123456789abcdef";
    pthread_mutex_lock(&display_mutex);
    size_t length = strlen(last_panel_tx_hex);
    if (length == 128) {
        memmove(last_panel_tx_hex, last_panel_tx_hex + 2, 126);
        length = 126;
    }
    last_panel_tx_hex[length] = hex[value >> 4];
    last_panel_tx_hex[length + 1] = hex[value & 15];
    last_panel_tx_hex[length + 2] = '\0';
    pthread_mutex_unlock(&display_mutex);
}

void live_host_adc_state(const uint16_t values[8], const unsigned int reads[8],
                         unsigned int duart_opr, unsigned int es5505_page) {
    pthread_mutex_lock(&display_mutex);
    memcpy(last_adc_values, values, sizeof(last_adc_values));
    memcpy(last_adc_reads, reads, sizeof(last_adc_reads));
    last_duart_opr = duart_opr;
    last_es5505_page = es5505_page;
    pthread_mutex_unlock(&display_mutex);
}

#else

int live_host_start(uint32_t sample_rate) { (void)sample_rate; return 0; }
void live_host_stop(void) {}
void live_host_write(const int16_t *samples, size_t frames) {
    (void)samples; (void)frames;
}
int live_host_poll_line(char *line, size_t size) { (void)line; (void)size; return 0; }
int live_host_poll_midi(LiveMidiEvent *event) { (void)event; return 0; }
int live_host_audio_input_sample(uint32_t target_rate,
                                 uint64_t conversion_cycle,
                                 int16_t *sample) {
    (void)target_rate;
    (void)conversion_cycle;
    if (sample) *sample = 0;
    return 0;
}
void live_host_audio_input_prepare_recording(void) {}
void live_host_clear_display_hold(void) {}
void live_host_display(const char display[23], uint32_t decimal_mask,
                       int cursor_start, int cursor_end) {
    (void)display; (void)decimal_mask; (void)cursor_start; (void)cursor_end;
}
void live_host_panel_tx(uint8_t value) { (void)value; }

#endif
