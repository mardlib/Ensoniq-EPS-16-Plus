#include "ProbeMachine.h"

#include <string.h>

/* Compile the verified probe into the private VST translation unit while
   selecting an explicit core context for every plug-in instance. */
static void plugin_capture_display(const char display[23], uint32_t decimal_mask,
                                   int cursor_start, int cursor_end,
                                   uint32_t cursor_mask);
static void plugin_capture_keyon(unsigned int voice);

#define EPS16_PANEL_DISPLAY_PUBLISHED(display, decimal_mask, cursor_start, cursor_end, cursor_mask) \
    plugin_capture_display((display), (decimal_mask), (cursor_start), (cursor_end), (cursor_mask))
#define EPS16_ES5505_KEYON_PUBLISHED(voice) plugin_capture_keyon((voice))
#define EPS16_ROM_PROBE_CONTEXT 1
#define EPS16_PLUGIN_BUILD 1
#define main eps16_probe_cli_main
#include "../native/rom_probe.c"
#undef main
#undef EPS16_PLUGIN_BUILD
#undef EPS16_ROM_PROBE_CONTEXT
#undef EPS16_ES5505_KEYON_PUBLISHED
#undef EPS16_PANEL_DISPLAY_PUBLISHED

#include "m68kcpu.h"

#include <math.h>

enum { PLUGIN_AUDIO_QUEUE_CAPACITY = 1024 };

typedef struct {
    int initialized;
    uint64_t executed;
    uint64_t audio_scheduled_cycle;
    uint64_t audio_cycle_accumulator;
    unsigned int timer_irqs;
    int16_t input_sample;
    int input_valid;
    float output_left, output_right;
    Eps16ProbeAudioFrame audio_queue[PLUGIN_AUDIO_QUEUE_CAPACITY];
    size_t audio_queue_read, audio_queue_write;
    uint8_t panel_pair[2];
    size_t panel_pair_count;
    char published_display[23];
    uint32_t published_decimal_mask;
    uint32_t published_cursor_mask;
    int published_cursor_start, published_cursor_end;
    uint32_t last_keyon_frequency;
    uint32_t restored_voice_mask;
    unsigned int restored_quiet_frames;
    int restored_output_suppressed;
} PluginRuntimeState;

struct Eps16ProbeMachine {
    RomProbeState core;
    PluginRuntimeState plugin;
    m68ki_cpu_core cpu;
};

static _Thread_local Eps16ProbeMachine *plugin_current_machine;

#define plugin_initialized (plugin_current_machine->plugin.initialized)
#define plugin_executed (plugin_current_machine->plugin.executed)
#define plugin_audio_scheduled_cycle \
    (plugin_current_machine->plugin.audio_scheduled_cycle)
#define plugin_audio_cycle_accumulator \
    (plugin_current_machine->plugin.audio_cycle_accumulator)
#define plugin_timer_irqs (plugin_current_machine->plugin.timer_irqs)
#define plugin_input_sample (plugin_current_machine->plugin.input_sample)
#define plugin_input_valid (plugin_current_machine->plugin.input_valid)
#define plugin_output_left (plugin_current_machine->plugin.output_left)
#define plugin_output_right (plugin_current_machine->plugin.output_right)
#define plugin_audio_queue (plugin_current_machine->plugin.audio_queue)
#define plugin_audio_queue_read (plugin_current_machine->plugin.audio_queue_read)
#define plugin_audio_queue_write (plugin_current_machine->plugin.audio_queue_write)
#define plugin_panel_pair (plugin_current_machine->plugin.panel_pair)
#define plugin_panel_pair_count (plugin_current_machine->plugin.panel_pair_count)
#define plugin_published_display (plugin_current_machine->plugin.published_display)
#define plugin_published_decimal_mask \
    (plugin_current_machine->plugin.published_decimal_mask)
#define plugin_published_cursor_mask \
    (plugin_current_machine->plugin.published_cursor_mask)
#define plugin_published_cursor_start \
    (plugin_current_machine->plugin.published_cursor_start)
#define plugin_published_cursor_end \
    (plugin_current_machine->plugin.published_cursor_end)
#define plugin_last_keyon_frequency \
    (plugin_current_machine->plugin.last_keyon_frequency)
#define plugin_restored_voice_mask \
    (plugin_current_machine->plugin.restored_voice_mask)
#define plugin_restored_quiet_frames \
    (plugin_current_machine->plugin.restored_quiet_frames)
#define plugin_restored_output_suppressed \
    (plugin_current_machine->plugin.restored_output_suppressed)

static void plugin_capture_display(const char display[23], uint32_t decimal_mask,
                                   int cursor_start, int cursor_end,
                                   uint32_t cursor_mask) {
    if (!plugin_current_machine) return;
    memcpy(plugin_published_display, display, 23);
    plugin_published_decimal_mask = decimal_mask;
    plugin_published_cursor_mask = cursor_mask;
    plugin_published_cursor_start = cursor_start;
    plugin_published_cursor_end = cursor_end;
}

static void plugin_capture_keyon(unsigned int voice) {
    if (plugin_current_machine && voice < ES5505_VOICES) {
        plugin_last_keyon_frequency = es5505.voices[voice].frequency;
        /* The OS has now constructed a genuinely new voice after restore.
           It is intentional live/sequencer activity, so audio may resume. */
        plugin_restored_voice_mask = 0;
        plugin_restored_quiet_frames = 0;
        plugin_restored_output_suppressed = 0;
    }
}

