#include "ProbeMachine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void run_for(uint64_t cycles) {
    eps16_probe_machine_run_until(eps16_probe_machine_cycles() + cycles);
}

static void click(uint8_t raw_code) {
    eps16_probe_machine_panel_byte((uint8_t)(raw_code | 0x80));
    eps16_probe_machine_panel_byte(0);
    run_for(1000000);
    eps16_probe_machine_panel_byte(raw_code);
    eps16_probe_machine_panel_byte(0);
    run_for(30000000);
}

static int click_without_partial_display(uint8_t raw_code,
                                         const char *expected) {
    char before[23];
    eps16_probe_machine_display(before);
    eps16_probe_machine_panel_byte((uint8_t)(raw_code | 0x80));
    eps16_probe_machine_panel_byte(0);
    for (unsigned int step = 0; step < 10; ++step) {
        char current[23];
        run_for(100000);
        eps16_probe_machine_display(current);
        if (strcmp(current, before) && strcmp(current, expected)) {
            fprintf(stderr, "partial display after press: |%s|\n", current);
            return 0;
        }
    }
    eps16_probe_machine_panel_byte(raw_code);
    eps16_probe_machine_panel_byte(0);
    for (unsigned int step = 0; step < 300; ++step) {
        char current[23];
        run_for(100000);
        eps16_probe_machine_display(current);
        if (strcmp(current, before) && strcmp(current, expected)) {
            fprintf(stderr, "partial display after release: |%s|\n", current);
            return 0;
        }
    }
    char final[23];
    eps16_probe_machine_display(final);
    return !strcmp(final, expected);
}

static int display_starts_with(const char *expected) {
    char display[23];
    eps16_probe_machine_display(display);
    printf("cycles=%llu display=|%s| illegal=%zu\n",
           (unsigned long long)eps16_probe_machine_cycles(), display,
           eps16_probe_machine_illegal_instructions());
    return !strncmp(display, expected, strlen(expected));
}

static int display_contains(const char *expected) {
    char display[23];
    eps16_probe_machine_display(display);
    printf("cycles=%llu display=|%s| illegal=%zu\n",
           (unsigned long long)eps16_probe_machine_cycles(), display,
           eps16_probe_machine_illegal_instructions());
    return strstr(display, expected) != NULL;
}

static int move_to_effect(int current, int target) {
    const uint8_t code = target > current ? 0x0a : 0x0b;
    char expected[8];
    snprintf(expected, sizeof(expected), "ROM-%02d", target);
    for (unsigned int attempt = 0;
         attempt < 8 && !display_contains(expected); ++attempt)
        click(code);
    return display_contains(expected);
}

static float play_root_key_and_measure(int effect) {
    eps16_probe_machine_midi(0x80, 60, 0);
    run_for(2000000);
    eps16_probe_machine_reset_audio_peaks();
    eps16_probe_machine_midi(0x90, 60, 100);
    float output_peak = 0.0f;
    for (unsigned int block = 0; block < 300; ++block) {
        run_for(100000);
        float left = 0.0f;
        float right = 0.0f;
        eps16_probe_machine_stereo_output(&left, &right);
        if (fabsf(left) > output_peak) output_peak = fabsf(left);
        if (fabsf(right) > output_peak) output_peak = fabsf(right);
    }
    const float rendered_peak = eps16_probe_machine_output_peak();
    printf("effect=%d bus=%f/%f/%f/%f esp=%f output=%f sampled=%f\n", effect,
           eps16_probe_machine_es5505_bus_peak(0),
           eps16_probe_machine_es5505_bus_peak(1),
           eps16_probe_machine_es5505_bus_peak(2),
           eps16_probe_machine_es5505_bus_peak(3),
           eps16_probe_machine_es5510_return_peak(), rendered_peak,
           output_peak);
    return rendered_peak;
}