#define PLUGIN_SNAPSHOT_FIELDS(X) \
    X(low_ram) X(sample_ram) X(sample_ram_write_bytes) X(os_ram) \
    X(es5505_writes) X(es5510) X(es5510_gpr_latch) \
    X(es5510_instruction_latch) X(es5510_dil_latch) X(es5510_dol_latch) \
    X(es5510_dadr_latch) X(es5510_ram_read) X(es5510_dram_reads) \
    X(es5510_dram_writes) X(es5510_gpr_writes) X(es5510_instruction_writes) \
    X(es5510_host_serial) X(es5510_host_serial_writes) \
    X(es5510_host_upload_active) X(es5510_host_access_until) X(live_mode) \
    X(deterministic_host_input) X(es5510_input_next_cycle) \
    X(es5510_input_next_time_ns) X(es5510_input_last_poll_time_ns) \
    X(es5510_input_last_poll_cycle) X(es5510_input_polls) \
    X(es5510_input_valid) X(es5510_input_bypass) \
    X(sample_record_input_valid_start) X(sample_record_write_start) \
    X(function_code) X(duart_registers) X(panel_rx) X(panel_rx_read) \
    X(panel_rx_write) X(panel_rx_count) X(panel_rx_consumed) X(panel_wire) \
    X(panel_wire_read) X(panel_wire_write) X(panel_wire_count) \
    X(panel_wire_tail_cycle) X(panel_tx) X(panel_tx_count) X(current_cycle) \
    X(panel_display) X(panel_display_decimal_mask) X(panel_display_dirty) \
    X(panel_display_last_change_cycle) X(panel_cursor) X(panel_cursor_start) \
    X(panel_cursor_end) X(panel_cursor_active) X(panel_cursor_width_pending) \
    X(panel_cursor_width_known) X(panel_noncell_parameter_pending) \
    X(panel_last_tx) X(panel_pick_instrument_seen) X(panel_file_loaded_seen) \
    X(disk_image) X(disk_loaded) X(disk_change_pending) X(fdc_track) \
    X(fdc_physical_track) X(fdc_sector) X(fdc_data_register) \
    X(fdc_last_command) X(fdc_step_direction) X(fdc_remaining) \
    X(fdc_data_reads) X(duart_output) X(duart_tx_a_enabled) \
    X(duart_tx_a_ready) X(duart_tx_a_ready_cycle) X(duart_tx_b_enabled) \
    X(duart_tx_b_ready) X(duart_tx_b_ready_cycle) X(midi_tx) \
    X(midi_tx_count) X(analog_values) X(analog_reads) \
    X(duart_timer_pending) X(duart_timer_running) X(duart_timer_next_cycle) \
    X(fdc_reads) X(dmac_registers) X(dmac_irq_channel) X(dmac_transfers) \
    X(dmac_pcl_level) X(kpc) X(kpc_firmware_execution) \
    X(kpc_firmware_failure_reported) X(kpc_physical_queue) \
    X(kpc_physical_queue_read) X(kpc_physical_queue_write) \
    X(kpc_physical_queue_count) X(kpc_physical_expected) \
    X(kpc_physical_active) X(kpc_physical_saw_code) \
    X(kpc_physical_next_cycle) X(display_trace_bytes) \
    X(display_trace_byte_count) X(illegal_instructions) \
    X(illegal_instruction_count) X(live_press_cycle) \
    X(sampling_enter_pending) X(sampling_enter_release_cycle) \
    X(sampling_recording_active) X(plugin_executed) \
    X(plugin_audio_scheduled_cycle) X(plugin_audio_cycle_accumulator) \
    X(plugin_timer_irqs) X(plugin_input_sample) X(plugin_input_valid) \
    X(plugin_output_left) X(plugin_output_right) X(plugin_panel_pair) \
    X(plugin_panel_pair_count)

#define PLUGIN_SNAPSHOT_V2_FIELDS(X) \
    X(panel_indicator_on) X(panel_indicator_flash) \
    X(panel_indicator_command_pending)

#define PLUGIN_SNAPSHOT_V3_FIELDS(X) \
    X(sampling_input_circuit)

#define PLUGIN_SNAPSHOT_V4_FIELDS(X) \
    X(panel_cursor_segment_mask) X(panel_cursor_full_segments) \
    X(plugin_published_display) X(plugin_published_decimal_mask) \
    X(plugin_published_cursor_mask) X(plugin_published_cursor_start) \
    X(plugin_published_cursor_end)

#define PLUGIN_SNAPSHOT_V5_FIELDS(X) \
    X(panel_threshold_position) X(panel_level_active) \
    X(panel_level_setup_state) X(panel_level_pending) \
    X(panel_level_pending_value) X(panel_level_value)

#define PLUGIN_SNAPSHOT_V6_FIELDS(X) \
    X(panel_cell_address_pending) X(panel_cell_address) \
    X(panel_edit_cursor) X(panel_cursor_command_state) \
    X(panel_cursor_positioned) X(panel_text_mode_pending)

Eps16ProbeMachine *eps16_probe_machine_create(void) {
    Eps16ProbeMachine *machine = calloc(1, sizeof(*machine));
    if (!machine) return NULL;
    rom_probe_state_defaults(&machine->core);
    memset(machine->plugin.published_display, ' ', 22);
    machine->plugin.published_display[22] = '\0';
    machine->plugin.published_cursor_start = -1;
    machine->plugin.published_cursor_end = -1;
    return machine;
}

void eps16_probe_machine_destroy(Eps16ProbeMachine *machine) {
    if (!machine) return;
    free(machine);
}

int eps16_probe_machine_begin(Eps16ProbeMachine *machine) {
    if (!machine || plugin_current_machine) return 0;
    plugin_current_machine = machine;
    rom_probe_state = &machine->core;
    m68k_set_context(&machine->cpu);
    return 1;
}

void eps16_probe_machine_end(Eps16ProbeMachine *machine) {
    if (!machine || plugin_current_machine != machine) return;
    m68k_get_context(&machine->cpu);
    rom_probe_state = NULL;
    plugin_current_machine = NULL;
}

typedef struct {
    uint8_t magic[8];
    uint32_t version;
    uint32_t header_size;
    uint64_t total_size;
    uint64_t checksum;
    uint32_t m68k_context_size;
    uint32_t reserved;
    int64_t fdc_data_offset;
} PluginSnapshotHeader;

typedef struct {
    uint8_t *current;
    uint8_t *end;
    int valid;
} PluginSnapshotWriter;

typedef struct {
    const uint8_t *current;
    const uint8_t *end;
    int valid;
} PluginSnapshotReader;

static void snapshot_write(PluginSnapshotWriter *writer,
                           const void *source, size_t size) {
    if (!writer->valid || size > (size_t)(writer->end - writer->current)) {
        writer->valid = 0;
        return;
    }
    memcpy(writer->current, source, size);
    writer->current += size;
}

static void snapshot_read(PluginSnapshotReader *reader,
                          void *destination, size_t size) {
    if (!reader->valid || size > (size_t)(reader->end - reader->current)) {
        reader->valid = 0;
        return;
    }
    memcpy(destination, reader->current, size);
    reader->current += size;
}