static int display_is_blank(void) {
    char display[23];
    eps16_probe_machine_display(display);
    printf("cycles=%llu recording_display=|%s|\n",
           (unsigned long long)eps16_probe_machine_cycles(), display);
    return strspn(display, " ") == 22;
}

static int threshold_position(void) {
    char display[23];
    eps16_probe_machine_display(display);
    printf("threshold_display=|%s|\n", display);
    int position = -1;
    for (int index = 0; index < 22; ++index) {
        if (display[index] == '*') {
            if (position >= 0) return -2;
            position = index;
        } else if (display[index] != ' ' && display[index] != '|') {
            return -2;
        }
    }
    printf("threshold_position=%d\n", position);
    return position;
}

static int level_bar_count(void) {
    char display[23];
    eps16_probe_machine_display(display);
    int count = 0;
    for (int index = 0; index < 22; ++index)
        if (display[index] == '|') ++count;
    printf("level_display=|%s| bars=%d\n", display, count);
    return count;
}

static int save_external_snapshot(const char *path) {
    const size_t size = eps16_probe_machine_state_size();
    void *snapshot = malloc(size);
    if (!snapshot || !eps16_probe_machine_save_state(snapshot, size)) {
        free(snapshot);
        return 0;
    }
    FILE *file = fopen(path, "wb");
    int ok = file && fwrite(snapshot, 1, size, file) == size;
    if (file && fclose(file)) ok = 0;
    free(snapshot);
    return ok;
}

static void print_indicators(const char *label) {
    printf("indicators[%s]=%04x/%04x %04x/%04x %04x/%04x\n", label,
           eps16_probe_machine_indicator_on(0),
           eps16_probe_machine_indicator_flash(0),
           eps16_probe_machine_indicator_on(1),
           eps16_probe_machine_indicator_flash(1),
           eps16_probe_machine_indicator_on(2),
           eps16_probe_machine_indicator_flash(2));
}

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s COMBINED_ROM KPC_ROM OS_DISK [SNAPSHOT]\n",
                argv[0]);
        return 2;
    }
    Eps16ProbeMachine *machine = eps16_probe_machine_create();
    if (!machine || !eps16_probe_machine_begin(machine)) return 1;
    char error[256];
    if (!eps16_probe_machine_initialize(argv[1], argv[2], argv[3],
                                        error, sizeof(error))) {
        fprintf(stderr, "machine initialization failed: %s\n", error);
        return 1;
    }
    eps16_probe_machine_run_until(220000000);
    if (!display_starts_with("NO INSTRUMENTS")) return 1;
    print_indicators("load-instrument");
    if (eps16_probe_machine_illegal_instructions()) return 1;

    click(0x05);
    click(0x1b);
    run_for(10000000);
    display_starts_with("");
    print_indicators("edit-system-midi");

    /* Exercise the same raw physical KPC transitions emitted by the VST GUI.
       SAMPLE and Track 1 must remain decisions of the original firmware/OS. */
    click(0x20);
    if (!display_starts_with("PICK SAMPLE INSTRUMENT")) return 1;
    run_for(5000000);
    print_indicators("pick-sample-instrument");
    click(0x02);
    eps16_probe_machine_sampling_input(0.25f, -0.125f);
    run_for(50000000);
    display_starts_with("");
    print_indicators("sample-track1");
    if ((eps16_probe_machine_indicator_on(0) & 0x0101U) != 0x0101U)
        return 1;
    if (eps16_probe_machine_indicator_on(1) ||
        eps16_probe_machine_indicator_on(2))
        return 1;

    /* Keep a time-varying input connected while the original OS polls the ADC
       and renders its Level-Detect meter. This exact #/meter mode owns the
       sampling board monitor gate. */
    for (unsigned int sample = 0; sample < 1000; ++sample) {
        const float input = 0.25f * sinf((float)sample * 0.13f);
        eps16_probe_machine_sampling_input(input, input);
        run_for(1000);
    }
    if (!eps16_probe_machine_sampling_monitor_active()) return 1;
    if (level_bar_count() <= 0) return 1;

    const int threshold_before = threshold_position();
    if (threshold_before < 0) return 1;
    click(0x0a);
    const int threshold_after_up = threshold_position();
    if (threshold_after_up < 0 || threshold_after_up == threshold_before)
        return 1;
    click(0x0b);
    if (threshold_position() != threshold_before) return 1;

    /* INPUT LEVEL is an original OS variable (low RAM 0211), which drives
       the hardware CD4053 LINE/MIC feedback switch. Verify both circuit
       states and restore LINE through the serialized machine state. */
    const size_t input_level_state_size = eps16_probe_machine_state_size();
    void *input_level_state = malloc(input_level_state_size);
    if (!input_level_state ||
        !eps16_probe_machine_save_state(input_level_state,
                                        input_level_state_size))
        return 1;
    for (unsigned int page = 0; page < 4; ++page) click(0x11);
    if (!display_starts_with("INPUT LEVEL=LINE") ||
        eps16_probe_machine_sampling_mic_input())
        return 1;
    if (getenv("EPS16_TEST_MIC_WITH_DATA")) {
        eps16_probe_machine_analog(3, 0);
        run_for(50000000);
    } else {
        click(0x0b);
    }
    run_for(20000000);
    if (!display_starts_with("INPUT LEVEL=MIC") ||
        !eps16_probe_machine_sampling_mic_input())
        return 1;
    const int record_microphone = getenv("EPS16_TEST_RECORD_MIC") != NULL;
    if (record_microphone) {
        for (unsigned int page = 0; page < 4; ++page) click(0x10);
    } else if (!eps16_probe_machine_load_state(input_level_state,
                                               input_level_state_size,
                                               error, sizeof(error))) {
        return 1;
    }
    free(input_level_state);
    if (eps16_probe_machine_sampling_mic_input() != record_microphone ||
        threshold_position() < 0)
        return 1;

    /* The sampling overlay also emits non-character VFD traffic which remains
       deliberately undecoded. ENTER release is the original OS/KPC transition
       into RECORD; a later ENTER press is the deliberate stop transition. */
    const uint64_t writes_before =
        eps16_probe_machine_sample_ram_write_bytes();
    const uint64_t conversions_before =
        eps16_probe_machine_sampling_input_conversions();
    click(0x23);
    /* Keep a real time-varying source connected during RECORD. Holding the
       final pre-record sample for several seconds turns the diagnostic into
       a DC-settling test and makes menu dwell time affect the result. */
    for (unsigned int sample = 0; sample < 100000; ++sample) {
        const float input = 0.25f * sinf((float)sample * 0.13f);
        eps16_probe_machine_sampling_input(input, input);
        run_for(1000);
    }
    if (!display_is_blank()) return 1;
    print_indicators("recording");
    /* Bank 0 contains the loaded/selected Track LEDs. The original OS turns
       on only the fixed REC legend (right annunciator bank, index 3) once the
       recording transition has completed. */
    if (eps16_probe_machine_indicator_on(1) ||
        eps16_probe_machine_indicator_on(2) != 0x0008U)
        return 1;
    if (eps16_probe_machine_sampling_monitor_active()) return 1;
    const uint64_t recorded_bytes =
        eps16_probe_machine_sample_ram_write_bytes() - writes_before;
    const uint64_t input_conversions =
        eps16_probe_machine_sampling_input_conversions() - conversions_before;
    printf("recorded_bytes=%llu input_conversions=%llu\n",
           (unsigned long long)recorded_bytes,
           (unsigned long long)input_conversions);
    if (!recorded_bytes || !input_conversions) return 1;
    click(0x23);
    run_for(100000000);
    if (!display_starts_with("PLAY ROOT KEY")) return 1;
    if (eps16_probe_machine_sampling_monitor_active()) return 1;
    eps16_probe_machine_midi(0x90, 60, 100);
    float playback_peak = 0.0f;
    for (unsigned int block = 0; block < 300; ++block) {
        run_for(100000);
        float left = 0.0f;
        float right = 0.0f;
        eps16_probe_machine_stereo_output(&left, &right);
        if (fabsf(left) > playback_peak) playback_peak = fabsf(left);
        if (fabsf(right) > playback_peak) playback_peak = fabsf(right);
    }
    display_starts_with("");
    int cursor_start = -1;
    int cursor_end = -1;
    eps16_probe_machine_cursor(&cursor_start, &cursor_end);
    printf("cursor=%d..%d\n", cursor_start, cursor_end);
    if (cursor_start < 0 || cursor_end <= cursor_start || cursor_end > 22)
        return 1;
    printf("playback_peak=%f\n", playback_peak);
    printf("playback_path bus1=%f bus2=%f bus3=%f aux=%f esp=%f output=%f\n",
           eps16_probe_machine_es5505_bus_peak(0),
           eps16_probe_machine_es5505_bus_peak(1),
           eps16_probe_machine_es5505_bus_peak(2),
           eps16_probe_machine_es5505_bus_peak(3),
           eps16_probe_machine_es5510_return_peak(),
           eps16_probe_machine_output_peak());
    /* A mere nonzero check missed the input-stage regression which removed
       the physical LINE gain before the original ES5510 sampling filter.
       This deterministic 0.25-FS source must remain usefully audible. */
    if (playback_peak < 0.025f) return 1;
    if (argc == 5 && !save_external_snapshot(argv[4])) return 1;

    /* A complete ENV page contains several 71/72-terminated fields. The GUI
       must see the previous page or the complete next page, never the serial
       controller's partially rebuilt intermediate cells. */
    click(0x05);
    click(0x0d);
    if (!display_starts_with("ENVELOPE=CURRENT VALUE")) return 1;
    if (!click_without_partial_display(0x11,
            "HARDVEL=0  99 78 66 62")) return 1;

    /* Cursor segments are selected by original OS/KPC traffic.  A plain 62
       field on the Filter MODE page is not a visible cursor.  Conversely,
       62 60 03 marks every padded cell in the selected PAN MOD field; moving
       RIGHT transfers that mask to the signed amount including its sign. */
    click(0x05);
    click(0x19);
    if (!display_starts_with("MODE F1=") ||
        eps16_probe_machine_cursor_segment_mask() != 0)
        return 1;
    click(0x05);
    click(0x1e);
    if (!display_starts_with("WS VOLUME=")) return 1;
    for (unsigned int field = 0; field < 4; ++field) click(0x11);
    if (!display_starts_with("PAN MOD=LFO") ||
        eps16_probe_machine_cursor_segment_mask() !=
            (UINT32_C(0x1f) << 8))
        return 1;
    click(0x11);
    if (!display_starts_with("PAN MOD=LFO") ||
        eps16_probe_machine_cursor_segment_mask() !=
            (UINT32_C(0x07) << 16))
        return 1;
    /* Exercise the complete hardware path with the sample recorded above:
       ES5505 voice/bus routing -> ES5510 effect -> DAC output. */
    eps16_probe_machine_midi(0x80, 60, 0);
    click(0x07);
    eps16_probe_machine_analog(3, 715);
    run_for(50000000);
    if (!display_contains("ROM-13")) return 1;
    int current_effect = 13;
    const int effects[] = {10, 11, 12, 13, 11, 10};
    int effect_path_failed = 0;
    for (unsigned int index = 0;
         index < sizeof(effects) / sizeof(effects[0]); ++index) {
        const int effect = effects[index];
        if (effect != current_effect &&
            !move_to_effect(current_effect, effect))
            return 1;
        current_effect = effect;
        if (play_root_key_and_measure(effect) < 0.00001f)
            effect_path_failed = 1;
    }
    if (effect_path_failed) return 1;
    Eps16ProbeAudioFrame queued[64];
    uint64_t previous_audio_cycle = 0;
    size_t queued_audio_frames = 0;
    for (;;) {
        const size_t count = eps16_probe_machine_drain_audio(queued, 64);
        for (size_t index = 0; index < count; ++index) {
            if (queued[index].cpu_cycle < previous_audio_cycle ||
                queued[index].clock_divider < 16 ||
                queued[index].clock_divider > 512 ||
                queued[index].clock_divider % 16)
                return 1;
            previous_audio_cycle = queued[index].cpu_cycle;
        }
        queued_audio_frames += count;
        if (count < 64) break;
    }
    printf("queued_audio_frames=%zu last_audio_cycle=%llu\n",
           queued_audio_frames, (unsigned long long)previous_audio_cycle);
    if (!queued_audio_frames) return 1;

    const size_t snapshot_size = eps16_probe_machine_state_size();
    void *snapshot = malloc(snapshot_size);
    if (!snapshot || !eps16_probe_machine_save_state(snapshot, snapshot_size))
        return 1;
    const uint64_t saved_cycle = eps16_probe_machine_cycles();
    const uint64_t saved_writes = eps16_probe_machine_sample_ram_write_bytes();
    char saved_display[23];
    eps16_probe_machine_display(saved_display);
    run_for(5000000);
    if (eps16_probe_machine_cycles() == saved_cycle) return 1;
    char restore_error[256];
    if (!eps16_probe_machine_load_state(snapshot, snapshot_size,
                                        restore_error, sizeof(restore_error))) {
        fprintf(stderr, "snapshot restore failed: %s\n", restore_error);
        return 1;
    }
    free(snapshot);
    if (eps16_probe_machine_cycles() != saved_cycle ||
        eps16_probe_machine_sample_ram_write_bytes() != saved_writes)
        return 1;
    const size_t restore_release_before =
        eps16_probe_machine_panel_rx_consumed();
    run_for(20000000);
    const size_t restore_release_after =
        eps16_probe_machine_panel_rx_consumed();
    printf("restore_key_release_bytes=%zu\n",
           restore_release_after - restore_release_before);
    if (restore_release_after < restore_release_before + 61 * 2)
        return 1;
    char restored_display[23];
    eps16_probe_machine_display(restored_display);
    if (memcmp(saved_display, restored_display, sizeof(saved_display))) return 1;
    run_for(1000000);
    if (eps16_probe_machine_illegal_instructions()) return 1;
    char disk_error[256];
    if (!eps16_probe_machine_insert_disk(argv[3], disk_error,
                                         sizeof(disk_error))) {
        fprintf(stderr, "OS disk reinsertion failed: %s\n", disk_error);
        return 1;
    }
    run_for(10000000);
    if (eps16_probe_machine_illegal_instructions()) return 1;
    char img_path[128];
    char hfe_path[128];
    snprintf(img_path, sizeof(img_path), "/tmp/eps16-vst-disk-%ld.img",
             (long)getpid());
    snprintf(hfe_path, sizeof(hfe_path), "/tmp/eps16-vst-disk-%ld.hfe",
             (long)getpid());
    if (!eps16_probe_machine_save_disk(img_path, 0, disk_error,
                                       sizeof(disk_error)) ||
        !eps16_probe_machine_save_disk(hfe_path, 1, disk_error,
                                       sizeof(disk_error)) ||
        !eps16_probe_machine_insert_disk(hfe_path, disk_error,
                                         sizeof(disk_error))) {
        fprintf(stderr, "disk save/reload failed: %s\n", disk_error);
        remove(img_path);
        remove(hfe_path);
        return 1;
    }
    remove(img_path);
    remove(hfe_path);
    printf("snapshot_size=%zu restored_cycle=%llu\n", snapshot_size,
           (unsigned long long)saved_cycle);
    eps16_probe_machine_midi(0x80, 60, 0);
    run_for(1000000);
    eps16_probe_machine_end(machine);
    eps16_probe_machine_destroy(machine);
    return 0;
}