static uint64_t snapshot_checksum(const uint8_t *data, size_t size) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void plugin_error(char *error, size_t error_size, const char *message) {
    if (error && error_size) snprintf(error, error_size, "%s", message);
}

static void plugin_queue_audio(uint64_t cycle, uint32_t divider,
                               float left, float right) {
    const size_t next = (plugin_audio_queue_write + 1) % PLUGIN_AUDIO_QUEUE_CAPACITY;
    if (next == plugin_audio_queue_read)
        plugin_audio_queue_read = (plugin_audio_queue_read + 1) % PLUGIN_AUDIO_QUEUE_CAPACITY;
    plugin_audio_queue[plugin_audio_queue_write] =
        (Eps16ProbeAudioFrame){cycle, divider, left, right};
    plugin_audio_queue_write = next;
}

static void plugin_render_audio(uint64_t elapsed_cycles, uint64_t end_cycle) {
    const uint32_t output_divider = es5505_core_output_divider(&es5505);
    plugin_audio_cycle_accumulator += elapsed_cycles;
    uint64_t frames_due = plugin_audio_cycle_accumulator / output_divider;
    plugin_audio_cycle_accumulator %= output_divider;
    uint64_t frame_cycle = frames_due
        ? end_cycle - plugin_audio_cycle_accumulator -
              (frames_due - 1) * output_divider
        : 0;
    while (frames_due) {
        int32_t buses[ES5505_STEREO_BUSES * 2][64];
        int32_t *bus_outputs[ES5505_STEREO_BUSES * 2];
        size_t chunk = frames_due > 64 ? 64 : (size_t)frames_due;
        for (unsigned int output = 0;
             output < ES5505_STEREO_BUSES * 2; ++output)
            bus_outputs[output] = buses[output];
        es5505_core_render_buses(&es5505, bus_outputs, chunk);
        if (plugin_restored_voice_mask) {
            for (unsigned int voice = 0; voice < ES5505_VOICES; ++voice)
                if (es5505.voices[voice].control & ES5505_STOP_MASK)
                    plugin_restored_voice_mask &=
                        ~(UINT32_C(1) << voice);
        }
        audio_frames += chunk;
        frames_due -= chunk;
        for (size_t frame = 0; frame < chunk; ++frame) {
            for (unsigned int bus = 0; bus < ES5505_STEREO_BUSES; ++bus) {
                int32_t magnitude = buses[bus * 2][frame] < 0
                    ? -buses[bus * 2][frame] : buses[bus * 2][frame];
                if (magnitude > es5505_bus_peak[bus])
                    es5505_bus_peak[bus] = magnitude;
                magnitude = buses[bus * 2 + 1][frame] < 0
                    ? -buses[bus * 2 + 1][frame] : buses[bus * 2 + 1][frame];
                if (magnitude > es5505_bus_peak[bus])
                    es5505_bus_peak[bus] = magnitude;
            }
            /* Main-board U41 (74LS157) selects either ES5505 DSER0 or the
               mono ADC A/DATA for ES5510 SER0. Its SAMPEN select is the
               physical MC68681 OP2 pin, which is active-low relative to the
               DUART set/reset command latch. Waveboy effects use those
               ordinary hardware writes; no effect-name recognition is
               involved. */
            const int external_input_selected = !(duart_output & 0x04);
            const int16_t esp_inputs[8] = {
                external_input_selected ? plugin_input_sample
                                        : audio_to_pcm16(buses[0][frame]),
                external_input_selected ? 0
                                        : audio_to_pcm16(buses[1][frame]),
                0, 0,
                audio_to_pcm16(buses[2][frame]),
                audio_to_pcm16(buses[3][frame]),
                audio_to_pcm16(buses[6][frame]),
                audio_to_pcm16(buses[7][frame])
            };
            int16_t esp_outputs[2] = {0, 0};
            if (es5510_host_upload_active &&
                (uint64_t)current_cycle >= es5510_host_access_until) {
                es5510_host_upload_active = 0;
                es5510_core_set_halted(&es5510, 0);
            }
            /* The sampling overlay is clocked by ADC conversions below. An
               additional DAC-rate run would inject false serial-input zeros
               between samples and change its anti-alias response. */
            if (!sampling_adc_is_active())
                es5510_core_process(&es5510, esp_inputs, esp_outputs);
            int32_t esp_magnitude = esp_outputs[0] < 0
                ? -(int32_t)esp_outputs[0] : (int32_t)esp_outputs[0];
            if (esp_magnitude > es5510_return_peak)
                es5510_return_peak = esp_magnitude;
            esp_magnitude = esp_outputs[1] < 0
                ? -(int32_t)esp_outputs[1] : (int32_t)esp_outputs[1];
            if (esp_magnitude > es5510_return_peak)
                es5510_return_peak = esp_magnitude;
            const int32_t output_left =
                apply_master_volume((int32_t)esp_outputs[0] << 4);
            const int32_t output_right =
                apply_master_volume((int32_t)esp_outputs[1] << 4);
            if (plugin_restored_output_suppressed) {
                const int restored_output_quiet =
                    !plugin_restored_voice_mask &&
                    esp_outputs[0] >= -1 && esp_outputs[0] <= 1 &&
                    esp_outputs[1] >= -1 && esp_outputs[1] <= 1;
                plugin_restored_quiet_frames = restored_output_quiet
                    ? plugin_restored_quiet_frames + 1 : 0;
                if (plugin_restored_quiet_frames >= 2048)
                    plugin_restored_output_suppressed = 0;
            }
            const int32_t visible_output_left =
                plugin_restored_output_suppressed ? 0 : output_left;
            const int32_t visible_output_right =
                plugin_restored_output_suppressed ? 0 : output_right;
            int32_t output_magnitude = visible_output_left < 0
                ? -visible_output_left : visible_output_left;
            if (output_magnitude > audio_peak) audio_peak = output_magnitude;
            output_magnitude = visible_output_right < 0
                ? -visible_output_right : visible_output_right;
            if (output_magnitude > audio_peak) audio_peak = output_magnitude;
            plugin_output_left =
                (float)audio_to_pcm16(visible_output_left) / 32768.0f;
            plugin_output_right =
                (float)audio_to_pcm16(visible_output_right) / 32768.0f;
            plugin_queue_audio(frame_cycle, output_divider,
                               plugin_output_left, plugin_output_right);
            frame_cycle += output_divider;
        }
    }
}

static int plugin_initialize_loaded_rom(const char *kpc_path,
                                        const char *os_disk_path,
                                        char *error, size_t error_size) {
    if (!load_disk(os_disk_path)) {
        plugin_error(error, error_size, "EPS OS disk could not be decoded");
        return 0;
    }
    kpc_legacy_init(&kpc);
    char kpc_error[256];
    if (!kpc_device_load(&kpc_device, kpc_path, kpc_error, sizeof(kpc_error))) {
        plugin_error(error, error_size, kpc_error);
        return 0;
    }
    kpc_firmware_execution = 1;
    kpc_device_set_capture_clock(&kpc_device, 2, 40000);
    deterministic_host_input = 1;
    live_mode = 0;
    memset(plugin_published_display, ' ', 22);
    plugin_published_display[22] = '\0';
    plugin_published_decimal_mask = 0;
    plugin_published_cursor_mask = 0;
    plugin_published_cursor_start = -1;
    plugin_published_cursor_end = -1;
    plugin_last_keyon_frequency = 0;

    m68k_init();
    es5505_core_init(&es5505, es5505_sample_read, NULL);
    es5510_core_init(&es5510);
    es5505_core_set_port_reader(&es5505, es5505_port_read, NULL);
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_fc_callback(set_function_code);
    m68k_set_int_ack_callback(interrupt_acknowledge);
    m68k_set_illg_instr_callback(illegal_instruction);
    m68k_set_instr_hook_callback(instruction_hook);
    m68k_pulse_reset();
    plugin_initialized = 1;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int eps16_probe_machine_initialize(const char *rom_path, const char *kpc_path,
                                   const char *os_disk_path,
                                   char *error, size_t error_size) {
    if (plugin_initialized) return 1;
    if (!rom_path || !*rom_path || !kpc_path || !*kpc_path ||
        !os_disk_path || !*os_disk_path) {
        plugin_error(error, error_size, "ROM, KPC ROM and OS disk are required");
        return 0;
    }
    if (!load_rom(rom_path)) {
        plugin_error(error, error_size, "combined EPS ROM must be exactly 128 KiB");
        return 0;
    }
    return plugin_initialize_loaded_rom(kpc_path, os_disk_path,
                                        error, error_size);
}

int eps16_probe_machine_initialize_split_rom(const char *upper_rom_path,
                                             const char *lower_rom_path,
                                             const char *kpc_path,
                                             const char *os_disk_path,
                                             char *error, size_t error_size) {
    if (plugin_initialized) return 1;
    if (!upper_rom_path || !*upper_rom_path ||
        !lower_rom_path || !*lower_rom_path ||
        !kpc_path || !*kpc_path || !os_disk_path || !*os_disk_path) {
        plugin_error(error, error_size,
                     "U28, U27, KPC ROM and OS disk are required");
        return 0;
    }
    if (!load_split_rom(upper_rom_path, lower_rom_path)) {
        plugin_error(error, error_size,
                     "EPS 1.00F U28/U27 ROM pair could not be combined");
        return 0;
    }
    return plugin_initialize_loaded_rom(kpc_path, os_disk_path,
                                        error, error_size);
}

int eps16_probe_machine_insert_disk(const char *disk_path,
                                    char *error, size_t error_size) {
    if (!plugin_initialized) {
        plugin_error(error, error_size, "machine is not initialized");
        return 0;
    }
    if (!disk_path || !*disk_path || !load_disk(disk_path)) {
        plugin_error(error, error_size,
                     "disk must be a valid EPS .IMG or HFE v1 image");
        return 0;
    }
    disk_change_pending = 1;
    fdc_track = 0;
    fdc_physical_track = 0;
    fdc_sector = 0;
    fdc_remaining = 0;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int eps16_probe_machine_create_blank_disk(char *error, size_t error_size) {
    if (!plugin_initialized) {
        plugin_error(error, error_size, "machine is not initialized");
        return 0;
    }
    if (!eps16_disk_create_blank(disk_image, sizeof(disk_image))) {
        plugin_error(error, error_size, "cannot create blank EPS disk");
        return 0;
    }
    disk_loaded = 1;
    disk_change_pending = 1;
    fdc_track = 0;
    fdc_physical_track = 0;
    fdc_sector = 0;
    fdc_remaining = 0;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int eps16_probe_machine_save_disk(const char *disk_path, int hfe_format,
                                  char *error, size_t error_size) {
    if (!plugin_initialized || !disk_loaded) {
        plugin_error(error, error_size, "no disk is inserted");
        return 0;
    }
    if (!disk_path || !*disk_path) {
        plugin_error(error, error_size, "disk output path is empty");
        return 0;
    }
    if (fdc_remaining && (fdc_last_command & 0xe0) == 0xa0) {
        plugin_error(error, error_size,
                     "disk write is still in progress; try Save again");
        return 0;
    }
    return eps16_disk_save(disk_path, disk_image, sizeof(disk_image),
                           hfe_format ? EPS16_DISK_HFE : EPS16_DISK_IMG,
                           error, error_size);
}

int eps16_probe_machine_is_initialized(void) {
    return plugin_initialized;
}

void eps16_probe_machine_run_until(uint64_t target_cycle) {
    if (!plugin_initialized) return;
    while (plugin_executed < target_cycle) {
        current_cycle = (long long)plugin_executed;
        duart_service_time(plugin_executed);
        kpc_execution_service(plugin_executed);
        uint64_t remaining = target_cycle - plugin_executed;
        int budget = remaining > 50000 ? 50000 : (int)remaining;
        uint64_t next_cycle = plugin_executed + (uint64_t)budget;
        if (duart_timer_running && duart_timer_next_cycle < next_cycle)
            next_cycle = duart_timer_next_cycle;
        if (panel_wire_count && panel_rx_count < PANEL_RX_SIZE &&
            panel_wire[panel_wire_read].cycle < next_cycle)
            next_cycle = panel_wire[panel_wire_read].cycle;
        if (!duart_tx_a_ready && duart_tx_a_ready_cycle < next_cycle)
            next_cycle = duart_tx_a_ready_cycle;
        if (!duart_tx_b_ready && duart_tx_b_ready_cycle < next_cycle)
            next_cycle = duart_tx_b_ready_cycle;
        budget = next_cycle > plugin_executed
                     ? (int)(next_cycle - plugin_executed) : 1;
        const int slice = m68k_execute(budget);
        if (slice <= 0) break;
        plugin_executed += (uint64_t)slice;
        current_cycle = (long long)plugin_executed;
        duart_service_time(plugin_executed);
        kpc_execution_service(plugin_executed);
        dmac_service();
        if (dmac_irq_channel >= 0) {
            m68k_set_irq(2);
            plugin_executed += (uint64_t)m68k_execute(128);
            current_cycle = (long long)plugin_executed;
            duart_service_time(plugin_executed);
            m68k_set_irq(0);
        }
        if (duart_interrupt_status() & duart_registers[5]) {
            m68k_set_irq(3);
            plugin_executed += (uint64_t)m68k_execute(128);
            current_cycle = (long long)plugin_executed;
            duart_service_time(plugin_executed);
            m68k_set_irq(0);
            ++plugin_timer_irqs;
        }
        const uint64_t elapsed = plugin_executed - plugin_audio_scheduled_cycle;
        plugin_audio_scheduled_cycle = plugin_executed;
        plugin_render_audio(elapsed, plugin_executed);
        if (es5505_core_irq_pending(&es5505)) {
            m68k_set_irq(1);
            plugin_executed += (uint64_t)m68k_execute(128);
            current_cycle = (long long)plugin_executed;
            duart_service_time(plugin_executed);
            m68k_set_irq(0);
            ++es5505_irqs;
        }
    }
    if (panel_display_dirty &&
        current_cycle - panel_display_last_change_cycle >= 100000) {
        plugin_capture_display(panel_display, panel_display_decimal_mask,
                               panel_cursor_start, panel_cursor_end,
                               panel_cursor_segment_mask);
        panel_display_dirty = 0;
    }
}

size_t eps16_probe_machine_drain_audio(Eps16ProbeAudioFrame *frames,
                                       size_t capacity) {
    if (!frames || !capacity) return 0;
    size_t count = 0;
    while (count < capacity && plugin_audio_queue_read != plugin_audio_queue_write) {
        frames[count++] = plugin_audio_queue[plugin_audio_queue_read];
        plugin_audio_queue_read =
            (plugin_audio_queue_read + 1) % PLUGIN_AUDIO_QUEUE_CAPACITY;
    }
    return count;
}

void eps16_probe_machine_midi(uint8_t status, uint8_t data1, uint8_t data2) {
    if (!plugin_initialized) return;
    if (status >= 0xf8) {
        midi_schedule_at(status, bus_cycle_now());
        return;
    }
    const unsigned int kind = status & 0xf0;
    const unsigned int channel = status & 0x0f;
    if (kind == 0x90 && data2) {
        live_note(data1, data2, 1);
    } else if (kind == 0x80 || (kind == 0x90 && !data2))
        live_note(data1, data2, 0);
    else if (kind == 0xa0 && channel == 0 &&
             midi_wire_count <=
                 sizeof(midi_wire) / sizeof(midi_wire[0]) - 3) {
        midi_schedule_at(status, bus_cycle_now());
        midi_schedule_at(data1, bus_cycle_now());
        midi_schedule_at(data2, bus_cycle_now());
    } else if ((kind == 0xe0 || (kind == 0xb0 && data1 == 1)) &&
               channel == 0)
        live_performance_midi(status, data1, data2);
}

void eps16_probe_machine_keyboard(uint8_t note, uint8_t velocity,
                                  int pressed) {
    if (!plugin_initialized) return;
    live_note(note, velocity, pressed != 0);
}

size_t eps16_probe_machine_midi_bytes(const uint8_t *bytes, size_t size) {
    if (!plugin_initialized || (!bytes && size)) return 0;
    size_t accepted = 0;
    while (accepted < size &&
           midi_schedule_at(bytes[accepted], bus_cycle_now()))
        ++accepted;
    return accepted;
}

size_t eps16_probe_machine_drain_midi_output(uint8_t *bytes,
                                             size_t capacity) {
    if (!plugin_initialized || !bytes || !capacity) return 0;
    const size_t count = midi_tx_count < capacity ? midi_tx_count : capacity;
    memcpy(bytes, midi_tx, count);
    midi_tx_count -= count;
    if (midi_tx_count)
        memmove(midi_tx, midi_tx + count, midi_tx_count);
    return count;
}

size_t eps16_probe_machine_midi_rx_consumed(void) {
    return plugin_initialized ? midi_rx_consumed : 0;
}

void eps16_probe_machine_panel_byte(uint8_t value) {
    if (!plugin_initialized) return;
    plugin_panel_pair[plugin_panel_pair_count++] = value;
    if (plugin_panel_pair_count == 2) {
        kpc_execution_panel_packet(plugin_panel_pair, 2, plugin_executed);
        plugin_panel_pair_count = 0;
    }
}

void eps16_probe_machine_analog(unsigned int channel, uint16_t value) {
    if (!plugin_initialized || channel >= 8) return;
    analog_values[channel] = (uint16_t)((value > 1023 ? 1023 : value) << 6);
}

void eps16_probe_machine_sampling_input_rate(double sample_rate) {
    if (!plugin_current_machine) return;
    sampling_input_circuit_set_rate(&sampling_input_circuit, sample_rate);
}

float eps16_probe_machine_sampling_input(float left, float right) {
    if (!plugin_initialized) return 0.0f;
    float mono = 0.5f * (left + right);
    if (mono > 1.0f) mono = 1.0f;
    if (mono < -1.0f) mono = -1.0f;
    const float filtered = sampling_input_circuit_process(
        &sampling_input_circuit, mono, low_ram[0x0211] == 0);
    plugin_input_sample = (int16_t)lrintf(filtered * 32767.0f);
    plugin_input_valid = 1;
    return filtered;
}

int eps16_probe_machine_sampling_mic_input(void) {
    return plugin_initialized && low_ram[0x0211] == 0;
}

void eps16_probe_machine_stereo_output(float *left, float *right) {
    if (left) *left = plugin_initialized ? plugin_output_left : 0.0f;
    if (right) *right = plugin_initialized ? plugin_output_right : 0.0f;
}

void eps16_probe_machine_display(char display[23]) {
    if (!display) return;
    memcpy(display, plugin_published_display, 23);
}

uint32_t eps16_probe_machine_decimal_mask(void) {
    return plugin_published_decimal_mask & 0x3fffffU;
}

uint32_t eps16_probe_machine_cursor_segment_mask(void) {
    return plugin_published_cursor_mask & 0x3fffffU;
}

uint16_t eps16_probe_machine_indicator_on(unsigned int bank) {
    return bank < 3 ? panel_indicator_on[bank] : 0;
}

uint16_t eps16_probe_machine_indicator_flash(unsigned int bank) {
    return bank < 3 ? panel_indicator_flash[bank] : 0;
}

int eps16_probe_machine_sampling_monitor_active(void) {
    if (!plugin_initialized || !plugin_input_valid ||
        !es5510_input_last_poll_cycle)
        return 0;
    const uint64_t now = bus_cycle_now();
    return now >= es5510_input_last_poll_cycle &&
           now - es5510_input_last_poll_cycle <= 10000;
}

unsigned int eps16_probe_machine_master_volume(void) {
    return plugin_initialized ? analog_values[5] >> 6 : 0;
}

unsigned int eps16_probe_machine_analog_value(unsigned int channel) {
    return plugin_initialized && channel < 8 ? analog_values[channel] >> 6 : 0;
}

size_t eps16_probe_machine_panel_rx_consumed(void) {
    return plugin_initialized ? panel_rx_consumed : 0;
}

uint32_t eps16_probe_machine_last_keyon_frequency(void) {
    return plugin_initialized ? plugin_last_keyon_frequency : 0;
}

void eps16_probe_machine_cursor(int *start, int *end) {
    if (start) *start = plugin_published_cursor_start;
    if (end) *end = plugin_published_cursor_end;
}

uint64_t eps16_probe_machine_cycles(void) {
    return plugin_executed;
}

size_t eps16_probe_machine_illegal_instructions(void) {
    return illegal_instruction_count;
}

uint64_t eps16_probe_machine_sample_ram_write_bytes(void) {
    return sample_ram_write_bytes;
}

uint64_t eps16_probe_machine_sampling_input_conversions(void) {
    return es5510_input_valid;
}

void eps16_probe_machine_reset_audio_peaks(void) {
    memset(es5505_bus_peak, 0, sizeof(es5505_bus_peak));
    es5510_return_peak = 0;
    audio_peak = 0;
}

float eps16_probe_machine_es5505_bus_peak(unsigned int bus) {
    if (bus >= ES5505_STEREO_BUSES) return 0.0f;
    return (float)es5505_bus_peak[bus] / 524288.0f;
}

float eps16_probe_machine_es5510_return_peak(void) {
    return (float)es5510_return_peak / 32768.0f;
}

float eps16_probe_machine_output_peak(void) {
    return (float)audio_peak / 524288.0f;
}

size_t eps16_probe_machine_state_size(void) {
    if (!plugin_initialized) return 0;
#define SNAPSHOT_FIELD_SIZE(name) + sizeof(name)
    return sizeof(PluginSnapshotHeader) + sizeof(Es5505Core) +
           sizeof(KpcDevice) + m68k_context_size()
           PLUGIN_SNAPSHOT_FIELDS(SNAPSHOT_FIELD_SIZE)
           PLUGIN_SNAPSHOT_V2_FIELDS(SNAPSHOT_FIELD_SIZE)
           PLUGIN_SNAPSHOT_V3_FIELDS(SNAPSHOT_FIELD_SIZE)
           PLUGIN_SNAPSHOT_V4_FIELDS(SNAPSHOT_FIELD_SIZE)
           PLUGIN_SNAPSHOT_V5_FIELDS(SNAPSHOT_FIELD_SIZE)
           PLUGIN_SNAPSHOT_V6_FIELDS(SNAPSHOT_FIELD_SIZE);
#undef SNAPSHOT_FIELD_SIZE
}

int eps16_probe_machine_save_state(void *data, size_t size) {
    const size_t required = eps16_probe_machine_state_size();
    if (!required || !data || size != required) return 0;
    memset(data, 0, size);
    PluginSnapshotHeader *header = (PluginSnapshotHeader *)data;
    memcpy(header->magic, "EPS16ST\0", 8);
    header->version = 6;
    header->header_size = sizeof(*header);
    header->total_size = size;
    header->m68k_context_size = m68k_context_size();
    header->fdc_data_offset = -1;
    if (fdc_data) {
        const uintptr_t pointer = (uintptr_t)fdc_data;
        const uintptr_t beginning = (uintptr_t)disk_image;
        const uintptr_t end = beginning + sizeof(disk_image);
        if (pointer >= beginning && pointer <= end)
            header->fdc_data_offset = (int64_t)(pointer - beginning);
    }

    PluginSnapshotWriter writer = {
        (uint8_t *)data + sizeof(*header), (uint8_t *)data + size, 1
    };
#define SNAPSHOT_SAVE_FIELD(name) snapshot_write(&writer, &(name), sizeof(name));
    PLUGIN_SNAPSHOT_FIELDS(SNAPSHOT_SAVE_FIELD)
    PLUGIN_SNAPSHOT_V2_FIELDS(SNAPSHOT_SAVE_FIELD)
    PLUGIN_SNAPSHOT_V3_FIELDS(SNAPSHOT_SAVE_FIELD)
    PLUGIN_SNAPSHOT_V4_FIELDS(SNAPSHOT_SAVE_FIELD)
    PLUGIN_SNAPSHOT_V5_FIELDS(SNAPSHOT_SAVE_FIELD)
    PLUGIN_SNAPSHOT_V6_FIELDS(SNAPSHOT_SAVE_FIELD)
#undef SNAPSHOT_SAVE_FIELD

    Es5505Core saved_es5505 = es5505;
    saved_es5505.sample_reader = NULL;
    saved_es5505.sample_context = NULL;
    saved_es5505.port_reader = NULL;
    saved_es5505.port_context = NULL;
    snapshot_write(&writer, &saved_es5505, sizeof(saved_es5505));

    KpcDevice saved_kpc_device = kpc_device;
    memset(&saved_kpc_device.firmware, 0, sizeof(saved_kpc_device.firmware));
    saved_kpc_device.cpu.memory_context = NULL;
    saved_kpc_device.cpu.read8 = NULL;
    saved_kpc_device.cpu.write8 = NULL;
    snapshot_write(&writer, &saved_kpc_device, sizeof(saved_kpc_device));

    m68ki_cpu_core saved_cpu;
    m68k_get_context(&saved_cpu);
    saved_cpu.cyc_instruction = NULL;
    saved_cpu.cyc_exception = NULL;
    saved_cpu.int_ack_callback = NULL;
    saved_cpu.bkpt_ack_callback = NULL;
    saved_cpu.reset_instr_callback = NULL;
    saved_cpu.cmpild_instr_callback = NULL;
    saved_cpu.rte_instr_callback = NULL;
    saved_cpu.tas_instr_callback = NULL;
    saved_cpu.illg_instr_callback = NULL;
    saved_cpu.trap_instr_callback = NULL;
    saved_cpu.pc_changed_callback = NULL;
    saved_cpu.set_fc_callback = NULL;
    saved_cpu.instr_hook_callback = NULL;
    snapshot_write(&writer, &saved_cpu, sizeof(saved_cpu));
    if (!writer.valid || writer.current != writer.end) return 0;
    header->checksum = snapshot_checksum((const uint8_t *)data + sizeof(*header),
                                         size - sizeof(*header));
    return 1;
}

int eps16_probe_machine_load_state(const void *data, size_t size,
                                   char *error, size_t error_size) {
    if (!plugin_initialized) {
        plugin_error(error, error_size, "machine must be initialized before restore");
        return 0;
    }
    if (!data || size < sizeof(PluginSnapshotHeader)) {
        plugin_error(error, error_size, "machine snapshot is truncated");
        return 0;
    }
    PluginSnapshotHeader header;
    memcpy(&header, data, sizeof(header));
    const size_t expected_v6 = eps16_probe_machine_state_size();
#define SNAPSHOT_V6_FIELD_SIZE(name) - sizeof(name)
    const size_t expected_v5 = expected_v6
        PLUGIN_SNAPSHOT_V6_FIELDS(SNAPSHOT_V6_FIELD_SIZE);
#undef SNAPSHOT_V6_FIELD_SIZE
#define SNAPSHOT_V5_FIELD_SIZE(name) - sizeof(name)
    const size_t expected_v4 = expected_v5
        PLUGIN_SNAPSHOT_V5_FIELDS(SNAPSHOT_V5_FIELD_SIZE);
#undef SNAPSHOT_V5_FIELD_SIZE
#define SNAPSHOT_V4_FIELD_SIZE(name) - sizeof(name)
    const size_t expected_v3 = expected_v4
        PLUGIN_SNAPSHOT_V4_FIELDS(SNAPSHOT_V4_FIELD_SIZE);
#undef SNAPSHOT_V4_FIELD_SIZE
#define SNAPSHOT_V3_FIELD_SIZE(name) - sizeof(name)
    const size_t expected_v2 = expected_v3
        PLUGIN_SNAPSHOT_V3_FIELDS(SNAPSHOT_V3_FIELD_SIZE);
#undef SNAPSHOT_V3_FIELD_SIZE
#define SNAPSHOT_V2_FIELD_SIZE(name) - sizeof(name)
    const size_t expected_v1 = expected_v2
        PLUGIN_SNAPSHOT_V2_FIELDS(SNAPSHOT_V2_FIELD_SIZE);
#undef SNAPSHOT_V2_FIELD_SIZE
    const size_t expected = header.version == 1 ? expected_v1
                          : header.version == 2 ? expected_v2
                          : header.version == 3 ? expected_v3
                          : header.version == 4 ? expected_v4
                          : header.version == 5 ? expected_v5 : expected_v6;
    if (memcmp(header.magic, "EPS16ST\0", 8) ||
        (header.version < 1 || header.version > 6) ||
        header.header_size != sizeof(header) || header.total_size != size ||
        size != expected || header.m68k_context_size != m68k_context_size()) {
        plugin_error(error, error_size, "machine snapshot format is incompatible");
        return 0;
    }
    const uint8_t *payload = (const uint8_t *)data + sizeof(header);
    if (header.checksum != snapshot_checksum(payload, size - sizeof(header))) {
        plugin_error(error, error_size, "machine snapshot checksum failed");
        return 0;
    }
    if (header.fdc_data_offset < -1 ||
        header.fdc_data_offset > (int64_t)sizeof(disk_image)) {
        plugin_error(error, error_size, "machine snapshot has invalid disk position");
        return 0;
    }

    const Es5505SampleReader sample_reader = es5505.sample_reader;
    void *const sample_context = es5505.sample_context;
    const Es5505PortReader port_reader = es5505.port_reader;
    void *const port_context = es5505.port_context;
    const KpcFirmware device_firmware = kpc_device.firmware;
    const double configured_input_rate = sampling_input_circuit.sample_rate;
    void *const kpc_memory_context = kpc_device.cpu.memory_context;
    const M68hc11Read8 kpc_read8 = kpc_device.cpu.read8;
    const M68hc11Write8 kpc_write8 = kpc_device.cpu.write8;

    PluginSnapshotReader reader = {payload, (const uint8_t *)data + size, 1};
#define SNAPSHOT_LOAD_FIELD(name) snapshot_read(&reader, &(name), sizeof(name));
    PLUGIN_SNAPSHOT_FIELDS(SNAPSHOT_LOAD_FIELD)
    if (header.version >= 2) {
        PLUGIN_SNAPSHOT_V2_FIELDS(SNAPSHOT_LOAD_FIELD)
    } else {
        memset(panel_indicator_on, 0, sizeof(panel_indicator_on));
        memset(panel_indicator_flash, 0, sizeof(panel_indicator_flash));
        panel_indicator_command_pending = 0;
    }
    if (header.version >= 3) {
        PLUGIN_SNAPSHOT_V3_FIELDS(SNAPSHOT_LOAD_FIELD)
        if (configured_input_rate >= 8000.0 &&
            fabs(sampling_input_circuit.sample_rate - configured_input_rate) >
                0.5)
            sampling_input_circuit_set_rate(&sampling_input_circuit,
                                            configured_input_rate);
    } else {
        sampling_input_circuit_init(&sampling_input_circuit,
            configured_input_rate >= 8000.0 ? configured_input_rate : 48000.0);
    }
    if (header.version >= 4) {
        PLUGIN_SNAPSHOT_V4_FIELDS(SNAPSHOT_LOAD_FIELD)
    } else {
        panel_cursor_segment_mask = 0;
        panel_cursor_full_segments = 0;
    }
    if (header.version >= 5) {
        PLUGIN_SNAPSHOT_V5_FIELDS(SNAPSHOT_LOAD_FIELD)
    } else {
        panel_threshold_position = -1;
        panel_level_active = 0;
        panel_level_setup_state = 0;
        panel_level_pending = 0;
        panel_level_pending_value = 0;
        panel_level_value = 0;
    }
    if (header.version >= 6) {
        PLUGIN_SNAPSHOT_V6_FIELDS(SNAPSHOT_LOAD_FIELD)
    } else {
        panel_cell_address_pending = 0;
        panel_cell_address = 0;
        panel_edit_cursor = -1;
        panel_cursor_command_state = 0;
        panel_cursor_positioned = 0;
        panel_text_mode_pending = 0;
    }
#undef SNAPSHOT_LOAD_FIELD
    snapshot_read(&reader, &es5505, sizeof(es5505));
    snapshot_read(&reader, &kpc_device, sizeof(kpc_device));
    if (!reader.valid ||
        (size_t)(reader.end - reader.current) != header.m68k_context_size) {
        plugin_error(error, error_size, "machine snapshot payload is invalid");
        return 0;
    }
    m68ki_cpu_core current_cpu;
    m68ki_cpu_core restored_cpu;
    m68k_get_context(&current_cpu);
    snapshot_read(&reader, &restored_cpu, sizeof(restored_cpu));
    restored_cpu.cyc_instruction = current_cpu.cyc_instruction;
    restored_cpu.cyc_exception = current_cpu.cyc_exception;
    restored_cpu.int_ack_callback = current_cpu.int_ack_callback;
    restored_cpu.bkpt_ack_callback = current_cpu.bkpt_ack_callback;
    restored_cpu.reset_instr_callback = current_cpu.reset_instr_callback;
    restored_cpu.cmpild_instr_callback = current_cpu.cmpild_instr_callback;
    restored_cpu.rte_instr_callback = current_cpu.rte_instr_callback;
    restored_cpu.tas_instr_callback = current_cpu.tas_instr_callback;
    restored_cpu.illg_instr_callback = current_cpu.illg_instr_callback;
    restored_cpu.trap_instr_callback = current_cpu.trap_instr_callback;
    restored_cpu.pc_changed_callback = current_cpu.pc_changed_callback;
    restored_cpu.set_fc_callback = current_cpu.set_fc_callback;
    restored_cpu.instr_hook_callback = current_cpu.instr_hook_callback;
    m68k_set_context(&restored_cpu);

    /* Version 4 stores the completed published VFD state independently of
       transient parser state.  Older snapshots derive it from their decoder
       fields and safely restore the previously unknown segment mask as off. */
    if (header.version < 4) {
        memcpy(plugin_published_display, panel_display, 23);
        plugin_published_decimal_mask = panel_display_decimal_mask;
        plugin_published_cursor_mask = 0;
        plugin_published_cursor_start = panel_cursor_start;
        plugin_published_cursor_end = panel_cursor_end;
    }

    es5505.sample_reader = sample_reader;
    es5505.sample_context = sample_context;
    es5505.port_reader = port_reader;
    es5505.port_context = port_context;
    kpc_device.firmware = device_firmware;
    kpc_device.cpu.memory_context = kpc_memory_context;
    kpc_device.cpu.read8 = kpc_read8;
    kpc_device.cpu.write8 = kpc_write8;
    fdc_data = header.fdc_data_offset >= 0
                   ? disk_image + header.fdc_data_offset : NULL;
    plugin_audio_queue_read = 0;
    plugin_audio_queue_write = 0;
    plugin_last_keyon_frequency = 0;
    midi_rx_read = midi_rx_write = midi_rx_count = 0;
    midi_wire_read = midi_wire_write = midi_wire_count = 0;
    midi_wire_tail_cycle = 0;
    plugin_restored_voice_mask = 0;
    for (unsigned int voice = 0; voice < ES5505_VOICES; ++voice)
        if (!(es5505.voices[voice].control & ES5505_STOP_MASK))
            plugin_restored_voice_mask |= UINT32_C(1) << voice;
    plugin_restored_quiet_frames = 0;
    plugin_restored_output_suppressed =
        plugin_restored_voice_mask != 0;
    /* Host MIDI keys are physical inputs, not durable machine state.  A
       snapshot taken while a host key was held nevertheless contains the
       resulting OS key state and active ES5505 voice.  Reconcile every
       restored snapshot, including versions 1-4, with the neutral external
       keyboard that exists at preset recall.  Use the same KPC byte path as
       ordinary host Note Off rather than stopping voices or editing OS RAM. */
    for (unsigned int note = 36; note <= 96; ++note)
        live_note(note, 1, 0);
    if (error && error_size) error[0] = '\0';
    return reader.current == reader.end;
}

/* live_host replacements used by the included probe.  No host services,
   sockets, CoreMIDI or AudioQueue enter the plug-in. */
int live_host_start(uint32_t sample_rate) { (void)sample_rate; return 1; }
void live_host_stop(void) {}
void live_host_write(const int16_t *samples, size_t frames) {
    (void)samples; (void)frames;
}
int live_host_poll_line(char *line, size_t size) {
    (void)line; (void)size; return 0;
}
int live_host_poll_midi(LiveMidiEvent *event) { (void)event; return 0; }
int live_host_audio_input_sample(uint32_t target_rate, int16_t *sample) {
    (void)target_rate;
    if (!plugin_input_valid || !sample) return 0;
    *sample = plugin_input_sample;
    return 1;
}
void live_host_audio_input_prepare_recording(void) {}
void live_host_clear_display_hold(void) {}
void live_host_display(const char display[23], uint32_t decimal_mask,
                       int cursor_start, int cursor_end) {
    (void)display; (void)decimal_mask; (void)cursor_start; (void)cursor_end;
}
void live_host_panel_tx(uint8_t value) { (void)value; }
void live_host_adc_state(const uint16_t values[8], const unsigned int reads[8],
                         unsigned int duart_opr, unsigned int es5505_page) {
    (void)values; (void)reads; (void)duart_opr; (void)es5505_page;
}
