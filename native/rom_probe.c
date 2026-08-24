#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "es5505_core.h"
#include "es5510_core.h"
#include "hfe_disk.h"
#include "kpc_device.h"
#include "kpc_firmware.h"
#include "kpc_legacy.h"
#include "sampling_input_circuit.h"
#include "live_host.h"
#include "m68k.h"

enum {
    LOW_RAM_SIZE = 32 * 1024,
    SAMPLE_RAM_BASE = 0x580000,
    SAMPLE_RAM_SIZE = 0x280000,
    OS_RAM_BASE = 0xFF0000,
    OS_RAM_SIZE = 64 * 1024,
    ROM_BASE = 0xC00000,
    ROM_SIZE = 128 * 1024,
    ES5505_BASE = 0x200000,
    ES5505_SIZE = 0x20,
    ES5510_BASE = 0x260000,
    ES5510_SIZE = 0x200,
    DUART_BASE = 0x280000,
    DUART_SIZE = 0x20,
    FDC_BASE = 0x2C0000,
    FDC_SIZE = 8,
    SCSI_BASE = 0x300000,
    SCSI_SIZE = 4,
    DISK_SIZE = 80 * 2 * 10 * 512,
    MAX_TRACES = 256,
    PANEL_RX_SIZE = 256,
    PANEL_EVENT_SIZE = 256,
    PANEL_SCRIPT_EVENTS = 32,
    PANEL_TRACE_SIZE = 2048,
    PANEL_WIRE_SIZE = 4096,
    CPU_CLOCK_RATE = 10000000,
    DUART_CLOCK_RATE = 5000000,
    PANEL_BYTE_CYCLES = 1760,
    MIDI_BYTE_CYCLES = 3200
};

typedef struct {
    long long cycle;
    uint8_t bytes[PANEL_EVENT_SIZE];
    size_t length;
    int injected;
} PanelScriptEvent;

typedef struct {
    uint32_t address;
    uint32_t value;
    uint32_t count;
    uint8_t width;
    uint8_t write;
} Trace;

typedef struct {
    long long cycle;
    uint32_t pc;
    uint8_t value;
    uint8_t direction;
} PanelTrace;

typedef struct { uint64_t cycle; uint8_t value; } PanelWireByte;
typedef struct { uint64_t cycle; uint8_t value; } MidiWireByte;

typedef struct {
    long long cycle;
    uint32_t pc;
    uint8_t kind;
    uint8_t channel;
    uint8_t reg;
    uint8_t value;
} DmaTrace;

typedef struct {
    uint8_t command;
    uint8_t track;
    uint8_t sector;
    uint8_t side;
    unsigned int block;
    uint32_t destination;
} FdcEvent;

typedef struct { long long cycle; uint32_t pc; uint8_t level; uint8_t vector; } IrqTrace;

typedef struct {
    long long cycle;
    uint32_t pc;
    uint16_t opcode;
} IllegalInstruction;

enum { KPC_PHYSICAL_QUEUE_SIZE = 256 };

/* All mutable probe state lives behind one explicit context.  The standalone
   probe uses the default context below; the VST wrapper selects one context
   per plug-in instance before entering the core. */
typedef struct {
    uint8_t low_ram[LOW_RAM_SIZE];
    uint8_t sample_ram[SAMPLE_RAM_SIZE];
    uint64_t sample_ram_write_bytes;
    uint8_t os_ram[OS_RAM_SIZE];
    uint8_t rom[ROM_SIZE];
    Es5505Core es5505;
    uint64_t es5505_writes;
    Es5510Core es5510;
    uint32_t es5510_gpr_latch;
    uint64_t es5510_instruction_latch;
    uint32_t es5510_dil_latch, es5510_dol_latch, es5510_dadr_latch;
    uint8_t es5510_ram_read;
    unsigned int es5510_dram_reads, es5510_dram_writes, es5510_gpr_writes;
    unsigned int es5510_instruction_writes;
    uint8_t es5510_host_serial;
    unsigned int es5510_host_serial_writes;
    int es5510_host_upload_active;
    uint64_t es5510_host_access_until;
    int live_mode, deterministic_host_input;
    uint64_t es5510_input_next_cycle, es5510_input_next_time_ns;
    uint64_t es5510_input_last_poll_time_ns, es5510_input_last_poll_cycle;
    uint64_t es5510_input_polls, es5510_input_valid, es5510_input_bypass;
    uint64_t sample_record_input_valid_start, sample_record_write_start;
    SamplingInputCircuit sampling_input_circuit;
    unsigned int function_code;
    uint8_t duart_registers[16];
    uint8_t panel_rx[PANEL_RX_SIZE];
    size_t panel_rx_read, panel_rx_write, panel_rx_count, panel_rx_consumed;
    PanelWireByte panel_wire[PANEL_WIRE_SIZE];
    size_t panel_wire_read, panel_wire_write, panel_wire_count;
    uint64_t panel_wire_tail_cycle;
    uint8_t panel_tx[1024];
    size_t panel_tx_count;
    PanelTrace panel_trace[PANEL_TRACE_SIZE];
    size_t panel_trace_count;
    long long current_cycle;
    char panel_display[23];
    uint32_t panel_display_decimal_mask;
    int panel_display_dirty;
    long long panel_display_last_change_cycle;
    size_t panel_cursor;
    int panel_cursor_start, panel_cursor_end, panel_cursor_active;
    int panel_cursor_width_pending, panel_cursor_width_known;
    uint32_t panel_cursor_segment_mask;
    int panel_cursor_full_segments;
    int panel_noncell_parameter_pending;
    uint8_t panel_cell_address_pending, panel_cell_address;
    int panel_edit_cursor;
    uint8_t panel_cursor_command_state, panel_cursor_positioned;
    uint8_t panel_text_mode_pending;
    int panel_threshold_position;
    uint8_t panel_level_active, panel_level_setup_state;
    uint8_t panel_level_pending, panel_level_pending_value;
    uint8_t panel_level_value;
    uint16_t panel_indicator_on[3], panel_indicator_flash[3];
    uint8_t panel_indicator_command_pending, panel_last_tx;
    int panel_pick_instrument_seen, panel_file_loaded_seen;
    uint8_t disk_image[DISK_SIZE];
    uint8_t disk_sector_status[EPS16_LOGICAL_SECTOR_COUNT];
    int disk_loaded, disk_change_pending;
    uint8_t fdc_track, fdc_physical_track, fdc_sector, fdc_data_register;
    uint8_t fdc_last_command;
    uint8_t fdc_error_status;
    int load_trace_enabled, fdc_step_direction;
    uint8_t *fdc_data;
    size_t fdc_remaining;
    unsigned int fdc_data_reads;
    uint32_t fdc_read_pcs[16];
    size_t fdc_read_pc_count;
    unsigned int duart_output;
    int duart_tx_a_enabled, duart_tx_a_ready;
    uint64_t duart_tx_a_ready_cycle;
    int duart_tx_b_enabled, duart_tx_b_ready;
    uint64_t duart_tx_b_ready_cycle;
    uint8_t midi_tx[256];
    size_t midi_tx_count;
    uint8_t midi_rx[256];
    size_t midi_rx_read, midi_rx_write, midi_rx_count, midi_rx_consumed;
    MidiWireByte midi_wire[256];
    size_t midi_wire_read, midi_wire_write, midi_wire_count;
    uint64_t midi_wire_tail_cycle;
    int duart_rx_a_enabled;
    uint16_t analog_values[8];
    unsigned int analog_reads[8];
    int duart_timer_pending, duart_timer_running;
    uint64_t duart_timer_next_cycle;
    unsigned int fdc_reads;
    uint8_t dmac_registers[4][0x40];
    int dmac_irq_channel;
    unsigned int dmac_transfers;
    int dmac_pcl_level[4];
    uint8_t scsi_registers[32];
    uint8_t scsi_address;
    uint8_t scsi_aux_status;
    uint8_t scsi_buffer[256];
    size_t scsi_buffer_position;
    size_t scsi_buffer_size;
    FILE *scsi_image;
    uint64_t scsi_image_size;
    uint64_t scsi_stream_remaining;
    uint8_t scsi_dma_padding_remaining;
    DmaTrace dma_trace[256];
    size_t dma_trace_count;
    FdcEvent fdc_events[256];
    size_t fdc_event_count;
    Trace traces[MAX_TRACES];
    size_t trace_count;
    uint32_t postboot_pc_counts[ROM_SIZE / 2];
    uint32_t postboot_os_pc_counts[OS_RAM_SIZE / 2];
    uint64_t postboot_instruction_count, postboot_rom_instructions;
    uint64_t postboot_low_ram_instructions, postboot_sample_ram_instructions;
    uint64_t postboot_os_ram_instructions, postboot_other_instructions;
    uint32_t pc_ring[64];
    size_t pc_ring_position;
    uint32_t fatal_history[64];
    int fatal_history_captured;
    uint32_t es5510_verify_d1, es5510_verify_d2, es5510_verify_d3;
    uint32_t es5510_verify_a3, es5510_verify_a6;
    uint8_t es5510_verify_actual;
    unsigned int es5510_verify_failures;
    uint64_t audio_frames;
    int32_t audio_peak, es5505_bus_peak[ES5505_STEREO_BUSES];
    int32_t es5510_return_peak;
    unsigned int es5505_irqs;
    IrqTrace irq_trace[128];
    size_t irq_trace_count;
    KpcLegacy kpc;
    KpcFirmware kpc_firmware;
    KpcDevice kpc_device;
    int kpc_firmware_execution, kpc_firmware_failure_reported;
    uint8_t kpc_physical_queue[KPC_PHYSICAL_QUEUE_SIZE];
    size_t kpc_physical_queue_read, kpc_physical_queue_write;
    size_t kpc_physical_queue_count;
    uint8_t kpc_physical_expected;
    int kpc_physical_active, kpc_physical_saw_code;
    uint64_t kpc_physical_next_cycle;
    uint8_t display_trace_bytes[256];
    size_t display_trace_byte_count;
    IllegalInstruction illegal_instructions[16];
    size_t illegal_instruction_count;
    int live_quit;
    uint64_t live_press_cycle[128];
    int sampling_enter_pending;
    uint64_t sampling_enter_release_cycle;
    int sampling_recording_active;
    uint64_t button_dispatch_trace_until;
    unsigned int button_dispatch_trace_lines;
    uint32_t sample_live_current_pc;
    unsigned int sample_live_bus_trace_lines;
    FILE *sample_live_trace;
    PanelWireByte kept_wire[PANEL_WIRE_SIZE];
    uint8_t kept_rx[PANEL_RX_SIZE];
    unsigned int es5505_port_trace_count;
    int es5505_trace_writes;
    long long es5505_trace_start_cycle;
} RomProbeState;

#ifdef EPS16_ROM_PROBE_CONTEXT
static _Thread_local RomProbeState *rom_probe_state;
#else
static RomProbeState rom_probe_default_state;
static RomProbeState *rom_probe_state = &rom_probe_default_state;
#endif

static void rom_probe_state_defaults(RomProbeState *state) {
    memset(state, 0, sizeof(*state));
    memset(state->panel_display, ' ', 22);
    state->panel_display[22] = '\0';
    state->panel_cursor_start = -1;
    state->panel_cursor_end = -1;
    state->panel_edit_cursor = -1;
    state->panel_threshold_position = -1;
    state->fdc_step_direction = 1;
    state->duart_tx_a_ready = 1;
    state->duart_tx_b_ready = 1;
    state->analog_values[0] = 0x7fc0;
    state->analog_values[1] = 0x0000;
    state->analog_values[2] = 0xffc0;
    state->analog_values[3] = 0x5980;
    state->analog_values[4] = 0xffc0;
    state->analog_values[5] = 0xffc0;
    state->analog_values[6] = 0x7fc0;
    state->analog_values[7] = 0x5540;
    sampling_input_circuit_init(&state->sampling_input_circuit, 48000.0);
    state->dmac_irq_channel = -1;
    for (unsigned int channel = 0; channel < 4; ++channel)
        state->dmac_pcl_level[channel] = 1;
    /* The SP-2 asserts the channel-1 PCL transition used by the boot ROM as
       its installed-card indication.  A target medium may be mounted later. */
    state->dmac_registers[1][0] = 0x02;
    state->es5505_trace_writes = -1;
}

#define RP(name) (rom_probe_state->name)
#define low_ram RP(low_ram)
#define sample_ram RP(sample_ram)
#define sample_ram_write_bytes RP(sample_ram_write_bytes)
#define os_ram RP(os_ram)
#define rom RP(rom)
#define es5505 RP(es5505)
#define es5505_writes RP(es5505_writes)
#define es5510 RP(es5510)
#define es5510_gpr_latch RP(es5510_gpr_latch)
#define es5510_instruction_latch RP(es5510_instruction_latch)
#define es5510_dil_latch RP(es5510_dil_latch)
#define es5510_dol_latch RP(es5510_dol_latch)
#define es5510_dadr_latch RP(es5510_dadr_latch)
#define es5510_ram_read RP(es5510_ram_read)
#define es5510_dram_reads RP(es5510_dram_reads)
#define es5510_dram_writes RP(es5510_dram_writes)
#define es5510_gpr_writes RP(es5510_gpr_writes)
#define es5510_instruction_writes RP(es5510_instruction_writes)
#define es5510_host_serial RP(es5510_host_serial)
#define es5510_host_serial_writes RP(es5510_host_serial_writes)
#define es5510_host_upload_active RP(es5510_host_upload_active)
#define es5510_host_access_until RP(es5510_host_access_until)
#define live_mode RP(live_mode)
#define deterministic_host_input RP(deterministic_host_input)
#define es5510_input_next_cycle RP(es5510_input_next_cycle)
#define es5510_input_next_time_ns RP(es5510_input_next_time_ns)
#define es5510_input_last_poll_time_ns RP(es5510_input_last_poll_time_ns)
#define es5510_input_last_poll_cycle RP(es5510_input_last_poll_cycle)
#define es5510_input_polls RP(es5510_input_polls)
#define es5510_input_valid RP(es5510_input_valid)
#define es5510_input_bypass RP(es5510_input_bypass)
#define sample_record_input_valid_start RP(sample_record_input_valid_start)
#define sample_record_write_start RP(sample_record_write_start)
#define sampling_input_circuit RP(sampling_input_circuit)
#define function_code RP(function_code)
#define duart_registers RP(duart_registers)
#define panel_rx RP(panel_rx)
#define panel_rx_read RP(panel_rx_read)
#define panel_rx_write RP(panel_rx_write)
#define panel_rx_count RP(panel_rx_count)
#define panel_rx_consumed RP(panel_rx_consumed)
#define panel_wire RP(panel_wire)
#define panel_wire_read RP(panel_wire_read)
#define panel_wire_write RP(panel_wire_write)
#define panel_wire_count RP(panel_wire_count)
#define panel_wire_tail_cycle RP(panel_wire_tail_cycle)
#define panel_tx RP(panel_tx)
#define panel_tx_count RP(panel_tx_count)
#define panel_trace RP(panel_trace)
#define panel_trace_count RP(panel_trace_count)
#define current_cycle RP(current_cycle)
#define panel_display RP(panel_display)
#define panel_display_decimal_mask RP(panel_display_decimal_mask)
#define panel_display_dirty RP(panel_display_dirty)
#define panel_display_last_change_cycle RP(panel_display_last_change_cycle)
#define panel_cursor RP(panel_cursor)
#define panel_cursor_start RP(panel_cursor_start)
#define panel_cursor_end RP(panel_cursor_end)
#define panel_cursor_active RP(panel_cursor_active)
#define panel_cursor_width_pending RP(panel_cursor_width_pending)
#define panel_cursor_width_known RP(panel_cursor_width_known)
#define panel_cursor_segment_mask RP(panel_cursor_segment_mask)
#define panel_cursor_full_segments RP(panel_cursor_full_segments)
#define panel_noncell_parameter_pending RP(panel_noncell_parameter_pending)
#define panel_cell_address_pending RP(panel_cell_address_pending)
#define panel_cell_address RP(panel_cell_address)
#define panel_edit_cursor RP(panel_edit_cursor)
#define panel_cursor_command_state RP(panel_cursor_command_state)
#define panel_cursor_positioned RP(panel_cursor_positioned)
#define panel_text_mode_pending RP(panel_text_mode_pending)
#define panel_threshold_position RP(panel_threshold_position)
#define panel_level_active RP(panel_level_active)
#define panel_level_setup_state RP(panel_level_setup_state)
#define panel_level_pending RP(panel_level_pending)
#define panel_level_pending_value RP(panel_level_pending_value)
#define panel_level_value RP(panel_level_value)
#define panel_indicator_on RP(panel_indicator_on)
#define panel_indicator_flash RP(panel_indicator_flash)
#define panel_indicator_command_pending RP(panel_indicator_command_pending)
#define panel_last_tx RP(panel_last_tx)
#define panel_pick_instrument_seen RP(panel_pick_instrument_seen)
#define panel_file_loaded_seen RP(panel_file_loaded_seen)
#define disk_image RP(disk_image)
#define disk_sector_status RP(disk_sector_status)
#define disk_loaded RP(disk_loaded)
#define disk_change_pending RP(disk_change_pending)
#define fdc_track RP(fdc_track)
#define fdc_physical_track RP(fdc_physical_track)
#define fdc_sector RP(fdc_sector)
#define fdc_data_register RP(fdc_data_register)
#define fdc_last_command RP(fdc_last_command)
#define fdc_error_status RP(fdc_error_status)
#define load_trace_enabled RP(load_trace_enabled)
#define fdc_step_direction RP(fdc_step_direction)
#define fdc_data RP(fdc_data)
#define fdc_remaining RP(fdc_remaining)
#define fdc_data_reads RP(fdc_data_reads)
#define fdc_read_pcs RP(fdc_read_pcs)
#define fdc_read_pc_count RP(fdc_read_pc_count)
#define duart_output RP(duart_output)
#define duart_tx_a_enabled RP(duart_tx_a_enabled)
#define duart_tx_a_ready RP(duart_tx_a_ready)
#define duart_tx_a_ready_cycle RP(duart_tx_a_ready_cycle)
#define duart_tx_b_enabled RP(duart_tx_b_enabled)
#define duart_tx_b_ready RP(duart_tx_b_ready)
#define duart_tx_b_ready_cycle RP(duart_tx_b_ready_cycle)
#define midi_tx RP(midi_tx)
#define midi_tx_count RP(midi_tx_count)
#define midi_rx RP(midi_rx)
#define midi_rx_read RP(midi_rx_read)
#define midi_rx_write RP(midi_rx_write)
#define midi_rx_count RP(midi_rx_count)
#define midi_rx_consumed RP(midi_rx_consumed)
#define midi_wire RP(midi_wire)
#define midi_wire_read RP(midi_wire_read)
#define midi_wire_write RP(midi_wire_write)
#define midi_wire_count RP(midi_wire_count)
#define midi_wire_tail_cycle RP(midi_wire_tail_cycle)
#define duart_rx_a_enabled RP(duart_rx_a_enabled)
#define analog_values RP(analog_values)
#define analog_reads RP(analog_reads)
#define duart_timer_pending RP(duart_timer_pending)
#define duart_timer_running RP(duart_timer_running)
#define duart_timer_next_cycle RP(duart_timer_next_cycle)
#define fdc_reads RP(fdc_reads)
#define dmac_registers RP(dmac_registers)
#define dmac_irq_channel RP(dmac_irq_channel)
#define dmac_transfers RP(dmac_transfers)
#define dmac_pcl_level RP(dmac_pcl_level)
#define scsi_registers RP(scsi_registers)
#define scsi_address RP(scsi_address)
#define scsi_aux_status RP(scsi_aux_status)
#define scsi_buffer RP(scsi_buffer)
#define scsi_buffer_position RP(scsi_buffer_position)
#define scsi_buffer_size RP(scsi_buffer_size)
#define scsi_image RP(scsi_image)
#define scsi_image_size RP(scsi_image_size)
#define scsi_stream_remaining RP(scsi_stream_remaining)
#define scsi_dma_padding_remaining RP(scsi_dma_padding_remaining)
#define dma_trace RP(dma_trace)
#define dma_trace_count RP(dma_trace_count)
#define fdc_events RP(fdc_events)
#define fdc_event_count RP(fdc_event_count)
#define traces RP(traces)
#define trace_count RP(trace_count)
#define postboot_pc_counts RP(postboot_pc_counts)
#define postboot_os_pc_counts RP(postboot_os_pc_counts)
#define postboot_instruction_count RP(postboot_instruction_count)
#define postboot_rom_instructions RP(postboot_rom_instructions)
#define postboot_low_ram_instructions RP(postboot_low_ram_instructions)
#define postboot_sample_ram_instructions RP(postboot_sample_ram_instructions)
#define postboot_os_ram_instructions RP(postboot_os_ram_instructions)
#define postboot_other_instructions RP(postboot_other_instructions)
#define pc_ring RP(pc_ring)
#define pc_ring_position RP(pc_ring_position)
#define fatal_history RP(fatal_history)
#define fatal_history_captured RP(fatal_history_captured)
#define es5510_verify_d1 RP(es5510_verify_d1)
#define es5510_verify_d2 RP(es5510_verify_d2)
#define es5510_verify_d3 RP(es5510_verify_d3)
#define es5510_verify_a3 RP(es5510_verify_a3)
#define es5510_verify_a6 RP(es5510_verify_a6)
#define es5510_verify_actual RP(es5510_verify_actual)
#define es5510_verify_failures RP(es5510_verify_failures)
#define audio_frames RP(audio_frames)
#define audio_peak RP(audio_peak)
#define es5505_bus_peak RP(es5505_bus_peak)
#define es5510_return_peak RP(es5510_return_peak)
#define es5505_irqs RP(es5505_irqs)
#define irq_trace RP(irq_trace)
#define irq_trace_count RP(irq_trace_count)
#define kpc RP(kpc)
#define kpc_firmware RP(kpc_firmware)
#define kpc_device RP(kpc_device)
#define kpc_firmware_execution RP(kpc_firmware_execution)
#define kpc_firmware_failure_reported RP(kpc_firmware_failure_reported)
#define kpc_physical_queue RP(kpc_physical_queue)
#define kpc_physical_queue_read RP(kpc_physical_queue_read)
#define kpc_physical_queue_write RP(kpc_physical_queue_write)
#define kpc_physical_queue_count RP(kpc_physical_queue_count)
#define kpc_physical_expected RP(kpc_physical_expected)
#define kpc_physical_active RP(kpc_physical_active)
#define kpc_physical_saw_code RP(kpc_physical_saw_code)
#define kpc_physical_next_cycle RP(kpc_physical_next_cycle)
#define display_trace_bytes RP(display_trace_bytes)
#define display_trace_byte_count RP(display_trace_byte_count)
#define illegal_instructions RP(illegal_instructions)
#define illegal_instruction_count RP(illegal_instruction_count)
#define live_quit RP(live_quit)
#define live_press_cycle RP(live_press_cycle)
#define sampling_enter_pending RP(sampling_enter_pending)
#define sampling_enter_release_cycle RP(sampling_enter_release_cycle)
#define sampling_recording_active RP(sampling_recording_active)
#define button_dispatch_trace_until RP(button_dispatch_trace_until)
#define button_dispatch_trace_lines RP(button_dispatch_trace_lines)
#define sample_live_current_pc RP(sample_live_current_pc)
#define sample_live_bus_trace_lines RP(sample_live_bus_trace_lines)
#define sample_live_trace RP(sample_live_trace)

#define es5510_gpr es5510.gpr
#define es5510_instruction es5510.instruction
#define es5510_dram es5510.dram
#define es5510_dlength es5510.dlength
#define es5510_abase es5510.abase
#define es5510_bbase es5510.bbase
#define es5510_dbase es5510.dbase
#define es5510_sigreg es5510.sigreg
#define es5510_ccr es5510.ccr
#define es5510_cmr es5510.cmr
/* A plug-in host supplies input from the DAW callback while retaining the
   offline probe's emulated-cycle ADC pacing.  The normal CLI/live modes leave
   this disabled. */
static uint64_t bus_cycle_now(void);

static uint64_t monotonic_time_ns(void) {
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000000000ULL + (uint64_t)value.tv_nsec;
}

static uint32_t es5510_sampling_input(void) {
    ++es5510_input_polls;
    es5510_input_last_poll_cycle = bus_cycle_now();
    /* The original sampling overlay programs GPR 81 with selectors 8..2.
       They select the board's seven 625 kHz conversion divisors in steps of
       seven: 56,49,42,35,28,21,14.  At a 10 MHz CPU clock one conversion is
       therefore exactly 16 * divider CPU cycles. */
    unsigned int selector = es5510_gpr[0x81] & 0xff;
    if (selector < 2 || selector > 8) selector = 3; /* normal 29.8 kHz default */
    unsigned int divider = selector * 7;
    uint32_t target_rate = 625000U / divider;
    uint64_t conversion_cycle = bus_cycle_now();
    if (live_mode && !deterministic_host_input) {
        /* A live ADC is clocked by the physical sampling oscillator, not by
           however quickly the host happens to execute 68000 instructions.
           Keep an absolute oscillator phase. */
        uint64_t now = monotonic_time_ns();
        uint64_t period_ns = (uint64_t)divider * 1600ULL;
        if (!es5510_input_next_time_ns) es5510_input_next_time_ns = now;
        /* The converter may be selected briefly during boot diagnostics and
           then left unread for seconds.  Real hardware retains only the most
           recent conversion, not an unbounded FIFO of every elapsed tick.
           Detect that from actual OS polling inactivity, not from oscillator
           lag: DAC output paces the CPU in roughly 34 ms buffered bursts, and
           treating a normal burst as stale after 10 ms discarded most ADC
           conversions and shortened live recordings. */
        if (es5510_input_last_poll_time_ns &&
            now > es5510_input_last_poll_time_ns + 250000000ULL)
            es5510_input_next_time_ns = now;
        es5510_input_last_poll_time_ns = now;
        if (now < es5510_input_next_time_ns) return 0;
        es5510_input_next_time_ns += period_ns;
    } else {
        uint64_t cycle = bus_cycle_now();
        const uint64_t period = (uint64_t)divider * 16;
        if (!es5510_input_next_cycle) es5510_input_next_cycle = cycle;
        if (cycle < es5510_input_next_cycle) return 0;
        /* The board ADC has its own oscillator. CPU polling may observe a
           completed conversion a few cycles late, but that latency must not
           move every following conversion by the same amount. Keep the
           oscillator phase and discard any conversions missed between polls,
           just as the single hardware latch retains only its newest value. */
        const uint64_t late = cycle - es5510_input_next_cycle;
        /* The ADC latch contains the newest conversion completed before the
           CPU poll. Reconstruct the input at that oscillator edge, not at the
           later and instruction-dependent poll cycle. Sampling at the poll
           introduced deterministic phase jitter and strong non-harmonic
           sidebands, especially at the 29.76 kHz selection. */
        conversion_cycle = es5510_input_next_cycle + (late / period) * period;
        es5510_input_next_cycle = conversion_cycle + period;
    }
    int16_t input = 0;
    if ((live_mode || deterministic_host_input) &&
        !live_host_audio_input_sample(target_rate, conversion_cycle, &input))
        return 0;
    if (live_mode && !deterministic_host_input) {
        if (fabs(sampling_input_circuit.sample_rate - target_rate) > 0.5)
            sampling_input_circuit_set_rate(&sampling_input_circuit,
                                            (double)target_rate);
        const float normalized = (float)input / 32768.0f;
        const float filtered = sampling_input_circuit_process(
            &sampling_input_circuit, normalized, low_ram[0x0211] == 0);
        input = (int16_t)lrintf(filtered * 32767.0f);
    }
    /* The mono ADC enters ES5510 serial input 0. The original sampling
       overlay loads its independently selectable cutoff table into GPR
       3b..69 and writes the filtered 24-bit result to GPR 80. Execute that
       uploaded program for each completed conversion instead of returning
       the unfiltered host sample. The host-visible low byte is the ADC-ready
       indication, not the filter result's fractional byte. */
    if (!es5510.halted && !es5510_host_upload_active &&
        selector >= 2 && selector <= 8) {
        const int16_t inputs[8] = {input, 0, 0, 0, 0, 0, 0, 0};
        int16_t outputs[2];
        es5510_core_process(&es5510, inputs, outputs);
        ++es5510_input_valid;
        return (es5510_gpr[0x80] & 0xffff00) | 0x01;
    }
    ++es5510_input_valid;
    return ((uint32_t)(uint16_t)input << 8) | 0x01;
}

static uint32_t es5510_read_register(uint8_t index) {
    if (index < 0xc0) {
        uint32_t value = es5510_gpr[index] & 0xffffff;
        /* The sampling overlay reads the board ADC through host-selected GPR
           80.  Its start-up wait does this before it programs the selected
           GPR 81 divider, while the running effect may have left any ordinary
           value in GPR 80.  The decisive hardware event is therefore the
           CPU's explicit host read selection, not the previous GPR contents.
           Program upload/verifier reads retain exact stored-GPR readback. */
        if (index == 0x80 && !es5510_host_upload_active) {
            return es5510_sampling_input();
        }
        if (index == 0x80) ++es5510_input_bypass;
        return value;
    }
    if (index == 0xf4) return es5510_core_read_reg(&es5510, index);
    /* DLENGTH, ABASE and BBASE are delay-memory addresses.  On the EPS-16
       board their unimplemented low four address bits read back high.  The
       original ROM's ES5510 program verifier explicitly masks those bits by
       expecting xx:xx:0f for f5..f7; returning the raw host latch makes a
       valid sampling-program download fail with ERROR 145. */
    if (index == 0xf5) return es5510_dlength | 0x00000f;
    if (index == 0xf6) return es5510_abase | 0x00000f;
    if (index == 0xf7) return es5510_bbase | 0x00000f;
    return es5510_core_read_reg(&es5510, index);
}

static void es5510_write_register(uint8_t index, uint32_t value) {
    value &= 0xffffff;
    if (index < 0xc0) {
        es5510_core_write_reg(&es5510, index, value);
        /* The board sampling oscillator keeps running independently.  In
           particular, refreshing GPR 81 must not restart its conversion
           interval; the original sampling program rewrites that selector. */
        return;
    }
    es5510_core_write_reg(&es5510, index, value);
}
/* The keypad/display protocol has three independently-addressed 16-way lamp
   banks. 74/75/76 select slot LED off/on/flash; 77/78/79 and 7a/7b/7c select
   display annunciator on/off/flash. The next byte is the physical index.
   Keeping the raw banks preserves original OS/KPC ownership of every lamp. */
#ifndef EPS16_PANEL_DISPLAY_PUBLISHED
#define EPS16_PANEL_DISPLAY_PUBLISHED(display, decimal_mask, cursor_start, cursor_end, cursor_mask) \
    ((void)0)
#endif
#ifndef EPS16_ES5505_KEYON_PUBLISHED
#define EPS16_ES5505_KEYON_PUBLISHED(voice) ((void)0)
#endif

static int panel_dotted_digit(uint8_t code, char *digit) {
    /* Original-OS table at CPU c0228c.  These are the KPC/VFD codes for
       decimal digits whose per-cell decimal point is lit. */
    static const uint8_t codes[10] = {
        0x21, 0x23, 0x25, 0x28, 0x29,
        0x3a, 0x3b, 0x5b, 0x5c, 0x5d
    };
    for (unsigned int value = 0; value < 10; ++value) {
        if (code != codes[value]) continue;
        *digit = (char)('0' + value);
        return 1;
    }
    return 0;
}
static void sample_live_log(const char *format, ...) {
    if (!sample_live_trace) return;
    va_list arguments;
    va_start(arguments, format);
    vfprintf(sample_live_trace, format, arguments);
    va_end(arguments);
    fputc('\n', sample_live_trace);
    fflush(sample_live_trace);
}

static void sample_live_log_state(const char *event, unsigned int code,
                                  int sampling_enter) {
    uint64_t now = bus_cycle_now();
    uint64_t adc_age = es5510_input_last_poll_cycle &&
                       now >= es5510_input_last_poll_cycle
                           ? now - es5510_input_last_poll_cycle : UINT64_MAX;
    sample_live_log(
        "%s code=%02x sampling=%d cycle=%llu adc_last=%llu adc_age=%llu "
        "polls=%llu valid=%llu rx=%zu wire=%zu display_frames=%u "
        "ready_search=%u display_burst=%u load_budget=%u display=|%s|",
        event, code & 0xff, sampling_enter,
        (unsigned long long)now,
        (unsigned long long)es5510_input_last_poll_cycle,
        (unsigned long long)adc_age,
        (unsigned long long)es5510_input_polls,
        (unsigned long long)es5510_input_valid,
        panel_rx_count, panel_wire_count, kpc.display_frames,
        kpc.ready_search_remaining, kpc.display_burst_remaining,
        kpc.load_ready_budget, panel_display);
}

static int parse_panel_event(const char *text, uint8_t *event, size_t *length);
static int load_disk(const char *path);
static unsigned int raw_read8(unsigned int address);
static void dmac_pcl_write(unsigned int channel, int state);
static uint32_t dmac_get32(unsigned int channel, unsigned int offset);
static int panel_schedule_at(uint8_t value, uint64_t cycle);
static void kpc_execution_service(uint64_t cycle);
static int kpc_execution_panel_packet(const uint8_t *bytes, size_t length,
                                      uint64_t cycle);
static void duart_refresh_irq_line(void);

static void panel_drop_pending_ready(void) {
    PanelWireByte *const kept_wire = RP(kept_wire);
    size_t kept_wire_count = 0;
    while (panel_wire_count) {
        PanelWireByte item = panel_wire[panel_wire_read];
        panel_wire_read = (panel_wire_read + 1) % PANEL_WIRE_SIZE;
        --panel_wire_count;
        if (item.value != 0xff) kept_wire[kept_wire_count++] = item;
    }
    panel_wire_read = 0;
    panel_wire_write = kept_wire_count % PANEL_WIRE_SIZE;
    panel_wire_count = kept_wire_count;
    panel_wire_tail_cycle = 0;
    for (size_t index = 0; index < kept_wire_count; ++index) {
        panel_wire[index] = kept_wire[index];
        panel_wire_tail_cycle = kept_wire[index].cycle;
    }

    uint8_t *const kept_rx = RP(kept_rx);
    size_t kept_rx_count = 0;
    while (panel_rx_count) {
        uint8_t item = panel_rx[panel_rx_read];
        panel_rx_read = (panel_rx_read + 1) % PANEL_RX_SIZE;
        --panel_rx_count;
        if (item != 0xff) kept_rx[kept_rx_count++] = item;
    }
    panel_rx_read = 0;
    panel_rx_write = kept_rx_count % PANEL_RX_SIZE;
    panel_rx_count = kept_rx_count;
    memcpy(panel_rx, kept_rx, kept_rx_count);
    duart_refresh_irq_line();
}

static void live_schedule_pair(uint8_t first, uint8_t second) {
    panel_schedule_at(first, (uint64_t)current_cycle);
    panel_schedule_at(second, (uint64_t)current_cycle);
}

static void kpc_arm_display_drain(unsigned int frames) {
    kpc_legacy_arm_display(&kpc, frames);
}

static void kpc_arm_load_ready(void) {
    kpc_legacy_arm_load(&kpc);
}

static void live_note(unsigned int note, unsigned int velocity, int pressed) {
    if (note < 36 || note > 96) return;
    uint8_t key = (uint8_t)(note - 36);
    live_schedule_pair(pressed ? (uint8_t)(key | 0x80) : key,
                       pressed ? (uint8_t)(velocity ? velocity : 1) : 1);
}

static void live_performance_midi(uint8_t status, uint8_t data1,
                                  uint8_t data2) {
    const unsigned int kind = status & 0xf0;
    if (kind == 0x90 && data2)
        live_note(data1, data2, 1);
    else if (kind == 0x80 || (kind == 0x90 && !data2))
        live_note(data1, data2, 0);
    else if (kind == 0xa0)
        live_note(data1, data2, 1);
    else if (kind == 0xe0) {
        const unsigned int bend = data1 | ((unsigned int)data2 << 7);
        analog_values[0] =
            (uint16_t)((((16383U - bend) * 1023U) / 16383U) << 6);
    } else if (kind == 0xb0 && data1 == 1) {
        const unsigned int wheel =
            1023U - ((unsigned int)data2 * 1023U) / 127U;
        analog_values[2] = (uint16_t)(wheel << 6);
    }
}

static void live_mount_disk(const char *env_name, const char *label) {
    const char *path = getenv(env_name);
    if (!path || !*path) {
        fprintf(stderr, "%s requires %s\n", label, env_name);
    } else if (load_disk(path)) {
        disk_change_pending = 1;
        fdc_track = 0;
        fdc_physical_track = 0;
        fdc_sector = 0;
        fdc_remaining = 0;
        live_host_clear_display_hold();
        if (load_trace_enabled) {
            fprintf(stderr,
                    "load_trace disk label=%s cycle=%lld track=%u phys=%u sector=%u last_cmd=%02x display=|%s|\n",
                    label, current_cycle, fdc_track, fdc_physical_track,
                    fdc_sector, fdc_last_command, panel_display);
        }
        fprintf(stderr, "%s inserted: %s\n", label, path);
    }
}

static int panel_is_track(unsigned int code) {
    static const uint8_t track_codes[8] = {
        0x02, 0x08, 0x0e, 0x14, 0x04, 0x22, 0x1c, 0x16
    };
    for (size_t index = 0; index < sizeof(track_codes); ++index)
        if (code == track_codes[index]) return 1;
    return 0;
}

static unsigned int panel_display_drain_frames(unsigned int code) {
    if (code == 0x0f) return 1;
    if (panel_is_track(code)) return 16;
    return 4;
}

static int sampling_adc_is_active(void) {
    unsigned int selector = es5510_gpr[0x81] & 0xff;
    const uint64_t now = bus_cycle_now();
    const uint64_t poll_age = es5510_input_last_poll_cycle &&
                              now >= es5510_input_last_poll_cycle
        ? now - es5510_input_last_poll_cycle : UINT64_MAX;
    /* GPR 81 is just ordinary program storage outside the sampling overlay;
       ROM effect 10 happens to leave the same low value as a valid ADC clock
       selector there. Sampling is therefore identified by the hardware
       behavior that matters: the OS is actively polling the selected ADC
       result in GPR 80. */
    return !es5510_host_upload_active && selector >= 2 && selector <= 8 &&
           poll_age <= 10000;
}

static void live_command(const char *line) {
    unsigned int first, second;
    if (sscanf(line, "click:%u", &first) == 1 && first < 0x80) {
        if (kpc_firmware_execution) {
            uint8_t packet[4] = {
                (uint8_t)(first | 0x80), 0, (uint8_t)first, 0
            };
            live_host_clear_display_hold();
            if (!kpc_execution_panel_packet(packet, sizeof(packet),
                                            (uint64_t)current_cycle))
                fprintf(stderr,
                        "KPC physical click rejected: %02x\n", first);
            return;
        }
        int track = panel_is_track(first);
        int sampling_enter = first == 0x23 && sampling_adc_is_active();
        int sampling_start_enter = sampling_enter && !sampling_recording_active;
        int sampling_stop_enter = sampling_enter && sampling_recording_active;
        sample_live_log_state("CLICK", first, sampling_enter);
        /* Periodic FF is only the unattended boot-idle approximation.  Once
           the physical panel produces an event, replies are driven by the
           actual button/display traffic; leaving the idle poll active injects
           a false event into time-critical handlers such as sampling. */
        kpc_legacy_set_live_periodic_ready(&kpc, 0);
        /* Track selection uses a bounded ready search to advance the original
           OS into the sampling target.  Once the OS is actively polling the
           ADC and the user presses ENTER, that search has completed.  The
           physical release packet below supplies the one ready byte required
           for the transition into RECORD; keeping the old load burst armed
           feeds unrelated readiness into the level-display state machine. */
        kpc_arm_display_drain(panel_display_drain_frames(first));
        if (load_trace_enabled) {
            fprintf(stderr,
                    "load_trace click code=%02x cycle=%lld track=%u phys=%u sector=%u last_cmd=%02x display=|%s|\n",
                    first, current_cycle, fdc_track, fdc_physical_track,
                    fdc_sector, fdc_last_command, panel_display);
        }
        live_host_clear_display_hold();
        panel_schedule_at((uint8_t)(first | 0x80), (uint64_t)current_cycle);
        panel_schedule_at(0, (uint64_t)current_cycle);
        /* UP/DOWN use the already verified short pulse for parameter steps.
           LEFT/RIGHT also select complete sampling pages, where the original
           OS debounce path requires an ordinary physical-button hold. */
        int short_cursor = first == 0x0a || first == 0x0b;
        uint64_t hold_cycles = short_cursor ? 50000 : 500000;
        uint64_t release_cycle = (uint64_t)current_cycle + hold_cycles;
        if (sampling_start_enter) {
            sampling_enter_pending = 1;
            sampling_enter_release_cycle = release_cycle;
        } else if (sampling_stop_enter) {
            sampling_enter_pending = 0;
            sampling_enter_release_cycle = 0;
            sampling_recording_active = 0;
        }
        panel_schedule_at((uint8_t)first, release_cycle);
        panel_schedule_at(0, release_cycle);
        /* The transition itself is the four-byte press/release packet.  The
           sampling overlay waits at ffea94 for the KPC's F7 transaction
           terminator: the original handler at ffa450 changes its dispatch
           state to A482 only for F7.  Ordinary panel readiness remains FF. */
        panel_schedule_at(0xff, release_cycle + PANEL_BYTE_CYCLES);
        if (first == 0x23) {
            sample_live_bus_trace_lines = 0;
            sample_live_log("SCHEDULE_READY source=click cycle=%llu",
                            (unsigned long long)(release_cycle + PANEL_BYTE_CYCLES));
        }
        if (track) kpc_arm_load_ready();
    } else if (sscanf(line, "press:%u", &first) == 1 && first < 0x80) {
        sample_live_log_state("PRESS", first, 0);
        kpc_legacy_set_live_periodic_ready(&kpc, 0);
        kpc_arm_display_drain(panel_display_drain_frames(first));
        live_host_clear_display_hold();
        live_press_cycle[first] = (uint64_t)current_cycle;
        live_schedule_pair((uint8_t)(first | 0x80), 0);
    } else if (sscanf(line, "release:%u", &first) == 1 && first < 0x80) {
        int track = panel_is_track(first);
        sample_live_log_state("RELEASE", first, 0);
        uint64_t release_cycle = (uint64_t)current_cycle;
        uint64_t minimum = live_press_cycle[first] + 500000;
        if (live_press_cycle[first] && release_cycle < minimum) release_cycle = minimum;
        panel_schedule_at((uint8_t)first, release_cycle);
        panel_schedule_at(0, release_cycle);
        panel_schedule_at(0xff, release_cycle + PANEL_BYTE_CYCLES);
        if (first == 0x23) {
            sample_live_bus_trace_lines = 0;
            sample_live_log("SCHEDULE_READY source=release cycle=%llu",
                            (unsigned long long)(release_cycle + PANEL_BYTE_CYCLES));
        }
        if (track) kpc_arm_load_ready();
        live_press_cycle[first] = 0;
    } else if (sscanf(line, "note:%u:%u", &first, &second) == 2) {
        kpc_arm_display_drain(4);
        live_note(first, second > 127 ? 127 : second, 1);
    } else if (sscanf(line, "off:%u", &first) == 1) {
        kpc_arm_display_drain(4);
        live_note(first, 1, 0);
    } else if (sscanf(line, "adc:%u:%u", &first, &second) == 2 &&
               first < 8 && second <= 1023) {
        analog_values[first] = (uint16_t)(second << 6);
        /* Analog movement is independent of the KPC UART.  The original OS
           samples the ADC and requests any resulting display update itself. */
        live_host_clear_display_hold();
    } else if (!strcmp(line, "disk:os")) {
        live_mount_disk("EPS16_OS_DISK", "OS disk");
        kpc_legacy_set_live_periodic_ready(&kpc, 1);
        kpc_arm_display_drain(0);
        kpc_legacy_cancel_load(&kpc);
    } else if (!strcmp(line, "disk:instrument")) {
        live_mount_disk("EPS16_SWAP_DISK", "Instrument disk");
        kpc_legacy_set_live_periodic_ready(&kpc, 0);
        kpc_arm_display_drain(0);
        kpc_legacy_cancel_load(&kpc);
    } else if (!strcmp(line, "quit")) {
        live_quit = 1;
    } else if (!strcmp(line, "display")) {
        EPS16_PANEL_DISPLAY_PUBLISHED(panel_display, panel_display_decimal_mask,
                                      panel_cursor_start, panel_cursor_end,
                                      panel_cursor_segment_mask);
        live_host_display(panel_display, panel_display_decimal_mask,
                          panel_cursor_start, panel_cursor_end);
    } else {
        static const struct { const char *name; uint8_t code; } commands[] = {
            {"command", 0x06}, {"edit", 0x05},
            {"instrument", 0x1a}, {"effects", 0x09}, {"up", 0x0a},
            {"down", 0x0b}, {"enter", 0x23}, {"track1", 0x02}
        };
        for (size_t index = 0; index < sizeof(commands) / sizeof(commands[0]); ++index) {
            if (strcmp(line, commands[index].name)) continue;
            live_schedule_pair((uint8_t)(commands[index].code | 0x80), 0);
            live_schedule_pair(commands[index].code, 0);
            break;
        }
    }
}

static void live_service(void) {
    /* Short OS display frames have no explicit trailing byte.  Publish them
       after the UART burst becomes idle, so the browser receives one complete
       update instead of rendering the serial byte stream character by
       character. */
    if (panel_display_dirty &&
        current_cycle - panel_display_last_change_cycle >= 100000) {
        EPS16_PANEL_DISPLAY_PUBLISHED(panel_display, panel_display_decimal_mask,
                                      panel_cursor_start, panel_cursor_end,
                                      panel_cursor_segment_mask);
        live_host_display(panel_display, panel_display_decimal_mask,
                          panel_cursor_start, panel_cursor_end);
        panel_display_dirty = 0;
    }
    live_host_adc_state(analog_values, analog_reads, duart_output, es5505.page);
    char line[512];
    while (live_host_poll_line(line, sizeof(line))) live_command(line);
    LiveMidiEvent event;
    while (live_host_poll_midi(&event))
        live_performance_midi(event.status, event.data1, event.data2);
}

static void write_le16(FILE *output, uint16_t value) {
    fputc(value & 0xff, output);
    fputc(value >> 8, output);
}

static void write_le32(FILE *output, uint32_t value) {
    write_le16(output, (uint16_t)value);
    write_le16(output, (uint16_t)(value >> 16));
}

static int wav_begin(FILE *output) {
    static const uint8_t placeholder[44];
    return fwrite(placeholder, 1, sizeof(placeholder), output) == sizeof(placeholder);
}

static int wav_finish(FILE *output, uint32_t frames, uint32_t sample_rate) {
    uint32_t data_bytes = frames * 4;
    if (fseek(output, 0, SEEK_SET)) return 0;
    fwrite("RIFF", 1, 4, output);
    write_le32(output, 36 + data_bytes);
    fwrite("WAVEfmt ", 1, 8, output);
    write_le32(output, 16);
    write_le16(output, 1);
    write_le16(output, 2);
    write_le32(output, sample_rate);
    write_le32(output, sample_rate * 4);
    write_le16(output, 4);
    write_le16(output, 16);
    fwrite("data", 1, 4, output);
    write_le32(output, data_bytes);
    return !ferror(output);
}

static int16_t audio_to_pcm16(int32_t sample) {
    sample >>= 4; /* ES5505 output is 20-bit; WAV output is signed 16-bit. */
    if (sample > 32767) return 32767;
    if (sample < -32768) return -32768;
    return (int16_t)sample;
}

static int32_t apply_master_volume(int32_t sample) {
    unsigned int level = analog_values[5] >> 6;
    return (int32_t)(((int64_t)sample * level) / 1023);
}

static uint16_t es5505_sample_read(void *context, unsigned int bank, uint32_t address) {
    (void)context;
    /* EPS-16 sample RAM is wired as a 512 KiB BS=0 region followed by the
       2 MiB BS=1 region.  ES5505 addresses are word-based within each bank. */
    size_t offset = (bank ? 0x80000U : 0U) + (size_t)address * 2;
    if (offset + 1 >= SAMPLE_RAM_SIZE) return 0;
    return ((uint16_t)sample_ram[offset] << 8) | sample_ram[offset + 1];
}

static uint16_t es5505_port_read(void *context) {
    (void)context;
    /* The EPS analog scanner is phased by the DUART's automatically-driven
       OP4-OP7 outputs.  OP0-OP2 are not the ADC channel number here (OP1 is
       also floppy side-select).  The original ROM's polling loop establishes
       these six stable phases; in particular 0x9x is the 8-bit "MR. KNOB"
       / Data Entry sample. */
    /* OP7 selects the sampling board's LINE/MIC feedback path.  The ADC
       scanner phase is carried only by OP4-OP6, so OP7 must not change the
       decoded panel-analog channel when the OS selects MIC. */
    unsigned int phase = duart_output & 0x70;
    unsigned int channel;
    switch (phase) {
        case 0x70: channel = 0; break; /* pitch wheel */
        case 0x30: channel = 1; break; /* patch select */
        case 0x60: channel = 2; break; /* modulation wheel */
        case 0x10: channel = 3; break; /* Data Entry */
        case 0x50: channel = 4; break; /* pedal / control voltage */
        case 0x20: channel = 5; break; /* volume */
        default: channel = 7; break;   /* startup/reference phase */
    }
    ++analog_reads[channel];
    if (getenv("EPS16_TRACE_ADC") && RP(es5505_port_trace_count)++ < 160)
        fprintf(stderr,
                "adc_trace cycle:%lld pc:%06x a2:%06x opr:%02x channel:%u value:%04x reference:%04x page:%02x\n",
                current_cycle, m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A2) & 0xffffff,
                duart_output & 0xff, channel, analog_values[channel],
                ((unsigned int)low_ram[0x0c5a] << 8) | low_ram[0x0c5b],
                es5505.page);
    return analog_values[channel];
}

static uint16_t es5505_read16(unsigned int address) {
    unsigned int reg = ((address - ES5505_BASE) >> 1) & 15;
    return es5505_core_read(&es5505, reg);
}

static void es5505_write16(unsigned int address, uint16_t value) {
    unsigned int reg = ((address - ES5505_BASE) >> 1) & 15;
    unsigned int page_before = es5505.page;
    uint16_t control_before = es5505.voices[page_before & 31].control;
    if (RP(es5505_trace_writes) < 0) {
        const char *enabled = getenv("EPS16_TRACE_ES5505_WRITES");
        const char *start = getenv("EPS16_TRACE_ES5505_START_CYCLE");
        RP(es5505_trace_writes) = enabled && *enabled;
        RP(es5505_trace_start_cycle) = start && *start ? atoll(start) : 0;
    }
    if (RP(es5505_trace_writes) &&
        current_cycle >= RP(es5505_trace_start_cycle))
        printf("es5505_write cycle:%lld pc:%06x page:%02x reg:%x value:%04x\n",
               current_cycle, m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
               es5505.page, reg, value);
    ++es5505_writes;
    es5505_core_write(&es5505, reg, value);
    if (getenv("EPS16_TRACE_ES5505_KEYON") && page_before < 0x20 && reg == 0 &&
        (control_before & ES5505_STOP_MASK) &&
        !(es5505.voices[page_before].control & ES5505_STOP_MASK) &&
        es5505.voices[page_before].start < es5505.voices[page_before].end) {
        const Es5505Voice *voice = &es5505.voices[page_before];
        fprintf(stderr,
                "es5505_keyon cycle:%lld voice:%u control:%04x freq:%08x "
                "start:%08x end:%08x accum:%08x bank:%u "
                "left:%02x right:%02x k1:%04x k2:%04x\n",
                current_cycle, page_before, voice->control, voice->frequency,
                voice->start, voice->end, voice->accumulator,
                (voice->control >> 2) & 1, voice->left_volume,
                voice->right_volume, voice->k1, voice->k2);
    }
    if (page_before < 0x20 && reg == 0 &&
        (control_before & ES5505_STOP_MASK) &&
        !(es5505.voices[page_before].control & ES5505_STOP_MASK) &&
        es5505.voices[page_before].start < es5505.voices[page_before].end)
        EPS16_ES5505_KEYON_PUBLISHED(page_before);
}

static void es5505_write8(unsigned int address, uint8_t value) {
    unsigned int aligned = address & ~1U;
    uint16_t current = es5505_read16(aligned);
    uint16_t updated = (address & 1) ? (uint16_t)((current & 0xff00) | value)
                                      : (uint16_t)((current & 0x00ff) | (value << 8));
    es5505_write16(aligned, updated);
}

static uint8_t es5510_read(unsigned int address) {
    unsigned int offset = ((address - ES5510_BASE) >> 1) & 0xff;
    if (!(address & 1)) return 0;
    /* Host access remains active while the 68000 is still transferring or
       verifying an ESP program.  External effect files can take longer to
       verify than the ROM programs; expiring from the last instruction write
       lets the running DSP alter a GPR underneath the original OS readback. */
    if (es5510_host_upload_active && offset <= 8)
        es5510_host_access_until = bus_cycle_now() + 250000;
    /* The sampling overlay selects GPR 80 again before every conversion,
       waits on the selected latch's low valid byte, then reads high/middle
       with MOVEP.  Selection refreshes the latch in es5510_write(); doing it
       again here consumes a second ADC sample and discards every other input
       value. */
    if (offset <= 2) return (es5510_gpr_latch >> ((2 - offset) * 8)) & 0xff;
    if (offset >= 3 && offset <= 8)
        return (es5510_instruction_latch >> ((8 - offset) * 8)) & 0xff;
    if (offset >= 9 && offset <= 11)
        return (es5510_dil_latch >> ((11 - offset) * 8)) & 0xff;
    if (offset >= 12 && offset <= 14) {
        if (offset == 14) return 0xff;
        return (es5510_dol_latch >> ((14 - offset) * 8)) & 0xff;
    }
    if (offset >= 15 && offset <= 17)
        return (es5510_dadr_latch >> ((17 - offset) * 8)) & 0xff;
    if (offset == 0x12) return 0; /* host control: ready */
    if (offset == 0x16) return 0x27; /* diagnostic program counter */
    return 0;
}

static void es5510_write(unsigned int address, uint8_t value) {
    if (!(address & 1)) return;
    unsigned int offset = ((address - ES5510_BASE) >> 1) & 0xff;
    if (offset <= 2) {
        unsigned int shift = (2 - offset) * 8;
        es5510_gpr_latch = (es5510_gpr_latch & ~(0xffU << shift)) | ((uint32_t)value << shift);
    } else if (offset >= 3 && offset <= 8) {
        unsigned int shift = (8 - offset) * 8;
        es5510_instruction_latch =
            (es5510_instruction_latch & ~((uint64_t)0xff << shift)) | ((uint64_t)value << shift);
    } else if (offset >= 12 && offset <= 14) {
        unsigned int shift = (14 - offset) * 8;
        es5510_dol_latch = (es5510_dol_latch & ~(0xffU << shift)) | ((uint32_t)value << shift);
    } else if (offset >= 15 && offset <= 17) {
        unsigned int shift = (17 - offset) * 8;
        es5510_dadr_latch = (es5510_dadr_latch & ~(0xffU << shift)) | ((uint32_t)value << shift);
        if (offset == 15) {
            unsigned int dram_address =
                es5510_core_dram_address(&es5510, es5510_dadr_latch);
            if (es5510_ram_read) {
                es5510_dil_latch = (uint16_t)es5510_dram[dram_address] << 8;
                ++es5510_dram_reads;
            } else {
                es5510_dram[dram_address] = (int16_t)(es5510_dol_latch >> 8);
                ++es5510_dram_writes;
            }
        }
    } else if (offset == 0x12) {
        /* Host Control bit 1 clears external delay RAM only while the ESP is
           halted. Effect downloads assert halt first, then issue 02 before
           uploading the replacement program. */
        if ((value & 0x02) && es5510.halted)
            memset(es5510.dram, 0, sizeof(es5510.dram));
        /* The original OS ends a completed host transfer with bit 0.  Release
           the access hold at that hardware transition instead of waiting for
           an arbitrary quiet interval; this also lets the sampling overlay
           begin immediately after its verified upload. */
        if (value & 0x01) {
            es5510_host_upload_active = 0;
            es5510_core_set_halted(&es5510, 0);
        }
    } else if (offset == 0x14) {
        es5510_ram_read = value & 0x80;
    } else if (offset == 0x18) {
        es5510_host_serial = value;
        ++es5510_host_serial_writes;
        es5510_core_set_host_serial(&es5510, value);
    } else if (offset == 0x1f) {
        es5510_host_upload_active = 1;
        es5510_host_access_until = bus_cycle_now() + 250000;
        es5510_core_set_halted(&es5510, 1);
    } else if (offset == 0x80) {
        if (value < 160) es5510_instruction_latch = es5510_instruction[value];
        if (value < 192 || value >= 0xea)
            es5510_gpr_latch = es5510_read_register(value);
    } else if (offset == 0xa0) {
        es5510_write_register(value, es5510_gpr_latch);
        ++es5510_gpr_writes;
    } else if (offset == 0xc0) {
        if (value < 160) {
            es5510_host_upload_active = 1;
            es5510_host_access_until = bus_cycle_now() + 250000;
            es5510_core_set_halted(&es5510, 1);
            es5510_instruction[value] = es5510_instruction_latch & 0xffffffffffffULL;
            ++es5510_instruction_writes;
        }
    } else if (offset == 0xe0) {
        if (value < 160) {
            es5510_host_upload_active = 1;
            es5510_host_access_until = bus_cycle_now() + 250000;
            es5510_core_set_halted(&es5510, 1);
            es5510_instruction[value] = es5510_instruction_latch & 0xffffffffffffULL;
            ++es5510_instruction_writes;
        }
        es5510_write_register(value, es5510_gpr_latch);
        ++es5510_gpr_writes;
    }
}

static int panel_enqueue(uint8_t value) {
    if (panel_rx_count == PANEL_RX_SIZE) return 0;
    panel_rx[panel_rx_write] = value;
    panel_rx_write = (panel_rx_write + 1) % PANEL_RX_SIZE;
    ++panel_rx_count;
    return 1;
}

static uint64_t bus_cycle_now(void) {
    return (uint64_t)current_cycle + (uint64_t)m68k_cycles_run();
}

static unsigned int duart_interrupt_status(void);

static void duart_refresh_irq_line(void) {
    /* MC68681 sources are level-sensitive. Reading the last byte from RHRB
       must drop IRQ3 while the CPU is still inside the handler. Otherwise
       the fixed execution slice can vector a second time with an empty ISR;
       the original sampling handler reports exactly that as ERROR 145. */
    m68k_set_irq((duart_interrupt_status() & duart_registers[5]) ? 3 : 0);
}

static int panel_schedule_at(uint8_t value, uint64_t cycle) {
    if (panel_wire_count == PANEL_WIRE_SIZE) return 0;
    uint64_t arrival = cycle + PANEL_BYTE_CYCLES;
    if (panel_wire_count && panel_wire_tail_cycle >= arrival)
        arrival = panel_wire_tail_cycle + PANEL_BYTE_CYCLES;
    panel_wire[panel_wire_write] = (PanelWireByte){arrival, value};
    panel_wire_write = (panel_wire_write + 1) % PANEL_WIRE_SIZE;
    ++panel_wire_count;
    panel_wire_tail_cycle = arrival;
    return 1;
}

static void kpc_execution_start_physical(uint64_t cycle) {
    if (kpc_physical_active || !kpc_physical_queue_count ||
        cycle < kpc_physical_next_cycle)
        return;
    kpc_physical_expected =
        kpc_physical_queue[kpc_physical_queue_read];
    kpc_physical_queue_read =
        (kpc_physical_queue_read + 1) % KPC_PHYSICAL_QUEUE_SIZE;
    --kpc_physical_queue_count;
    if (getenv("EPS16_TRACE_KPC_PANEL"))
        fprintf(stderr,
                "kpc_physical start=%02x cycle=%llu phase=%u spi=%02x/%02x\n",
                kpc_physical_expected, (unsigned long long)cycle,
                kpc_device.panel_transition_phase,
                kpc_device.spi_recent_tx[(kpc_device.spi_recent_count - 1) & 0xff],
                kpc_device.spi_recent_rx[(kpc_device.spi_recent_count - 1) & 0xff]);
    if (!kpc_device_panel_transition(
            &kpc_device, (uint8_t)(kpc_physical_expected & 0x3f),
            (kpc_physical_expected & 0x80) != 0)) {
        if (getenv("EPS16_TRACE_KPC_PANEL"))
            fprintf(stderr, "kpc_physical transition rejected phase=%u\n",
                    kpc_device.panel_transition_phase);
        return;
    }
    const char *event_transfers_text =
        getenv((kpc_physical_expected & 0x80)
                   ? "EPS16_KPC_PANEL_EVENT_TRANSFERS"
                   : "EPS16_KPC_PANEL_RELEASE_TRANSFERS");
    kpc_device.panel_transfers_remaining =
        event_transfers_text && *event_transfers_text
            ? (unsigned int)strtoul(event_transfers_text, NULL, 0)
            : 1;
    if (!(kpc_physical_expected & 0x80)) {
        kpc_device.panel_transition_phase = 6;
        kpc_device.panel_transfers_remaining = 1;
        kpc_device.panel_scan_command = 0;
        kpc_device.panel_scan_command_valid = 0;
        kpc_device.spi_receive_value = 0;
    }
    kpc_physical_active = 1;
    kpc_physical_saw_code = 0;
}

static void kpc_execution_drain_transmit(uint64_t cycle) {
    uint8_t reply;
    while (kpc_device_transmit(&kpc_device, &reply)) {
        if (getenv("EPS16_TRACE_KPC_EXEC"))
            fprintf(stderr,
                    "kpc_exec tx=%02x main_cycle=%llu kpc_cycle=%llu pc=%04x e4=%02x e5=%02x\n",
                    reply, (unsigned long long)cycle,
                    (unsigned long long)kpc_device.cpu.cycles,
                    kpc_device.cpu.pc, kpc_device.writable[0x00e4],
                    kpc_device.writable[0x00e5]);
        if (getenv("EPS16_TRACE_KPC_PANEL") && kpc_physical_active)
            fprintf(stderr, "kpc_physical tx=%02x expected=%02x stage=%d phase=%u spi=%02x/%02x\n",
                    reply, kpc_physical_expected, kpc_physical_saw_code,
                    kpc_device.panel_transition_phase,
                    kpc_device.spi_recent_tx[(kpc_device.spi_recent_count - 1) & 0xff],
                    kpc_device.spi_recent_rx[(kpc_device.spi_recent_count - 1) & 0xff]);
        if (kpc_physical_active && !kpc_physical_saw_code &&
            reply == kpc_physical_expected) {
            kpc_physical_saw_code = 1;
        } else if (kpc_physical_active && kpc_physical_saw_code &&
                   reply == 0) {
            kpc_device_panel_transition_complete(&kpc_device);
            kpc_physical_active = 0;
            kpc_physical_saw_code = 0;
            const char *hold_cycles_text =
                getenv("EPS16_KPC_PHYSICAL_HOLD_CYCLES");
            uint64_t hold_cycles = hold_cycles_text && *hold_cycles_text
                ? strtoull(hold_cycles_text, NULL, 0) : 1000000;
            kpc_physical_next_cycle = cycle + hold_cycles;
        }
        panel_schedule_at(reply, cycle + PANEL_BYTE_CYCLES);
    }
}

static void kpc_execution_service(uint64_t cycle) {
    /* The main 68000 runs at 10 MHz.  The KPC's 68HC11 E-clock is 2 MHz,
       so it advances continuously at one KPC cycle per five main cycles;
       it must not stop merely because the main board has stopped sending
       SCI bytes. */
    kpc_execution_start_physical(cycle);
    uint64_t target_cycles = cycle / 5;
    while (kpc_device.cpu.cycles < target_cycles &&
           !kpc_device.cpu.illegal) {
        size_t spi_before = kpc_device.spi_recent_count;
        kpc_device_step(&kpc_device);
        if (getenv("EPS16_TRACE_KPC_PANEL") && kpc_physical_active) {
            while (spi_before < kpc_device.spi_recent_count) {
                fprintf(stderr,
                        "kpc_physical spi=%02x/%02x phase=%u remain=%u\n",
                        kpc_device.spi_recent_tx[spi_before & 0xff],
                        kpc_device.spi_recent_rx[spi_before & 0xff],
                        kpc_device.panel_transition_phase,
                        kpc_device.panel_transfers_remaining);
                ++spi_before;
            }
        }
        /* During a physical transition the scanner response must change on
           the instruction which emits the event, not one 68000 slice later. */
        if (kpc_physical_active)
            kpc_execution_drain_transmit(cycle);
    }
    if (kpc_device.cpu.illegal && !kpc_firmware_failure_reported) {
        fprintf(stderr,
                "KPC firmware stopped: opcode=%02x pc=%04x cycles=%llu\n",
                kpc_device.cpu.last_opcode,
                (uint16_t)(kpc_device.cpu.pc - 1),
                (unsigned long long)kpc_device.cpu.cycles);
        kpc_firmware_failure_reported = 1;
    }
    kpc_execution_drain_transmit(cycle);
}

static int kpc_execution_panel_packet(const uint8_t *bytes, size_t length,
                                      uint64_t cycle) {
    if ((length & 1) || length / 2 >
            KPC_PHYSICAL_QUEUE_SIZE - kpc_physical_queue_count)
        return 0;
    for (size_t index = 0; index < length; index += 2) {
        if (bytes[index + 1]) return 0;
        kpc_physical_queue[kpc_physical_queue_write] = bytes[index];
        kpc_physical_queue_write =
            (kpc_physical_queue_write + 1) % KPC_PHYSICAL_QUEUE_SIZE;
        ++kpc_physical_queue_count;
    }
    if (!kpc_physical_active && !kpc_physical_next_cycle)
        kpc_physical_next_cycle = cycle;
    return 1;
}

static void panel_service_wire(uint64_t cycle) {
    while (panel_wire_count && panel_wire[panel_wire_read].cycle <= cycle &&
           panel_rx_count < PANEL_RX_SIZE) {
        panel_enqueue(panel_wire[panel_wire_read].value);
        panel_wire_read = (panel_wire_read + 1) % PANEL_WIRE_SIZE;
        --panel_wire_count;
    }
}

static int midi_schedule_at(uint8_t value, uint64_t cycle) {
    if (midi_wire_count == sizeof(midi_wire) / sizeof(midi_wire[0])) return 0;
    uint64_t arrival = cycle + MIDI_BYTE_CYCLES;
    if (midi_wire_count && midi_wire_tail_cycle >= arrival)
        arrival = midi_wire_tail_cycle + MIDI_BYTE_CYCLES;
    midi_wire[midi_wire_write] = (MidiWireByte){arrival, value};
    midi_wire_write =
        (midi_wire_write + 1) % (sizeof(midi_wire) / sizeof(midi_wire[0]));
    ++midi_wire_count;
    midi_wire_tail_cycle = arrival;
    return 1;
}

static void midi_service_wire(uint64_t cycle) {
    while (midi_wire_count && midi_wire[midi_wire_read].cycle <= cycle) {
        const uint8_t value = midi_wire[midi_wire_read].value;
        midi_wire_read =
            (midi_wire_read + 1) % (sizeof(midi_wire) / sizeof(midi_wire[0]));
        --midi_wire_count;
        if (duart_rx_a_enabled &&
            midi_rx_count < sizeof(midi_rx) / sizeof(midi_rx[0])) {
            midi_rx[midi_rx_write] = value;
            midi_rx_write =
                (midi_rx_write + 1) % (sizeof(midi_rx) / sizeof(midi_rx[0]));
            ++midi_rx_count;
        }
    }
}

static uint64_t duart_timer_period_cycles(void) {
    unsigned int count = ((unsigned int)duart_registers[6] << 8) | duart_registers[7];
    if (!count) count = 1;
    unsigned int mode = (duart_registers[4] >> 4) & 3;
    unsigned int timer_rate = mode == 3 ? DUART_CLOCK_RATE / 16 : DUART_CLOCK_RATE;
    uint64_t numerator = 2ULL * count * CPU_CLOCK_RATE;
    return (numerator + timer_rate - 1) / timer_rate;
}

static void duart_timer_start(uint64_t cycle) {
    duart_timer_running = 1;
    duart_timer_next_cycle = cycle + duart_timer_period_cycles();
}

static void duart_service_time(uint64_t cycle) {
    if (!duart_tx_a_ready && cycle >= duart_tx_a_ready_cycle) duart_tx_a_ready = 1;
    if (!duart_tx_b_ready && cycle >= duart_tx_b_ready_cycle) duart_tx_b_ready = 1;
    panel_service_wire(cycle);
    midi_service_wire(cycle);
    if (duart_timer_running && cycle >= duart_timer_next_cycle) {
        duart_timer_pending = 1;
        uint64_t period = duart_timer_period_cycles();
        do duart_timer_next_cycle += period;
        while (cycle >= duart_timer_next_cycle);
    }
}

static unsigned int panel_dequeue(void) {
    if (!panel_rx_count) return 0;
    unsigned int value = panel_rx[panel_rx_read];
    panel_rx_read = (panel_rx_read + 1) % PANEL_RX_SIZE;
    --panel_rx_count;
    duart_refresh_irq_line();
    ++panel_rx_consumed;
    if ((getenv("EPS16_TRACE_BUTTON_DISPATCH") || sample_live_trace) &&
        value != 0 && value != 0xff) {
        button_dispatch_trace_until = (uint64_t)current_cycle + 200000;
        button_dispatch_trace_lines = 0;
    }
    if (panel_trace_count < PANEL_TRACE_SIZE)
        panel_trace[panel_trace_count++] = (PanelTrace){
            current_cycle, m68k_get_reg(NULL, M68K_REG_PC), (uint8_t)value, 'R'};
    sample_live_log("RHRB value=%02x pc=%06x cycle=%lld rx=%zu wire=%zu",
                    value & 0xff,
                    m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
                    current_cycle, panel_rx_count, panel_wire_count);
    return value;
}

static unsigned int duart_interrupt_status(void) {
    return (duart_tx_a_enabled && duart_tx_a_ready ? 0x01 : 0x00) |
           (midi_rx_count ? 0x02 : 0x00) |
           (duart_timer_pending ? 0x08 : 0x00) |
           (duart_tx_b_enabled && duart_tx_b_ready ? 0x10 : 0x00) |
           (panel_rx_count ? 0x20 : 0x00);
}

static void trace_access(uint32_t address, uint32_t value, unsigned int width, int write) {
    for (size_t index = 0; index < trace_count; ++index) {
        Trace *trace = &traces[index];
        if (trace->address == address && trace->width == width && trace->write == write) {
            trace->value = value;
            ++trace->count;
            return;
        }
    }
    if (trace_count < MAX_TRACES) {
        traces[trace_count++] = (Trace){address, value, 1, (uint8_t)width, (uint8_t)write};
    }
}

static unsigned int duart_read(unsigned int address) {
    /* The 68000 frequently polls SRB/RHRB in a tight loop while the next KPC
       byte is still shifting in.  Advance the MC68681 to the exact current bus
       cycle on every read; servicing it only between large CPU slices makes
       those loops time out before a scheduled byte can become visible. */
    duart_service_time(bus_cycle_now());
    unsigned int reg = ((address - DUART_BASE) >> 1) & 15;
    if (reg == 5) return duart_interrupt_status();
    if (reg == 1)
        return (duart_tx_a_enabled && duart_tx_a_ready ? 0x0c : 0x00) |
               (midi_rx_count ? 0x01 : 0x00); /* SRA */
    if (reg == 3) { /* RHRA: external MIDI input */
        if (!midi_rx_count) return 0;
        const unsigned int value = midi_rx[midi_rx_read];
        midi_rx_read =
            (midi_rx_read + 1) % (sizeof(midi_rx) / sizeof(midi_rx[0]));
        --midi_rx_count;
        ++midi_rx_consumed;
        duart_refresh_irq_line();
        return value;
    }
    if (reg == 9)
        return (duart_tx_b_enabled && duart_tx_b_ready ? 0x0c : 0x00) |
               (panel_rx_count ? 0x01 : 0x00); /* SRB */
    if (reg == 11) return panel_dequeue(); /* RHRB */
    /* The EPS firmware treats DUART IP0 (ready) and IP1 (disk change) as
       active-low inputs.  A runtime swap pulses IP1 once; holding it low
       repeatedly invalidates the newly read directory state. */
    if (reg == 13) {
        unsigned int value = (disk_change_pending ? 0x00 : 0x02) |
                             (disk_loaded ? 0x00 : 0x01);
        if (getenv("EPS16_TRACE_DISK_READY"))
            printf("disk_ready_read=value:%02x pc:%06x cycle:%lld\n", value,
                   m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff, current_cycle);
        disk_change_pending = 0;
        return value;
    }
    if (reg == 14) { /* start counter/timer command */
        duart_timer_start(bus_cycle_now());
        m68k_end_timeslice();
        return 0;
    }
    if (reg == 15) { /* stop counter command also clears its interrupt */
        duart_timer_pending = 0;
        if (!(duart_registers[4] & 0x40)) duart_timer_running = 0;
        duart_refresh_irq_line();
        return 0;
    }
    return duart_registers[reg];
}

static void panel_render_level_meter(uint8_t value) {
    /* The original Level-Detect program sends a single value 00..0e after
       15 73 00 f7.  Hardware observation confirms that it is the number of
       left-to-right vertical level bars.  Trigger sensitivity remains the
       independently addressed 2a marker and does not affect this value. */
    panel_level_value = value > 0x0e ? 0x0e : value;
    for (unsigned int index = 0; index < 22; ++index)
        panel_display[index] = index < panel_level_value ? '|' : ' ';
    if (panel_threshold_position >= 0 && panel_threshold_position < 22)
        panel_display[panel_threshold_position] = '*';
    panel_display[22] = '\0';
    panel_display_decimal_mask = 0;
    panel_cursor_start = -1;
    panel_cursor_end = -1;
    panel_cursor_active = 0;
    panel_cursor_segment_mask = 0;
    panel_display_dirty = 1;
    panel_display_last_change_cycle = current_cycle;
    EPS16_PANEL_DISPLAY_PUBLISHED(panel_display, panel_display_decimal_mask,
                                  panel_cursor_start, panel_cursor_end,
                                  panel_cursor_segment_mask);
    if (live_mode)
        live_host_display(panel_display, panel_display_decimal_mask,
                          panel_cursor_start, panel_cursor_end);
    panel_display_dirty = 0;
}

static void duart_write(unsigned int address, unsigned int value) {
    unsigned int reg = ((address - DUART_BASE) >> 1) & 15;
    value &= 0xff;
    uint8_t old_value = duart_registers[reg];
    duart_registers[reg] = (uint8_t)value;
    uint64_t cycle = bus_cycle_now();
    if (reg == 4 && !(old_value & 0x40) && (value & 0x40)) {
        duart_timer_start(cycle);
        m68k_end_timeslice();
    }
    if (reg == 2) { /* CRA: channel-A receiver/transmitter commands */
        if (value & 0x01) duart_rx_a_enabled = 1;
        if (value & 0x02) duart_rx_a_enabled = 0;
        if ((value & 0x70) == 0x20) {
            midi_rx_read = midi_rx_write = midi_rx_count = 0;
        }
        if (value & 0x04) duart_tx_a_enabled = 1;
        if (value & 0x08) duart_tx_a_enabled = 0;
        duart_refresh_irq_line();
    }
    if (reg == 3) { /* THRA: external MIDI output */
        if (midi_tx_count < sizeof(midi_tx)) midi_tx[midi_tx_count++] = (uint8_t)value;
        duart_tx_a_ready = 0;
        duart_tx_a_ready_cycle = cycle + MIDI_BYTE_CYCLES;
        duart_refresh_irq_line();
        m68k_end_timeslice();
    }
    if (reg == 10) { /* CRB */
        if (value & 0x04) duart_tx_b_enabled = 1;
        if (value & 0x08) duart_tx_b_enabled = 0;
        duart_refresh_irq_line();
    }
    if (reg == 11) { /* THRB: panel/KPC transport */
        duart_tx_b_ready = 0;
        duart_tx_b_ready_cycle = cycle + PANEL_BYTE_CYCLES;
        duart_refresh_irq_line();
        if (panel_trace_count < PANEL_TRACE_SIZE)
            panel_trace[panel_trace_count++] = (PanelTrace){
                current_cycle, m68k_get_reg(NULL, M68K_REG_PC), (uint8_t)value, 'T'};
        if (panel_tx_count < sizeof(panel_tx)) panel_tx[panel_tx_count++] = (uint8_t)value;
        if (live_mode) live_host_panel_tx((uint8_t)value);
        if (value != 'f' && display_trace_byte_count < sizeof(display_trace_bytes))
            display_trace_bytes[display_trace_byte_count++] = (uint8_t)value;

        int cursor_command_byte = 0;
        int cursor_command_complete = 0;
        if (panel_cursor_command_state) {
            cursor_command_byte = 1;
            if (panel_cursor_command_state == 1 && value == 0x6a) {
                panel_cursor_command_state = 2;
            } else if (panel_cursor_command_state == 1 && value == 0x69) {
                panel_cursor_command_state = 3;
            } else if (panel_cursor_command_state == 1 && value == 0x72) {
                panel_cursor_command_state = 5;
            } else if (panel_cursor_command_state == 2 && value == 0x67) {
                if (panel_edit_cursor >= 0 && panel_edit_cursor < 21)
                    ++panel_edit_cursor;
                panel_cursor = panel_edit_cursor >= 0
                    ? (size_t)panel_edit_cursor : panel_cursor;
                panel_cursor_segment_mask = panel_edit_cursor >= 0
                    ? UINT32_C(1) << panel_edit_cursor : 0;
                panel_cursor_positioned = 1;
                panel_cursor_command_state = 0;
                cursor_command_complete = 1;
            } else if (panel_cursor_command_state == 3 && value == 0x65) {
                panel_cursor_command_state = 4;
            } else if (panel_cursor_command_state == 4 && value == 0x67) {
                if (panel_edit_cursor > 0) --panel_edit_cursor;
                panel_cursor = panel_edit_cursor >= 0
                    ? (size_t)panel_edit_cursor : panel_cursor;
                panel_cursor_segment_mask = panel_edit_cursor >= 0
                    ? UINT32_C(1) << panel_edit_cursor : 0;
                panel_cursor_positioned = 1;
                panel_cursor_command_state = 0;
                cursor_command_complete = 1;
            } else if (panel_cursor_command_state == 5 && value == 0x67) {
                panel_cursor_command_state = 6;
            } else if (panel_cursor_command_state == 6 && value == 0x72) {
                panel_cursor_segment_mask = panel_edit_cursor >= 0
                    ? UINT32_C(1) << panel_edit_cursor : 0;
                panel_cursor_command_state = 0;
                cursor_command_complete = 1;
            } else if (panel_cursor_command_state == 1) {
                /* Not a cursor-motion suffix: retain the established 63
                   incremental field update and process this byte as its first
                   replacement glyph. */
                panel_cursor = panel_cursor_start >= 0
                    ? (size_t)panel_cursor_start : panel_cursor;
                panel_cursor_active = panel_cursor_start >= 0;
                panel_cursor_width_pending = 0;
                panel_cursor_width_known = 1;
                panel_cursor_full_segments =
                    panel_cursor_start >= 0 &&
                    (panel_cursor_segment_mask &
                     (UINT32_C(1) << panel_cursor_start)) != 0;
                panel_cursor_command_state = 0;
                cursor_command_byte = 0;
            } else {
                panel_cursor_command_state = 0;
            }
        }

        /* Some original-OS pages update a field by sending its zero-based VFD
           cell address followed by replacement characters. For example,
           LOAD/Instrument volume emits 14 32 31 for "21" in cells 20..21.
           Sequencer transport can insert the normal text-mode prefix between
           the address and field marker, as in 00 60 01 62. Preserve the
           pending address across that exact command path and resolve it when
           62 begins the field. Low bytes also serve the meter, indicators and
           other transport commands, so every other non-printable successor
           still cancels the candidate. Direct addressing moves the write
           position but does not invent a visible cursor. */
        if (panel_cell_address_pending) {
            const int text_mode_prefix = value == 0x60;
            const int text_mode_argument = panel_text_mode_pending;
            const int addressed_field = value == 0x62 && panel_last_tx == 0x01;
            if ((value >= 0x20 && value <= 0x5f) || addressed_field)
                panel_cursor = panel_cell_address;
            if (!text_mode_prefix && !text_mode_argument)
                panel_cell_address_pending = 0;
        }

        /* A level byte and a trigger cell address occupy the same low-byte
           range.  Hold one low byte until the next transport byte proves
           whether it is a meter update or the address in <cell> 2a/5e. */
        const int threshold_pair = panel_level_pending &&
            (value == 0x2a || value == 0x5e) &&
            panel_last_tx == panel_level_pending_value &&
            panel_level_pending_value >= 1 &&
            panel_level_pending_value <= 22;
        if (panel_level_pending) {
            if (!threshold_pair)
                panel_render_level_meter(panel_level_pending_value);
            panel_level_pending = 0;
        }

        int level_setup_byte = 0;
        if (value == 'f') {
            panel_level_active = 0;
            panel_level_setup_state = 0;
            panel_level_pending = 0;
            panel_level_value = 0;
        } else if (panel_level_setup_state == 0 && value == 0x15) {
            panel_level_setup_state = 1;
            level_setup_byte = 1;
        } else if (panel_level_setup_state == 1 && value == 0x73) {
            panel_level_setup_state = 2;
            level_setup_byte = 1;
        } else if (panel_level_setup_state == 2 && value == 0x00) {
            panel_level_setup_state = 3;
            level_setup_byte = 1;
        } else if (panel_level_setup_state == 3 && value == 0xf7) {
            panel_level_setup_state = 0;
            panel_level_active = 1;
            level_setup_byte = 1;
        } else if (panel_level_setup_state) {
            panel_level_setup_state = value == 0x15 ? 1 : 0;
            level_setup_byte = value == 0x15;
        }

        const int level_low_is_other_parameter =
            panel_indicator_command_pending ||
            panel_cursor_width_pending ||
            panel_noncell_parameter_pending ||
            (value == 0x00 && panel_last_tx == 0x72);
        if (panel_level_active && !level_setup_byte &&
            !level_low_is_other_parameter && value <= 0x0e) {
            panel_level_pending = 1;
            panel_level_pending_value = (uint8_t)value;
        }

        if (value == 0x00 && panel_last_tx == 0x72) {
            panel_cursor = 0;
            panel_cursor_start = -1;
            panel_cursor_end = -1;
            panel_cursor_active = 0;
            panel_cursor_width_pending = 0;
            panel_cursor_width_known = 0;
            panel_cursor_segment_mask = 0;
            panel_cursor_full_segments = 0;
            panel_noncell_parameter_pending = 0;
            panel_cell_address_pending = 0;
            panel_edit_cursor = -1;
            panel_cursor_command_state = 0;
            panel_cursor_positioned = 0;
            panel_text_mode_pending = 0;
            panel_indicator_command_pending = 0;
        } else if (value == 'f') {
            panel_threshold_position = -1;
            if (panel_display_dirty)
                EPS16_PANEL_DISPLAY_PUBLISHED(panel_display,
                                              panel_display_decimal_mask,
                                              panel_cursor_start,
                                              panel_cursor_end,
                                              panel_cursor_segment_mask);
            if (live_mode && panel_display_dirty)
                live_host_display(panel_display, panel_display_decimal_mask,
                                  panel_cursor_start, panel_cursor_end);
            if (getenv("EPS16_TRACE_DISPLAY")) {
                fprintf(stderr,
                        "display_trace cycle:%lld text:|%s| decimal:%06x bytes:",
                        current_cycle, panel_display,
                        panel_display_decimal_mask & 0x3fffffU);
                for (size_t index = 0; index < display_trace_byte_count; ++index)
                    fprintf(stderr, "%02x", display_trace_bytes[index]);
                fputc('\n', stderr);
            }
            display_trace_byte_count = 0;
            memset(panel_display, ' ', 22);
            panel_display[22] = '\0';
            panel_display_decimal_mask = 0;
            panel_cursor = 0;
            panel_cursor_start = -1;
            panel_cursor_end = -1;
            panel_cursor_active = 0;
            panel_cursor_width_pending = 0;
            panel_cursor_width_known = 0;
            panel_cursor_segment_mask = 0;
            panel_cursor_full_segments = 0;
            panel_noncell_parameter_pending = 0;
            panel_cell_address_pending = 0;
            panel_edit_cursor = -1;
            panel_cursor_command_state = 0;
            panel_cursor_positioned = 0;
            panel_text_mode_pending = 0;
            panel_display_dirty = 1;
            panel_display_last_change_cycle = current_cycle;
        } else if (cursor_command_byte) {
            /* Multi-byte 63 cursor command consumed above. */
        } else if (panel_text_mode_pending) {
            /* The byte after 60 selects a text/cursor mode. It is command
               metadata, not a direct cell address. */
            panel_text_mode_pending = 0;
        } else if (panel_indicator_command_pending) {
            const unsigned int command = panel_indicator_command_pending;
            const unsigned int bank = (command - 0x74U) / 3U;
            const unsigned int operation = (command - 0x74U) % 3U;
            const uint16_t bit = (uint16_t)(UINT16_C(1) << (value & 0x0fU));
            const int off = bank == 0 ? operation == 0 : operation == 1;
            const int on = bank == 0 ? operation == 1 : operation == 0;
            if (off) {
                panel_indicator_on[bank] &= (uint16_t)~bit;
                panel_indicator_flash[bank] &= (uint16_t)~bit;
            } else if (on) {
                panel_indicator_on[bank] |= bit;
                panel_indicator_flash[bank] &= (uint16_t)~bit;
            } else {
                panel_indicator_on[bank] |= bit;
                panel_indicator_flash[bank] |= bit;
            }
            if (getenv("EPS16_TRACE_INDICATORS"))
                fprintf(stderr,
                        "indicator command=%02x index=%u banks=%04x/%04x,%04x/%04x,%04x/%04x cycle=%lld\n",
                        command, value & 0x0fU,
                        panel_indicator_on[0], panel_indicator_flash[0],
                        panel_indicator_on[1], panel_indicator_flash[1],
                        panel_indicator_on[2], panel_indicator_flash[2],
                        current_cycle);
            panel_indicator_command_pending = 0;
        } else if (!panel_cursor_active && value >= 0x74 && value <= 0x7c) {
            panel_indicator_command_pending = (uint8_t)value;
        } else if (panel_noncell_parameter_pending) {
            /* 12/15 are non-cell VFD commands. Their following argument is
               transport metadata (3c/3e in split coarse/fine address
               fields), not a printable display character. */
            panel_noncell_parameter_pending = 0;
        } else if (!panel_cursor_active &&
                   (value == 0x12 || value == 0x15)) {
            panel_noncell_parameter_pending = 1;
        } else if (value == 0x62) {
            const int positioned = panel_cursor_positioned;
            panel_cursor_start = (int)panel_cursor;
            panel_cursor_end = (int)panel_cursor;
            panel_cursor_active = 1;
            panel_cursor_width_pending = 0;
            panel_cursor_width_known = 0;
            panel_cursor_segment_mask = 0;
            panel_cursor_full_segments = 0;
            if (panel_edit_cursor < 0) panel_edit_cursor = (int)panel_cursor;
            if (positioned && panel_edit_cursor >= 0)
                panel_cursor_segment_mask = UINT32_C(1) << panel_edit_cursor;
            panel_cursor_positioned = 0;
        } else if (value == 0x63 && panel_cursor_start >= 0) {
            /* 63 is either an incremental field write or the prefix for the
               original cursor-motion sequences 63 6a 67, 63 69 65 67 and
               63 72 67 72. The following byte disambiguates them. */
            panel_cursor_command_state = 1;
            panel_cursor_active = 0;
            panel_cursor_width_pending = 0;
        } else if (value == 0x60 && panel_cursor_active && panel_last_tx == 0x62) {
            panel_cursor_width_pending = 1;
        } else if (value == 0x60 && !panel_cursor_active) {
            panel_text_mode_pending = 1;
        } else if (panel_cursor_width_pending && value < 0x20) {
            /* 62 60 03 selects the physical lower segment for this field.
               The following padded characters, not 03, define its width. */
            panel_cursor_width_pending = 0;
            panel_cursor_width_known = 0;
            panel_cursor_full_segments = value == 0x03;
            panel_cursor_segment_mask = 0;
        } else if (value == 0x72 && panel_cursor_active) {
            panel_cursor_active = 0;
            panel_cursor_width_pending = 0;
        } else if (panel_cursor_active && panel_cursor < 22) {
            char digit;
            if (panel_dotted_digit((uint8_t)value, &digit)) {
                if (panel_cursor_full_segments)
                    panel_cursor_segment_mask |=
                        UINT32_C(1) << panel_cursor;
                else
                    panel_cursor_segment_mask &=
                        ~(UINT32_C(1) << panel_cursor);
                panel_display[panel_cursor] = digit;
                panel_display_decimal_mask |= UINT32_C(1) << panel_cursor;
                ++panel_cursor;
                panel_display_dirty = 1;
                panel_display_last_change_cycle = current_cycle;
                if (!panel_cursor_width_known)
                    panel_cursor_end = (int)panel_cursor;
            } else if (value >= 0x20 && value <= 0x5f) {
                if (panel_cursor_full_segments)
                    panel_cursor_segment_mask |=
                        UINT32_C(1) << panel_cursor;
                else
                    panel_cursor_segment_mask &=
                        ~(UINT32_C(1) << panel_cursor);
                panel_display_decimal_mask &= ~(UINT32_C(1) << panel_cursor);
                panel_display[panel_cursor++] = (char)value;
                panel_display_dirty = 1;
                panel_display_last_change_cycle = current_cycle;
                if (!panel_cursor_width_known)
                    panel_cursor_end = (int)panel_cursor;
            }
        } else if (!panel_cursor_active &&
                   (value == 0x2a || value == 0x5e) &&
                   panel_last_tx >= 1 && panel_last_tx <= 22) {
            /* Sampling Level-Detect sends the threshold marker as the exact
               two-byte pair <one-based cell> 2a and erases the old marker
               with <one-based cell> 5e. Low KPC/VFD bytes have several other
               meanings, so the address is valid only as part of these
               observed pairs and must never move the general text cursor by
               itself. */
            const int position = (int)panel_last_tx - 1;
            if (value == 0x5e) {
                panel_display[position] =
                    position < panel_level_value ? '|' : ' ';
                panel_display_decimal_mask &= ~(UINT32_C(1) << position);
                if (panel_threshold_position == position)
                    panel_threshold_position = -1;
            } else {
                if (panel_threshold_position >= 0 &&
                    panel_threshold_position < 22 &&
                    panel_threshold_position != position &&
                    panel_display[panel_threshold_position] == '*') {
                    panel_display[panel_threshold_position] =
                        panel_threshold_position < panel_level_value ? '|'
                                                                   : ' ';
                    panel_display_decimal_mask &=
                        ~(UINT32_C(1) << panel_threshold_position);
                }
                panel_display[position] = '*';
                panel_display_decimal_mask &= ~(UINT32_C(1) << position);
                panel_threshold_position = position;
            }
            panel_display_dirty = 1;
            panel_display_last_change_cycle = current_cycle;
        } else if (!panel_cursor_active && !panel_level_active &&
                   value <= 0x15) {
            panel_cell_address_pending = 1;
            panel_cell_address = (uint8_t)value;
        } else if (value >= 0x20 && value <= 0x5f && panel_cursor < 22) {
            panel_display_decimal_mask &= ~(UINT32_C(1) << panel_cursor);
            panel_display[panel_cursor++] = (char)value;
            panel_display_dirty = 1;
            panel_display_last_change_cycle = current_cycle;
            if (panel_cursor_active && !panel_cursor_width_known)
                panel_cursor_end = (int)panel_cursor;
        }
        /* Publish complete VFD updates atomically. A full OS page can contain
           several fields terminated by 71/72; publishing each terminator made
           the 30 Hz plug-in GUI expose partially rebuilt ENV pages that the
           physical VFD's persistence never presents as separate frames.
           Complete 22-cell frames publish here. Short field writes publish
           after the existing 10 ms UART-idle boundary in live_service or the
           plug-in run loop. Cursor-motion commands remain immediately visible
           because they do not expose an incomplete text rebuild. */
        if (panel_cursor >= 22 || cursor_command_complete) {
            EPS16_PANEL_DISPLAY_PUBLISHED(panel_display,
                                          panel_display_decimal_mask,
                                          panel_cursor_start,
                                          panel_cursor_end,
                                          panel_cursor_segment_mask);
            if (live_mode)
                live_host_display(panel_display, panel_display_decimal_mask,
                                  panel_cursor_start, panel_cursor_end);
            panel_display_dirty = 0;
            /* The physical 22-cell controller wraps its write cursor after
               the last cell. Subsequent 60 01 text mode traffic therefore
               starts at cell zero without treating 01 as an address. */
            if (panel_cursor >= 22) panel_cursor = 0;
        }
        if (value == 0x72 && getenv("EPS16_TRACE_DISPLAY"))
            fprintf(stderr,
                    "display_field cycle:%lld text:|%s| decimal:%06x cursor:%d-%d\n",
                    current_cycle, panel_display,
                    panel_display_decimal_mask & 0x3fffffU,
                    panel_cursor_start, panel_cursor_end);
        if (getenv("EPS16_TRACE_SAMPLE_RECORD") &&
            !strncmp(panel_display, "PLAY ROOT KEY", 13))
            fprintf(stderr,
                    "sample_record_complete cycle:%lld input_valid_delta:%llu "
                    "write_delta:%llu input_total:%llu writes_total:%llu\n",
                    current_cycle,
                    (unsigned long long)(es5510_input_valid - sample_record_input_valid_start),
                    (unsigned long long)(sample_ram_write_bytes - sample_record_write_start),
                    (unsigned long long)es5510_input_valid,
                    (unsigned long long)sample_ram_write_bytes);
        if (load_trace_enabled && (strstr(panel_display, "FILE ") ||
                                   strstr(panel_display, "LOAD") ||
                                   strstr(panel_display, "PICK INSTRUMENT"))) {
            fprintf(stderr,
                    "load_trace display cycle=%lld text=|%s| cursor=%d..%d fdc_track=%u phys=%u sector=%u\n",
                    current_cycle, panel_display, panel_cursor_start,
                    panel_cursor_end, fdc_track, fdc_physical_track, fdc_sector);
        }
        panel_last_tx = (uint8_t)value;
        if (strstr(panel_display, "PICK INSTRUMENT BUTTON")) panel_pick_instrument_seen = 1;
        if (strstr(panel_display, "FILE LOADED")) {
            panel_file_loaded_seen = 1;
            kpc_legacy_cancel_load(&kpc);
        }
        if (kpc_firmware_execution) {
            if (getenv("EPS16_TRACE_KPC_EXEC"))
                fprintf(stderr,
                        "kpc_exec rx=%02x main_cycle=%llu kpc_cycle=%llu pc=%04x e4=%02x e5=%02x\n",
                        value & 0xff, (unsigned long long)cycle,
                        (unsigned long long)kpc_device.cpu.cycles,
                        kpc_device.cpu.pc, kpc_device.writable[0x00e4],
                        kpc_device.writable[0x00e5]);
            if (!kpc_device_receive(&kpc_device, (uint8_t)value)) {
                fprintf(stderr, "KPC firmware RX queue overflow\n");
            }
        } else {
            uint8_t reply;
            if (kpc_legacy_reply(&kpc, (uint8_t)value, &reply)) {
                if (getenv("EPS16_TRACE_KPC_FF") && sample_record_input_valid_start &&
                    reply == 0xff)
                    fprintf(stderr, "kpc_ff source:reply tx:%02x cycle:%lld\n",
                            value, current_cycle);
                sample_live_log("KPC_REPLY tx=%02x reply=%02x cycle=%llu",
                                value & 0xff, reply,
                                (unsigned long long)(cycle + PANEL_BYTE_CYCLES));
                panel_schedule_at(reply, cycle + PANEL_BYTE_CYCLES);
            }
            /* Ordinary ENTER dialogs keep their byte-by-byte KPC display
               handshake. Sampling ENTER is distinguished earlier by actual ADC
               polling and does not arm a display drain, so meter traffic cannot
               manufacture a second panel event. */
            if (kpc_legacy_observe_tx(&kpc, (uint8_t)value)) {
                if (getenv("EPS16_TRACE_KPC_FF") && sample_record_input_valid_start)
                    fprintf(stderr, "kpc_ff source:display tx=%02x cycle:%lld\n",
                            value, current_cycle);
                sample_live_log("SCHEDULE_READY source=display tx=%02x cycle=%llu",
                                value & 0xff,
                                (unsigned long long)(cycle + PANEL_BYTE_CYCLES));
                panel_schedule_at(0xff, cycle + PANEL_BYTE_CYCLES);
            }
        }
        m68k_end_timeslice();
    }
    if (reg == 14) duart_output |= value;  /* set output port bits */
    if (reg == 15) duart_output &= ~value; /* reset output port bits */
    if (reg == 5) duart_refresh_irq_line(); /* IMR gates IRQ immediately */
}

static unsigned int fdc_read(unsigned int address) {
    unsigned int reg = ((address - FDC_BASE) >> 1) & 3;
    if (reg == 0)
    {
        unsigned int status = fdc_remaining ? 0x03
            : fdc_error_status ? fdc_error_status
            : (((fdc_last_command & 0x80) == 0 ||
                (fdc_last_command & 0xf0) == 0xd0) &&
               fdc_physical_track == 0 ? 0x04 : 0x00);
        if (getenv("EPS16_TRACE_FDC_STATUS"))
            printf("fdc_status_read=value:%02x command:%02x pc:%06x cycle:%lld\n",
                   status, fdc_last_command,
                   m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff, current_cycle);
        dmac_pcl_write(0, 1); /* status read clears WD1772 INTRQ */
        return status;
    }
    if (reg == 1) return fdc_track;
    if (reg == 2) return fdc_sector;
    if (!fdc_remaining) return 0;
    if (fdc_remaining == 512 && fdc_event_count)
        fdc_events[fdc_event_count - 1].destination =
            m68k_get_reg(NULL, M68K_REG_A0);
    if (fdc_read_pc_count < sizeof(fdc_read_pcs) / sizeof(fdc_read_pcs[0]))
        fdc_read_pcs[fdc_read_pc_count++] = m68k_get_reg(NULL, M68K_REG_PC);
    ++fdc_data_reads;
    unsigned int value = *fdc_data++;
    --fdc_remaining;
    if (!fdc_remaining) dmac_pcl_write(0, 0); /* active-low INTRQ */
    return value;
}

static unsigned int floppy_side(void) {
    /* MC68681 output-port commands drive active-low OP pins. */
    return ((duart_output >> 1) & 1) ^ 1;
}

static void fdc_write(unsigned int address, unsigned int value) {
    unsigned int reg = ((address - FDC_BASE) >> 1) & 3;
    value &= 0xff;
    if (reg == 1) {
        fdc_track = (uint8_t)value;
        return;
    }
    if (reg == 2) {
        fdc_sector = (uint8_t)value;
        return;
    }
    if (reg == 3 && fdc_remaining &&
        (fdc_last_command & 0xe0) == 0xa0) {
        *fdc_data++ = (uint8_t)value;
        --fdc_remaining;
        if (!fdc_remaining) dmac_pcl_write(0, 0); /* active-low INTRQ */
        return;
    }
    if (reg == 3) {
        fdc_data_register = (uint8_t)value;
        return;
    }
    fdc_last_command = (uint8_t)value;
    fdc_error_status = 0;
    dmac_pcl_write(0, 1); /* accepting a command clears the previous INTRQ */
    if (dma_trace_count < sizeof(dma_trace) / sizeof(dma_trace[0]))
        dma_trace[dma_trace_count++] = (DmaTrace){
            current_cycle, m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
            'F', 0, 0, (uint8_t)value};
    if (reg != 0) return;

    unsigned int command_type = value >> 4;
    if (command_type == 0x0) { /* RESTORE */
        fdc_physical_track = 0;
        fdc_track = 0;
        dmac_pcl_write(0, 0);
        return;
    }
    if (command_type == 0x1) { /* SEEK to data register */
        fdc_physical_track = fdc_data_register < 80 ? fdc_data_register : 79;
        fdc_track = fdc_physical_track;
        dmac_pcl_write(0, 0);
        return;
    }
    if (command_type >= 0x2 && command_type <= 0x7) {
        if (command_type >= 0x4 && command_type <= 0x5) fdc_step_direction = 1;
        if (command_type >= 0x6) fdc_step_direction = -1;
        int next = (int)fdc_physical_track + fdc_step_direction;
        if (next < 0) next = 0;
        if (next > 79) next = 79;
        fdc_physical_track = (uint8_t)next;
        if (value & 0x10) fdc_track = fdc_physical_track;
        dmac_pcl_write(0, 0);
        return;
    }
    if (command_type == 0xd) { /* FORCE INTERRUPT */
        fdc_remaining = 0;
        if (value & 0x08) dmac_pcl_write(0, 0);
        return;
    }
    if (((value & 0xe0) == 0x80 || (value & 0xe0) == 0xa0) &&
        disk_loaded && fdc_physical_track < 80 && fdc_sector < 10) {
        unsigned int side = floppy_side();
        unsigned int block = ((fdc_physical_track * 2 + side) * 10) + fdc_sector;
        if (load_trace_enabled && fdc_event_count < 64) {
            fprintf(stderr,
                    "load_trace fdc_%s cycle=%lld cmd=%02x track=%u phys=%u sector=%u side=%u block=%u dest=%06x\n",
                    (value & 0x20) ? "write" : "read",
                    current_cycle, value, fdc_track, fdc_physical_track,
                    fdc_sector, side, block, dmac_get32(0, 0x14));
        }
        if (fdc_event_count < sizeof(fdc_events) / sizeof(fdc_events[0])) {
            fdc_events[fdc_event_count++] =
                (FdcEvent){(uint8_t)value, fdc_physical_track, fdc_sector,
                           (uint8_t)side, block, 0};
        }
        if (disk_sector_status[block] != EPS16_SECTOR_READABLE) {
            fdc_data = NULL;
            fdc_remaining = 0;
            fdc_error_status = disk_sector_status[block];
            dmac_pcl_write(0, 0);
            return;
        }
        fdc_data = &disk_image[block * 512];
        fdc_remaining = 512;
        ++fdc_reads;
        if (fdc_reads == 124) dma_trace_count = 0;
        if (fdc_reads == 128) irq_trace_count = 0;
    } else {
        fdc_remaining = 0;
    }
}

static uint16_t dmac_get16(unsigned int channel, unsigned int offset) {
    return ((uint16_t)dmac_registers[channel][offset] << 8) |
           dmac_registers[channel][offset + 1];
}

static void dmac_pcl_write(unsigned int channel, int state) {
    int old_state = dmac_pcl_level[channel];
    state = state != 0;
    if (state == old_state) return;
    dmac_pcl_level[channel] = state;
    if (state) dmac_registers[channel][0] |= 0x01; /* PCS */
    else dmac_registers[channel][0] &= (uint8_t)~0x01;
    if (!state && old_state) {
        unsigned int mode = dmac_registers[channel][4] & 0x07;
        if (mode == 0 || mode == 1) dmac_registers[channel][0] |= 0x02; /* PCT */
        if (mode == 1 && (dmac_registers[channel][7] & 0x08) &&
            dmac_irq_channel < 0)
            dmac_irq_channel = (int)channel;
    }
    if (dma_trace_count < sizeof(dma_trace) / sizeof(dma_trace[0]))
        dma_trace[dma_trace_count++] = (DmaTrace){
            current_cycle, m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
            'P', (uint8_t)channel, 0, (uint8_t)state};
}

static uint32_t dmac_get32(unsigned int channel, unsigned int offset) {
    return ((uint32_t)dmac_get16(channel, offset) << 16) |
           dmac_get16(channel, offset + 2);
}

static void dmac_set16(unsigned int channel, unsigned int offset, uint16_t value) {
    dmac_registers[channel][offset] = (uint8_t)(value >> 8);
    dmac_registers[channel][offset + 1] = (uint8_t)value;
}

static void dmac_set32(unsigned int channel, unsigned int offset, uint32_t value) {
    dmac_set16(channel, offset, (uint16_t)(value >> 16));
    dmac_set16(channel, offset + 2, (uint16_t)value);
}

static unsigned int dmac_read8(unsigned int address) {
    unsigned int offset = address - 0x240000;
    unsigned int channel = (offset >> 6) & 3;
    return dmac_registers[channel][offset & 0x3f];
}

static void dmac_write8(unsigned int address, uint8_t value) {
    unsigned int offset = address - 0x240000;
    unsigned int channel = (offset >> 6) & 3;
    unsigned int reg = offset & 0x3f;
    if (channel == 1 && getenv("EPS16_TRACE_ADC"))
        fprintf(stderr,
                "adc_dmac cycle:%lld pc:%06x reg:%02x value:%02x\n",
                current_cycle, m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
                reg, value);
    if (dma_trace_count < sizeof(dma_trace) / sizeof(dma_trace[0]))
        dma_trace[dma_trace_count++] = (DmaTrace){
            current_cycle, m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
            'D', (uint8_t)channel, (uint8_t)reg, value};
    if (reg == 0) {
        dmac_registers[channel][0] &= (uint8_t)~(value & 0xf6);
        if (value & 0x10) dmac_registers[channel][1] = 0;
        if (dmac_irq_channel == (int)channel &&
            (dmac_registers[channel][0] & 0xf2) == 0)
            dmac_irq_channel = -1;
        return;
    }
    dmac_registers[channel][reg] = value;
    if (reg == 7) {
        if (value & 0x80) {
            dmac_registers[channel][0] &= (uint8_t)~0xe0;
            dmac_registers[channel][0] |= 0x08;
        }
        if (value & 0x10) {
            dmac_registers[channel][0] &= (uint8_t)~0x08;
            dmac_registers[channel][7] &= (uint8_t)~0xc0;
        }
        if (!(value & 0x08) && dmac_irq_channel == (int)channel)
            dmac_irq_channel = -1;
    }
}

static void dmac_memory_write8(uint32_t address, uint8_t value) {
    address &= 0xffffff;
    if (address < LOW_RAM_SIZE) {
        low_ram[address] = value;
    } else if (address >= SAMPLE_RAM_BASE && address < SAMPLE_RAM_BASE + SAMPLE_RAM_SIZE) {
        sample_ram[address - SAMPLE_RAM_BASE] = value;
        ++sample_ram_write_bytes;
    } else if (address >= OS_RAM_BASE) {
        os_ram[address - OS_RAM_BASE] = value;
    } else {
        trace_access(address, value, 1, 1);
    }
}

static uint8_t dmac_memory_read8(uint32_t address) {
    address &= 0xffffff;
    if (address < LOW_RAM_SIZE) return low_ram[address];
    if (address >= SAMPLE_RAM_BASE &&
        address < SAMPLE_RAM_BASE + SAMPLE_RAM_SIZE)
        return sample_ram[address - SAMPLE_RAM_BASE];
    if (address >= OS_RAM_BASE) return os_ram[address - OS_RAM_BASE];
    if (address >= ROM_BASE && address < ROM_BASE + ROM_SIZE)
        return rom[address - ROM_BASE];
    trace_access(address, 0, 1, 0);
    return 0;
}

static void dmac_complete(unsigned int channel) {
    dmac_registers[channel][0] |= 0xe0;
    dmac_registers[channel][0] &= (uint8_t)~0x08;
    dmac_registers[channel][7] &= (uint8_t)~0xc0;
    if ((dmac_registers[channel][7] & 0x08) && dmac_irq_channel < 0)
        dmac_irq_channel = (int)channel;
}

static void scsi_raise_interrupt(void) {
    scsi_aux_status |= 0x80;
    dmac_pcl_write(1, 0); /* WD33C93 INT is wired to channel-1 PCL. */
}

static void scsi_clear_interrupt(void) {
    scsi_aux_status &= (uint8_t)~0x80;
    dmac_pcl_write(1, 1);
}

static uint8_t scsi_dma_read8(void) {
    if (scsi_stream_remaining) {
        const int byte = fgetc(scsi_image);
        --scsi_stream_remaining;
        const unsigned int remaining = (unsigned int)scsi_stream_remaining;
        scsi_registers[0x12] = (uint8_t)(remaining >> 16);
        scsi_registers[0x13] = (uint8_t)(remaining >> 8);
        scsi_registers[0x14] = (uint8_t)remaining;
        if (!remaining) {
            scsi_registers[0x18] = 0;
            scsi_registers[0x17] = 0x16;
            scsi_raise_interrupt();
        }
        return byte == EOF ? 0 : (uint8_t)byte;
    }
    if (scsi_dma_padding_remaining) {
        --scsi_dma_padding_remaining;
        return 0;
    }
    return 0;
}

static void dmac_service(void) {
    for (unsigned int channel = 0; channel < 4; ++channel) {
        for (unsigned int transfer = 0; transfer < 4096; ++transfer) {
            if (!(dmac_registers[channel][0] & 0x08)) break;
            uint16_t count = dmac_get16(channel, 0x0a);
            if (!count) {
                dmac_complete(channel);
                break;
            }
            uint8_t operation = dmac_registers[channel][5];
            uint32_t device = dmac_get32(channel, 0x14) & 0xffffff;
            if (channel == 0 && device == FDC_BASE + 7 && !fdc_remaining) break;
            if (channel == 1 && device == 0x340001 &&
                !scsi_stream_remaining && !scsi_dma_padding_remaining)
                break;
            uint32_t memory = dmac_get32(channel, 0x0c);
            if (operation & 0x80) {
                uint8_t value = device == FDC_BASE + 7
                    ? (uint8_t)fdc_read(FDC_BASE + 6)
                    : device == 0x340001
                        ? scsi_dma_read8()
                        : (uint8_t)raw_read8(device);
                dmac_memory_write8(memory, value);
            } else {
                if (device != FDC_BASE + 7) break;
                fdc_write(FDC_BASE + 6, dmac_memory_read8(memory));
            }
            ++dmac_transfers;
            --count;
            dmac_set16(channel, 0x0a, count);
            unsigned int memory_mode = dmac_registers[channel][6] & 0x0c;
            if (memory_mode == 0x04) ++memory;
            else if (memory_mode == 0x08) --memory;
            dmac_set32(channel, 0x0c, memory);
            if (!count) dmac_complete(channel);
        }
    }
}

static void scsi_trace(const char *operation, unsigned int reg,
                       unsigned int value) {
    if (!getenv("EPS16_TRACE_SCSI")) return;
    fprintf(stderr, "scsi cycle:%lld pc:%06x %s reg:%02x value:%02x\n",
            current_cycle, m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
            operation, reg & 0x1f, value & 0xff);
}

static int scsi_mount_image(const char *path) {
    FILE *image = path && *path ? fopen(path, "rb") : NULL;
    if (!image) return 0;
    if (fseeko(image, 0, SEEK_END) || ftello(image) <= 0) {
        fclose(image);
        return 0;
    }
    const off_t size = ftello(image);
    if ((size & 511) != 0 || fseeko(image, 0, SEEK_SET)) {
        fclose(image);
        return 0;
    }
    if (scsi_image) fclose(scsi_image);
    scsi_image = image;
    scsi_image_size = (uint64_t)size;
    return 1;
}

static void scsi_command(uint8_t command) {
    scsi_trace("command", 0x18, command);
    if (getenv("EPS16_TRACE_SCSI")) {
        fprintf(stderr, "scsi cdb=");
        for (unsigned int index = 3; index <= 14; ++index)
            fprintf(stderr, "%02x", scsi_registers[index]);
        fprintf(stderr, " target=%u count=%u\n",
                scsi_registers[0x15] & 7,
                ((unsigned int)scsi_registers[0x12] << 16) |
                ((unsigned int)scsi_registers[0x13] << 8) |
                scsi_registers[0x14]);
    }
    scsi_buffer_position = 0;
    scsi_buffer_size = 0;
    scsi_stream_remaining = 0;
    scsi_dma_padding_remaining = 0;
    if (command == 0) {
        scsi_registers[0x18] = 0;
        scsi_registers[0x17] = 0x00; /* reset complete */
        scsi_raise_interrupt();
        return;
    }
    if ((command & 0x7f) == 0x09 && (scsi_registers[0x15] & 7) == 0 &&
        (scsi_registers[3] == 0x03 || scsi_registers[3] == 0x12)) {
        /* REQUEST SENSE and INQUIRY are the EPS boot-time target discovery
           commands. The virtual CD drive stays connected without a medium. */
        scsi_buffer_size = scsi_registers[7];
        if (scsi_buffer_size > sizeof(scsi_buffer))
            scsi_buffer_size = sizeof(scsi_buffer);
        memset(scsi_buffer, 0, scsi_buffer_size);
        if (scsi_registers[3] == 0x03) {
            if (scsi_buffer_size) scsi_buffer[0] = 0x70;
        } else {
            static const uint8_t inquiry[36] = {
                /* The EPS CD-ROM discovery path requires a removable
                   direct-access target and the literal CD-ROM product tag. */
                0x00, 0x80, 0x01, 0x01, 31, 0, 0, 0,
                'E','N','S','O','N','I','Q',' ',
                0,0,'C','D','-','R','O','M',' ','E','P','S',' ',' ',' ',' ',
                '1','.','0',' '
            };
            size_t count = scsi_buffer_size < sizeof(inquiry)
                               ? scsi_buffer_size : sizeof(inquiry);
            memcpy(scsi_buffer, inquiry, count);
        }
        scsi_aux_status = scsi_buffer_size ? 0x01 : 0x80; /* DBR or INT */
        scsi_registers[0x17] = scsi_buffer_size ? 0x00 : 0x16;
        return;
    }
    if ((command & 0x7f) == 0x09 && (scsi_registers[0x15] & 7) == 0 &&
        scsi_registers[3] == 0x00) {
        scsi_registers[0x18] = 0;
        scsi_registers[0x17] = 0x16;
        scsi_raise_interrupt();
        return;
    }
    if ((command & 0x7f) == 0x09 && (scsi_registers[0x15] & 7) == 0 &&
        scsi_registers[3] == 0x28 && scsi_image) {
        const uint32_t lba =
            ((uint32_t)scsi_registers[5] << 24) |
            ((uint32_t)scsi_registers[6] << 16) |
            ((uint32_t)scsi_registers[7] << 8) |
            scsi_registers[8];
        const uint32_t blocks =
            ((uint32_t)scsi_registers[10] << 8) | scsi_registers[11];
        const uint64_t offset = (uint64_t)lba * 512;
        const uint64_t bytes = (uint64_t)blocks * 512;
        if (bytes && offset <= scsi_image_size &&
            bytes <= scsi_image_size - offset &&
            !fseeko(scsi_image, (off_t)offset, SEEK_SET)) {
            scsi_stream_remaining = bytes;
            /* The SP-2 programs HD63450 channel 1 for data length + 1.  The
               WD33C93 drops DREQ after the data phase with one DMA transfer
               still pending; its completion interrupt then makes the OS
               abort/clear the channel.  Writing a fabricated final byte would
               overrun the destination and corrupt the EPS sample-memory free
               list after the first instrument load. */
            scsi_aux_status = 0x01; /* DBR */
            scsi_registers[0x17] = 0;
            return;
        }
    }
    scsi_registers[0x18] = 0;
    scsi_registers[0x17] = 0x42; /* selection timeout */
    scsi_raise_interrupt();
}

static unsigned int scsi_read8(unsigned int address) {
    if ((address & 2) == 0) {
        scsi_trace("read-aux", 0x1f, scsi_aux_status);
        return scsi_aux_status;
    }
    const unsigned int reg = scsi_address & 0x1f;
    uint8_t value = scsi_registers[reg];
    if (reg == 0x19 && scsi_buffer_position < scsi_buffer_size) {
        value = scsi_buffer[scsi_buffer_position++];
        unsigned int remaining =
            (unsigned int)(scsi_buffer_size - scsi_buffer_position);
        scsi_registers[0x12] = (uint8_t)(remaining >> 16);
        scsi_registers[0x13] = (uint8_t)(remaining >> 8);
        scsi_registers[0x14] = (uint8_t)remaining;
        if (!remaining) {
            scsi_registers[0x18] = 0;
            scsi_registers[0x17] = 0x16; /* select-and-transfer complete */
            scsi_raise_interrupt();
        }
    } else if (reg == 0x19 && scsi_stream_remaining) {
        const int byte = fgetc(scsi_image);
        value = byte == EOF ? 0 : (uint8_t)byte;
        --scsi_stream_remaining;
        const unsigned int remaining = (unsigned int)scsi_stream_remaining;
        scsi_registers[0x12] = (uint8_t)(remaining >> 16);
        scsi_registers[0x13] = (uint8_t)(remaining >> 8);
        scsi_registers[0x14] = (uint8_t)remaining;
        if (!remaining) {
            scsi_registers[0x18] = 0;
            scsi_registers[0x17] = 0x16;
            scsi_raise_interrupt();
        }
    }
    scsi_trace("read", reg, value);
    if (reg == 0x17) scsi_clear_interrupt();
    if (reg < 0x19) scsi_address = (uint8_t)(reg + 1);
    return value;
}

static void scsi_write8(unsigned int address, uint8_t value) {
    if ((address & 2) == 0) {
        scsi_address = value & 0x1f;
        scsi_trace("select", scsi_address, value);
        return;
    }
    const unsigned int reg = scsi_address & 0x1f;
    scsi_registers[reg] = value;
    scsi_trace("write", reg, value);
    if (reg == 0x18) scsi_command(value);
    if (reg < 0x19) scsi_address = (uint8_t)(reg + 1);
}

static unsigned int raw_read8(unsigned int address) {
    address &= 0xFFFFFF;
    if (address < LOW_RAM_SIZE)
        return function_code == 6 ? rom[address] : low_ram[address];
    if (address >= ES5505_BASE && address < ES5505_BASE + ES5505_SIZE) {
        uint16_t value = es5505_read16(address & ~1U);
        return (address & 1) ? value & 0xff : value >> 8;
    }
    if (address >= ES5510_BASE && address < ES5510_BASE + ES5510_SIZE)
        return es5510_read(address);
    if (address >= 0x240000 && address < 0x240100)
        return dmac_read8(address);
    if (address >= DUART_BASE && address < DUART_BASE + DUART_SIZE)
        return duart_read(address);
    if (address >= FDC_BASE && address < FDC_BASE + FDC_SIZE)
        return fdc_read(address);
    if (address >= SCSI_BASE && address < SCSI_BASE + SCSI_SIZE)
        return scsi_read8(address);
    if (address >= SAMPLE_RAM_BASE && address < SAMPLE_RAM_BASE + SAMPLE_RAM_SIZE)
        return sample_ram[address - SAMPLE_RAM_BASE];
    if (address >= OS_RAM_BASE) return os_ram[address - OS_RAM_BASE];
    if (address >= ROM_BASE && address < ROM_BASE + ROM_SIZE) return rom[address - ROM_BASE];
    trace_access(address, 0, 1, 0);
    return 0;
}

unsigned int m68k_read_memory_8(unsigned int address) {
    unsigned int value = raw_read8(address);
    if (sample_live_trace && sample_live_bus_trace_lines < 128 &&
        (sample_live_current_pc == 0xffe41c ||
         sample_live_current_pc == 0xffe422 ||
         sample_live_current_pc == 0xffe428 ||
         sample_live_current_pc == 0xffe42a ||
         sample_live_current_pc == 0xffe42e) &&
        (address & 0xffffff) < OS_RAM_BASE) {
        sample_live_log("WAIT_BUS_READ pc=%06x address=%06x value=%02x cycle=%llu",
                        sample_live_current_pc, address & 0xffffff, value & 0xff,
                        (unsigned long long)bus_cycle_now());
        ++sample_live_bus_trace_lines;
    }
    return value;
}

unsigned int m68k_read_memory_16(unsigned int address) {
    return (m68k_read_memory_8(address) << 8) |
           m68k_read_memory_8(address + 1);
}

unsigned int m68k_read_memory_32(unsigned int address) {
    address &= 0xFFFFFF;
    return (m68k_read_memory_16(address) << 16) | m68k_read_memory_16(address + 2);
}

static void set_function_code(unsigned int value) {
    function_code = value;
}

static int interrupt_acknowledge(int level) {
    int vector = M68K_INT_ACK_AUTOVECTOR;
    if (level == 3 && duart_registers[12] >= 2) vector = duart_registers[12];
    if (level == 2 && dmac_irq_channel >= 0) {
        unsigned int channel = (unsigned int)dmac_irq_channel;
        vector = (dmac_registers[channel][0] & 0x10)
                     ? dmac_registers[channel][0x27]
                     : dmac_registers[channel][0x25];
    }
    if (irq_trace_count < sizeof(irq_trace) / sizeof(irq_trace[0]))
        irq_trace[irq_trace_count++] = (IrqTrace){
            current_cycle, m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
            (uint8_t)level, (uint8_t)vector};
    return vector;
}

static int illegal_instruction(int opcode) {
    if (illegal_instruction_count <
        sizeof(illegal_instructions) / sizeof(illegal_instructions[0])) {
        illegal_instructions[illegal_instruction_count++] =
            (IllegalInstruction){current_cycle,
                                 m68k_get_reg(NULL, M68K_REG_PPC) & 0xffffff,
                                 (uint16_t)opcode};
    }
    return 0;
}

static void instruction_hook(unsigned int pc) {
    if (fdc_reads < 124) return;
    pc &= 0xffffff;
#if !defined(EPS16_PLUGIN_BUILD)
    sample_live_current_pc = pc;
#endif
    if (pc == 0xffebc8 && sampling_enter_pending &&
        (uint64_t)current_cycle >= sampling_enter_release_cycle &&
        (m68k_get_reg(NULL, M68K_REG_D2) & 0xff) == 0x25 &&
        (m68k_get_reg(NULL, M68K_REG_A2) & 0xffffff) == 0x001be8) {
        low_ram[0x0b72] = 0xff;
        sample_live_log("SAMPLE_PRESS_ARM cycle=%lld", current_cycle);
    }
    if (sample_live_trace &&
        (uint64_t)current_cycle <= button_dispatch_trace_until &&
        (pc == 0xffeb30 || pc == 0xffeb32 || pc == 0xffeb34 ||
         pc == 0xffeb36 || pc == 0xffeb3a || pc == 0xffeb3e ||
         pc == 0xffeb42))
        sample_live_log(
            "SAMPLE_POLL cycle=%lld pc=%06x d0=%08x d2=%08x a2=%06x "
            "sr=%04x b72=%02x",
            current_cycle, pc, m68k_get_reg(NULL, M68K_REG_D0),
            m68k_get_reg(NULL, M68K_REG_D2),
            m68k_get_reg(NULL, M68K_REG_A2) & 0xffffff,
            m68k_get_reg(NULL, M68K_REG_SR) & 0xffff, low_ram[0x0b72]);
    if (sample_live_trace &&
        (pc == 0xffeb44 || pc == 0xffeb4a || pc == 0xffebb4 ||
         pc == 0xffebc8 || pc == 0xffebce || pc == 0xffebda)) {
        unsigned int d2 = m68k_get_reg(NULL, M68K_REG_D2);
        unsigned int a2 = m68k_get_reg(NULL, M68K_REG_A2) & 0xffffff;
        if ((d2 & 0xff) == 0x25 || low_ram[0x0b72] ||
            a2 == 0x0085fe || a2 == 0x001be8)
            sample_live_log(
                "SAMPLE_EVENT cycle=%lld pc=%06x d2=%08x a2=%06x "
                "b72=%02x ad6=%02x%02x rx=%zu wire=%zu",
                current_cycle, pc, d2, a2, low_ram[0x0b72],
                low_ram[0x0ad6], low_ram[0x0ad7], panel_rx_count,
                panel_wire_count);
    }
    if (pc == 0xffe4f0) {
        sampling_enter_pending = 0;
        sampling_enter_release_cycle = 0;
        sampling_recording_active = 1;
        sample_live_log("RECORD_ENTRY cycle=%lld polls=%llu valid=%llu writes=%llu",
                        current_cycle,
                        (unsigned long long)es5510_input_polls,
                        (unsigned long long)es5510_input_valid,
                        (unsigned long long)sample_ram_write_bytes);
        sample_record_input_valid_start = es5510_input_valid;
        sample_record_write_start = sample_ram_write_bytes;
        /* Bound inactive microphone history while retaining enough Chrome
           transport cushion for a continuous ADC stream at record start. */
        if (live_mode) live_host_audio_input_prepare_recording();
        /* The target-selection ready burst has completed its job once the
           original sampling overlay enters RECORD.  Leaving queued readiness
           active turns the next FF into an immediate stop request. */
        kpc_legacy_cancel_load(&kpc);
        panel_drop_pending_ready();
    }
#if !defined(EPS16_PLUGIN_BUILD)
    if (pc == 0xffba18 && getenv("EPS16_TRACE_ES5505_KEYON"))
        fprintf(stderr,
                "voice_volume_source cycle:%lld d0:%08x d1:%08x d2:%08x "
                "d3:%08x d4:%08x d5:%08x d6:%08x d7:%08x "
                "a0:%06x a1:%06x a2:%06x a3:%06x a4:%06x a5:%06x a6:%06x\n",
                current_cycle,
                m68k_get_reg(NULL, M68K_REG_D0),
                m68k_get_reg(NULL, M68K_REG_D1),
                m68k_get_reg(NULL, M68K_REG_D2),
                m68k_get_reg(NULL, M68K_REG_D3),
                m68k_get_reg(NULL, M68K_REG_D4),
                m68k_get_reg(NULL, M68K_REG_D5),
                m68k_get_reg(NULL, M68K_REG_D6),
                m68k_get_reg(NULL, M68K_REG_D7),
                m68k_get_reg(NULL, M68K_REG_A0) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A1) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A2) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A3) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A4) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A5) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A6) & 0xffffff);
    if (getenv("EPS16_TRACE_SAMPLE_IRQ") && pc >= 0xffe55a && pc <= 0xffe584)
        fprintf(stderr,
                "sample_irq pc:%06x cycle:%lld imr:%02x isr:%02x panel:%zu "
                "irq_vector:%02x\n",
                pc, current_cycle, duart_registers[5], duart_interrupt_status(),
                panel_rx_count, duart_registers[12]);
    if (getenv("EPS16_TRACE_SAMPLE_RECORD") &&
        (pc == 0xffe3b2 || pc == 0xffe3bc || pc == 0xffe4d8 ||
         pc == 0xffe4e8 || pc == 0xffe4f0 || pc == 0xffe528 ||
         pc == 0xffe550 || pc == 0xffe556 || pc == 0xffe584))
        fprintf(stderr,
                "sample_record pc:%06x cycle:%lld d0:%08x d1:%08x d2:%08x "
                "a0:%06x a1:%06x a2:%06x a5:%06x a6:%06x "
                "low_a16:%08x os_e046:%08x os_e04a:%08x writes:%llu "
                "input_polls:%llu input_valid:%llu input_bypass:%llu gpr80:%06x gpr81:%06x\n",
                pc, current_cycle,
                m68k_get_reg(NULL, M68K_REG_D0),
                m68k_get_reg(NULL, M68K_REG_D1),
                m68k_get_reg(NULL, M68K_REG_D2),
                m68k_get_reg(NULL, M68K_REG_A0) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A1) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A2) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A5) & 0xffffff,
                m68k_get_reg(NULL, M68K_REG_A6) & 0xffffff,
                ((uint32_t)low_ram[0x0a16] << 24) |
                    ((uint32_t)low_ram[0x0a17] << 16) |
                    ((uint32_t)low_ram[0x0a18] << 8) | low_ram[0x0a19],
                ((uint32_t)os_ram[0xe046] << 24) |
                    ((uint32_t)os_ram[0xe047] << 16) |
                    ((uint32_t)os_ram[0xe048] << 8) | os_ram[0xe049],
                ((uint32_t)os_ram[0xe04a] << 24) |
                    ((uint32_t)os_ram[0xe04b] << 16) |
                    ((uint32_t)os_ram[0xe04c] << 8) | os_ram[0xe04d],
                (unsigned long long)sample_ram_write_bytes,
                (unsigned long long)es5510_input_polls,
                (unsigned long long)es5510_input_valid,
                (unsigned long long)es5510_input_bypass,
                es5510_gpr[0x80] & 0xffffff, es5510_gpr[0x81] & 0xffffff);
    if ((getenv("EPS16_TRACE_BUTTON_DISPATCH") || sample_live_trace) &&
        (uint64_t)current_cycle <= button_dispatch_trace_until &&
        button_dispatch_trace_lines < 512) {
        sample_live_log(
            "BUTTON_PATH cycle=%lld pc=%06x d0=%08x d1=%08x d2=%08x d3=%08x "
            "a0=%06x a1=%06x a2=%06x a3=%06x",
            current_cycle, pc,
            m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
            m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
            m68k_get_reg(NULL, M68K_REG_A0) & 0xffffff,
            m68k_get_reg(NULL, M68K_REG_A1) & 0xffffff,
            m68k_get_reg(NULL, M68K_REG_A2) & 0xffffff,
            m68k_get_reg(NULL, M68K_REG_A3) & 0xffffff);
        if (getenv("EPS16_TRACE_BUTTON_DISPATCH"))
            fprintf(stderr,
                    "button_dispatch cycle:%lld pc:%06x d0:%08x d1:%08x d2:%08x d3:%08x "
                    "a0:%06x a1:%06x a2:%06x a3:%06x\n",
                    current_cycle, pc,
                    m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                    m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
                    m68k_get_reg(NULL, M68K_REG_A0) & 0xffffff,
                    m68k_get_reg(NULL, M68K_REG_A1) & 0xffffff,
                    m68k_get_reg(NULL, M68K_REG_A2) & 0xffffff,
                    m68k_get_reg(NULL, M68K_REG_A3) & 0xffffff);
        ++button_dispatch_trace_lines;
    }
    if (getenv("EPS16_TRACE_KEY_EVENT") && pc >= 0xffa4ac && pc <= 0xffa4f4)
        printf("key_event=pc:%06x d0:%08x d1:%08x d2:%08x d3:%08x cycle:%lld\n",
               pc, m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
               m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
               current_cycle);
    pc_ring[pc_ring_position++ % 64] = pc;
    if (pc == 0xc0f6be) {
        es5510_verify_d1 = m68k_get_reg(NULL, M68K_REG_D1);
        es5510_verify_d2 = m68k_get_reg(NULL, M68K_REG_D2);
        es5510_verify_d3 = m68k_get_reg(NULL, M68K_REG_D3);
        es5510_verify_a3 = m68k_get_reg(NULL, M68K_REG_A3) & 0xffffff;
        es5510_verify_a6 = m68k_get_reg(NULL, M68K_REG_A6) & 0xffffff;
        es5510_verify_actual = raw_read8(es5510_verify_a6);
        ++es5510_verify_failures;
    }
    if (pc == 0xc079aa && !fatal_history_captured) {
        for (size_t index = 0; index < 64; ++index)
            fatal_history[index] = pc_ring[(pc_ring_position + index) % 64];
        fatal_history_captured = 1;
    }
    ++postboot_instruction_count;
    if (pc >= ROM_BASE && pc < ROM_BASE + ROM_SIZE) {
        ++postboot_rom_instructions;
        ++postboot_pc_counts[(pc - ROM_BASE) >> 1];
    } else if (pc < LOW_RAM_SIZE) {
        ++postboot_low_ram_instructions;
    } else if (pc >= SAMPLE_RAM_BASE && pc < SAMPLE_RAM_BASE + SAMPLE_RAM_SIZE) {
        ++postboot_sample_ram_instructions;
    } else if (pc >= OS_RAM_BASE) {
        ++postboot_os_ram_instructions;
        ++postboot_os_pc_counts[(pc - OS_RAM_BASE) >> 1];
    } else {
        ++postboot_other_instructions;
    }
#endif
}

unsigned int m68k_read_disassembler_16(unsigned int address) {
    return m68k_read_memory_16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address) {
    return m68k_read_memory_32(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
    address &= 0xFFFFFF;
    if (address >= ES5505_BASE && address < ES5505_BASE + ES5505_SIZE) {
        es5505_write8(address, value);
        return;
    }
    if (address >= ES5510_BASE && address < ES5510_BASE + ES5510_SIZE) {
        es5510_write(address, (uint8_t)value);
        return;
    }
    if (address >= 0x240000 && address < 0x240100) {
        dmac_write8(address, (uint8_t)value);
        return;
    }
    if (address >= DUART_BASE && address < DUART_BASE + DUART_SIZE) {
        duart_write(address, value);
        return;
    }
    if (address >= FDC_BASE && address < FDC_BASE + FDC_SIZE) {
        fdc_write(address, value);
        return;
    }
    if (address >= SCSI_BASE && address < SCSI_BASE + SCSI_SIZE) {
        scsi_write8(address, (uint8_t)value);
        return;
    }
    if (address < LOW_RAM_SIZE && function_code != 6) {
        low_ram[address] = (uint8_t)value;
        return;
    }
    if (address >= SAMPLE_RAM_BASE && address < SAMPLE_RAM_BASE + SAMPLE_RAM_SIZE) {
        sample_ram[address - SAMPLE_RAM_BASE] = (uint8_t)value;
        ++sample_ram_write_bytes;
        return;
    }
    if (address >= OS_RAM_BASE) {
        os_ram[address - OS_RAM_BASE] = (uint8_t)value;
        return;
    }
    trace_access(address, value & 0xFF, 1, 1);
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
    address &= 0xFFFFFF;
    if (address >= ES5505_BASE && address + 1 < ES5505_BASE + ES5505_SIZE) {
        es5505_write16(address, (uint16_t)value);
        return;
    }
    if (address >= 0x240000 && address + 1 < 0x240100) {
        dmac_write8(address, (uint8_t)(value >> 8));
        dmac_write8(address + 1, (uint8_t)value);
        return;
    }
    if (address + 1 < LOW_RAM_SIZE && function_code != 6) {
        low_ram[address] = (uint8_t)(value >> 8);
        low_ram[address + 1] = (uint8_t)value;
        return;
    }
    if (address >= SAMPLE_RAM_BASE && address + 1 < SAMPLE_RAM_BASE + SAMPLE_RAM_SIZE) {
        size_t offset = address - SAMPLE_RAM_BASE;
        sample_ram[offset] = (uint8_t)(value >> 8);
        sample_ram[offset + 1] = (uint8_t)value;
        sample_ram_write_bytes += 2;
        return;
    }
    if (address >= OS_RAM_BASE && address + 1 < OS_RAM_BASE + OS_RAM_SIZE) {
        size_t offset = address - OS_RAM_BASE;
        os_ram[offset] = (uint8_t)(value >> 8);
        os_ram[offset + 1] = (uint8_t)value;
        return;
    }
    trace_access(address, value & 0xFFFF, 2, 1);
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
    address &= 0xFFFFFF;
    if (address >= ES5505_BASE && address + 3 < ES5505_BASE + ES5505_SIZE) {
        es5505_write16(address, (uint16_t)(value >> 16));
        es5505_write16(address + 2, (uint16_t)value);
        return;
    }
    if (address >= 0x240000 && address + 3 < 0x240100) {
        dmac_write8(address, (uint8_t)(value >> 24));
        dmac_write8(address + 1, (uint8_t)(value >> 16));
        dmac_write8(address + 2, (uint8_t)(value >> 8));
        dmac_write8(address + 3, (uint8_t)value);
        return;
    }
    if (address + 3 < LOW_RAM_SIZE && function_code != 6) {
        low_ram[address] = (uint8_t)(value >> 24);
        low_ram[address + 1] = (uint8_t)(value >> 16);
        low_ram[address + 2] = (uint8_t)(value >> 8);
        low_ram[address + 3] = (uint8_t)value;
        return;
    }
    if (address >= SAMPLE_RAM_BASE && address + 3 < SAMPLE_RAM_BASE + SAMPLE_RAM_SIZE) {
        size_t offset = address - SAMPLE_RAM_BASE;
        sample_ram[offset] = (uint8_t)(value >> 24);
        sample_ram[offset + 1] = (uint8_t)(value >> 16);
        sample_ram[offset + 2] = (uint8_t)(value >> 8);
        sample_ram[offset + 3] = (uint8_t)value;
        sample_ram_write_bytes += 4;
        return;
    }
    if (address >= OS_RAM_BASE && address + 3 < OS_RAM_BASE + OS_RAM_SIZE) {
        size_t offset = address - OS_RAM_BASE;
        os_ram[offset] = (uint8_t)(value >> 24);
        os_ram[offset + 1] = (uint8_t)(value >> 16);
        os_ram[offset + 2] = (uint8_t)(value >> 8);
        os_ram[offset + 3] = (uint8_t)value;
        return;
    }
    trace_access(address, value, 4, 1);
}

static int load_rom(const char *path) {
    FILE *input = fopen(path, "rb");
    if (!input) {
        perror(path);
        return 0;
    }
    size_t bytes = fread(rom, 1, sizeof(rom), input);
    int extra = fgetc(input);
    fclose(input);
    if (bytes != sizeof(rom) || extra != EOF) {
        fprintf(stderr, "expected exactly %u ROM bytes, got %zu or more\n", ROM_SIZE, bytes);
        return 0;
    }
    return 1;
}

static int load_split_rom(const char *upper_path, const char *lower_path) {
    const size_t chip_size = ROM_SIZE / 2;
    uint8_t *upper = (uint8_t *)malloc(chip_size);
    uint8_t *lower = (uint8_t *)malloc(chip_size);
    if (!upper || !lower) {
        fprintf(stderr, "could not allocate split ROM buffers\n");
        free(upper);
        free(lower);
        return 0;
    }

    const char *paths[2] = {upper_path, lower_path};
    uint8_t *chips[2] = {upper, lower};
    for (size_t chip = 0; chip < 2; ++chip) {
        FILE *input = fopen(paths[chip], "rb");
        if (!input) {
            perror(paths[chip]);
            free(upper);
            free(lower);
            return 0;
        }
        size_t bytes = fread(chips[chip], 1, chip_size, input);
        int extra = fgetc(input);
        fclose(input);
        if (bytes != chip_size || extra != EOF) {
            fprintf(stderr,
                    "expected exactly %zu bytes in split ROM, got %zu or more\n",
                    chip_size, bytes);
            free(upper);
            free(lower);
            return 0;
        }
    }

    for (size_t index = 0; index < chip_size; ++index) {
        rom[index * 2] = upper[index];
        rom[index * 2 + 1] = lower[index];
    }
    free(upper);
    free(lower);

    const uint32_t initial_sp = ((uint32_t)rom[0] << 24) |
                                ((uint32_t)rom[1] << 16) |
                                ((uint32_t)rom[2] << 8) | rom[3];
    const uint32_t reset_pc = ((uint32_t)rom[4] << 24) |
                              ((uint32_t)rom[5] << 16) |
                              ((uint32_t)rom[6] << 8) | rom[7];
    if ((initial_sp & 1U) || reset_pc < ROM_BASE ||
        reset_pc >= ROM_BASE + ROM_SIZE) {
        fprintf(stderr,
                "split ROM has invalid vectors (SP=%08x PC=%08x); "
                "U28/U27 may be reversed\n",
                initial_sp, reset_pc);
        return 0;
    }
    return 1;
}

static int load_disk(const char *path) {
    char error[256];
    Eps16DiskFormat format;
    if (!eps16_disk_load_physical(path, disk_image, sizeof(disk_image),
                                  disk_sector_status,
                                  sizeof(disk_sector_status), &format,
                                  error, sizeof(error))) {
        fprintf(stderr, "%s: %s\n", path, error);
        return 0;
    }
    unsigned int unreadable = 0;
    for (size_t block = 0; block < EPS16_LOGICAL_SECTOR_COUNT; ++block)
        unreadable += disk_sector_status[block] != EPS16_SECTOR_READABLE;
    const char *format_name = format == EPS16_DISK_HFE ? "HFE" :
                              format == EPS16_DISK_EFE ? "EFE" : "IMG";
    printf("disk_input=%s format=%s logical_bytes=%u unreadable_sectors=%u\n",
           path, format_name, DISK_SIZE,
           unreadable);
    disk_loaded = 1;
    return 1;
}

static int parse_panel_script(const char *text, PanelScriptEvent *events, size_t *count) {
    *count = 0;
    if (!text || !*text) return 1;
    char *copy = malloc(strlen(text) + 1);
    if (!copy) return 0;
    strcpy(copy, text);
    char *entry = strtok(copy, ",");
    while (entry) {
        if (*count == PANEL_SCRIPT_EVENTS) {
            free(copy);
            return 0;
        }
        char *separator = strchr(entry, ':');
        if (!separator) {
            free(copy);
            return 0;
        }
        *separator++ = '\0';
        char *end = NULL;
        long long cycle = strtoll(entry, &end, 10);
        if (cycle < 0 || !end || *end ||
            !parse_panel_event(separator, events[*count].bytes, &events[*count].length)) {
            free(copy);
            return 0;
        }
        events[*count].cycle = cycle;
        events[*count].injected = 0;
        ++*count;
        entry = strtok(NULL, ",");
    }
    free(copy);
    return 1;
}

static int hex_nibble(int value) {
    if (value >= '0' && value <= '9') return value - '0';
    value = tolower((unsigned char)value);
    return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}

static int parse_panel_event(const char *text, uint8_t *event, size_t *length) {
    size_t chars = strlen(text);
    if ((chars & 1) || chars / 2 > PANEL_EVENT_SIZE) return 0;
    for (size_t index = 0; index < chars; index += 2) {
        int high = hex_nibble(text[index]);
        int low = hex_nibble(text[index + 1]);
        if (high < 0 || low < 0) return 0;
        event[index / 2] = (uint8_t)((high << 4) | low);
    }
    *length = chars / 2;
    return 1;
}

int main(int argc, char **argv) {
    rom_probe_state_defaults(rom_probe_state);
    const char *scsi_image_path = getenv("EPS16_SCSI_CD");
    if (scsi_image_path && *scsi_image_path &&
        !scsi_mount_image(scsi_image_path)) {
        fprintf(stderr, "SCSI CD image could not be opened: %s\n",
                scsi_image_path);
        return 1;
    }
    if (argc < 2 || argc > 6) {
        fprintf(stderr,
                "usage: %s COMBINED_ROM [CYCLES=200000] [LOGICAL_DISK_IMG] "
                "[PANEL_EVENT_HEX] [INJECT_CYCLE]\n", argv[0]);
        return 2;
    }
    int cycles = argc >= 3 ? atoi(argv[2]) : 200000;
    if (cycles <= 0 || !load_rom(argv[1])) return 1;
    if (argc >= 4 && !load_disk(argv[3])) return 1;
    uint8_t panel_event[PANEL_EVENT_SIZE];
    size_t panel_event_length = 0;
    if (argc >= 5 && !parse_panel_event(argv[4], panel_event, &panel_event_length)) {
        fprintf(stderr, "panel event must contain an even number of hexadecimal digits\n");
        return 2;
    }
    long long inject_cycle = argc >= 6 ? atoll(argv[5]) : 250000000LL;
    int panel_event_injected = panel_event_length == 0;
    PanelScriptEvent panel_script[PANEL_SCRIPT_EVENTS];
    size_t panel_script_count = 0;
    if (!parse_panel_script(getenv("EPS16_PANEL_SCRIPT"), panel_script, &panel_script_count)) {
        fprintf(stderr, "EPS16_PANEL_SCRIPT must be comma-separated CYCLE:HEX events\n");
        return 2;
    }
    const char *swap_disk_path = getenv("EPS16_SWAP_DISK");
    const char *swap_cycle_text = getenv("EPS16_SWAP_CYCLE");
    long long swap_cycle = swap_cycle_text && *swap_cycle_text
                               ? atoll(swap_cycle_text) : -1;
    int disk_swapped = !swap_disk_path || !*swap_disk_path || swap_cycle < 0;
    kpc_legacy_init(&kpc);
    const char *kpc_rom_path = getenv("EPS16_KPC_ROM");
    const char *kpc_execute_text = getenv("EPS16_KPC_EXECUTE");
    kpc_firmware_execution = kpc_execute_text && *kpc_execute_text &&
                             strcmp(kpc_execute_text, "0");
    if (kpc_firmware_execution && (!kpc_rom_path || !*kpc_rom_path)) {
        fprintf(stderr, "EPS16_KPC_EXECUTE requires EPS16_KPC_ROM\n");
        return 1;
    }
    if (kpc_rom_path && *kpc_rom_path) {
        char error[256];
        int loaded = kpc_firmware_execution
                         ? kpc_device_load(&kpc_device, kpc_rom_path,
                                           error, sizeof(error))
                         : kpc_firmware_load(&kpc_firmware, kpc_rom_path,
                                             error, sizeof(error));
        if (!loaded) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        fprintf(stderr,
                "KPC firmware loaded: %s reset=%04x execution=%s\n",
                kpc_rom_path,
                kpc_firmware_execution ? kpc_device.firmware.reset_vector
                                       : kpc_firmware.reset_vector,
                kpc_firmware_execution ? "68hc11-experimental"
                                       : "legacy-model");
        if (kpc_firmware_execution) {
            const char *spi_receive_text = getenv("EPS16_KPC_SPI_RECEIVE");
            if (spi_receive_text && *spi_receive_text)
                kpc_device.spi_receive_value =
                    (uint8_t)strtoul(spi_receive_text, NULL, 16);
            const char *panel_tail_text = getenv("EPS16_KPC_PANEL_TAIL");
            if (panel_tail_text && *panel_tail_text)
                kpc_device.panel_tail_receive_value =
                    (uint8_t)strtoul(panel_tail_text, NULL, 16);
            const char *tail_transfers_text =
                getenv("EPS16_KPC_PANEL_TAIL_TRANSFERS");
            if (tail_transfers_text && *tail_transfers_text)
                kpc_device.panel_tail_transfers =
                    (unsigned int)strtoul(tail_transfers_text, NULL, 0);
            const char *capture_mode_text = getenv("EPS16_KPC_CAPTURE");
            const char *capture_period_text =
                getenv("EPS16_KPC_CAPTURE_PERIOD");
            if (capture_mode_text && *capture_mode_text &&
                capture_period_text && *capture_period_text)
                kpc_device_set_capture_clock(
                    &kpc_device,
                    (unsigned int)strtoul(capture_mode_text, NULL, 0),
                    strtoull(capture_period_text, NULL, 0));
            else
                kpc_device_set_capture_clock(&kpc_device, 2, 40000);
        }
    }
    const char *kpc_poll_text = getenv("EPS16_KPC_POLL_CYCLES");
    if (kpc_poll_text && *kpc_poll_text) {
        unsigned long long value = strtoull(kpc_poll_text, NULL, 0);
        if (value >= PANEL_BYTE_CYCLES * 2)
            kpc_legacy_set_poll_cycles(&kpc, value);
    }
    const char *kpc_e7_reply_text = getenv("EPS16_KPC_E7_REPLY");
    if (kpc_e7_reply_text && *kpc_e7_reply_text)
        kpc_legacy_set_e7_reply(&kpc,
                                (uint8_t)strtoul(kpc_e7_reply_text, NULL, 16));
    const char *audio_wav_path = getenv("EPS16_AUDIO_WAV");
    const char *audio_start_text = getenv("EPS16_AUDIO_START_CYCLE");
    const char *audio_end_text = getenv("EPS16_AUDIO_END_CYCLE");
    long long audio_start_cycle = audio_start_text && *audio_start_text
                                      ? atoll(audio_start_text) : 0;
    long long audio_end_cycle = audio_end_text && *audio_end_text
                                    ? atoll(audio_end_text) : cycles;
    FILE *audio_wav = NULL;
    uint32_t audio_wav_frames = 0;
    const uint32_t cpu_clock_rate = 10000000U;
    const uint32_t audio_wav_rate = cpu_clock_rate / (16U * 21U);
    uint64_t audio_cycle_accumulator = 0;
    long long audio_scheduled_cycle = 0;
    if (audio_wav_path && *audio_wav_path) {
        audio_wav = fopen(audio_wav_path, "wb");
        if (!audio_wav || !wav_begin(audio_wav)) {
            perror(audio_wav_path);
            if (audio_wav) fclose(audio_wav);
            return 1;
        }
    }
    live_mode = getenv("EPS16_LIVE") && *getenv("EPS16_LIVE");
    const char *sample_live_trace_path = getenv("EPS16_TRACE_SAMPLE_LIVE");
    if (sample_live_trace_path && *sample_live_trace_path) {
        sample_live_trace = fopen(sample_live_trace_path, "w");
        if (!sample_live_trace) {
            perror(sample_live_trace_path);
            return 1;
        }
        setvbuf(sample_live_trace, NULL, _IOLBF, 0);
        sample_live_log("TRACE_START");
    }
    load_trace_enabled = getenv("EPS16_TRACE_LOAD") && *getenv("EPS16_TRACE_LOAD");
    const char *kpc_autopoll_text = getenv("EPS16_KPC_AUTOPOLL");
    kpc_legacy_set_autopoll(&kpc,
        kpc_autopoll_text && *kpc_autopoll_text
            ? atoi(kpc_autopoll_text) != 0 : 1);
    if (live_mode && !live_host_start(audio_wav_rate)) {
        fprintf(stderr, "live host could not start\n");
        return 1;
    }

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
    long long executed = 0;
    unsigned int timer_irqs = 0;
    while (!live_quit && (live_mode || executed < cycles)) {
        current_cycle = executed;
        duart_service_time((uint64_t)executed);
        if (kpc_firmware_execution)
            kpc_execution_service((uint64_t)executed);
        if (live_mode) live_service();
        int budget = live_mode || cycles - executed > 50000
                         ? 50000 : (int)(cycles - executed);
        uint64_t next_cycle = (uint64_t)executed + (uint64_t)budget;
        if (duart_timer_running && duart_timer_next_cycle < next_cycle)
            next_cycle = duart_timer_next_cycle;
        if (panel_wire_count && panel_rx_count < PANEL_RX_SIZE &&
            panel_wire[panel_wire_read].cycle < next_cycle)
            next_cycle = panel_wire[panel_wire_read].cycle;
        if (!duart_tx_a_ready && duart_tx_a_ready_cycle < next_cycle)
            next_cycle = duart_tx_a_ready_cycle;
        if (!duart_tx_b_ready && duart_tx_b_ready_cycle < next_cycle)
            next_cycle = duart_tx_b_ready_cycle;
        if (next_cycle > (uint64_t)executed)
            budget = (int)(next_cycle - (uint64_t)executed);
        else budget = 1;
        int slice = m68k_execute(budget);
        executed += slice;
        current_cycle = executed;
        duart_service_time((uint64_t)executed);
        if (!panel_event_injected && executed >= inject_cycle) {
            if (kpc_firmware_execution) {
                if (!kpc_execution_panel_packet(panel_event,
                                                panel_event_length,
                                                (uint64_t)executed)) {
                    fprintf(stderr, "KPC physical panel event rejected\n");
                    return 1;
                }
            } else {
                for (size_t index = 0; index < panel_event_length; ++index)
                    panel_schedule_at(panel_event[index],
                                      (uint64_t)executed);
            }
            panel_event_injected = 1;
        }
        if (!disk_swapped && executed >= swap_cycle) {
            if (!load_disk(swap_disk_path)) return 1;
            disk_change_pending = 1;
            fdc_track = 0;
            fdc_physical_track = 0;
            fdc_sector = 0;
            fdc_remaining = 0;
            disk_swapped = 1;
        }
        for (size_t event_index = 0; event_index < panel_script_count; ++event_index) {
            PanelScriptEvent *event = &panel_script[event_index];
            if (!event->injected && executed >= event->cycle) {
                if (kpc_firmware_execution) {
                    if (!kpc_execution_panel_packet(event->bytes,
                                                    event->length,
                                                    (uint64_t)executed)) {
                        fprintf(stderr,
                                "KPC physical panel script event rejected\n");
                        return 1;
                    }
                } else {
                    for (size_t byte_index = 0;
                         byte_index < event->length; ++byte_index)
                        panel_schedule_at(event->bytes[byte_index],
                                          (uint64_t)executed);
                }
                event->injected = 1;
            }
        }
        if (!kpc_firmware_execution &&
            kpc_legacy_service(&kpc, live_mode, fdc_reads,
                               (uint64_t)executed,
                               !panel_rx_count && !panel_wire_count)) {
            if (getenv("EPS16_TRACE_KPC_FF") && sample_record_input_valid_start)
                fprintf(stderr, "kpc_ff source:service cycle:%lld\n", current_cycle);
            sample_live_log("SCHEDULE_READY source=service cycle=%lld", executed);
            panel_schedule_at(0xff, (uint64_t)executed);
        }
        dmac_service();
        if (dmac_irq_channel >= 0) {
            m68k_set_irq(2);
            current_cycle = executed;
            executed += m68k_execute(128);
            current_cycle = executed;
            duart_service_time((uint64_t)executed);
            m68k_set_irq(0);
        }
        /* Cycle-timed MC68681 counter/timer and channel-B receive IRQ. */
        if (duart_interrupt_status() & duart_registers[5]) {
            m68k_set_irq(3);
            current_cycle = executed;
            executed += m68k_execute(128);
            current_cycle = executed;
            duart_service_time((uint64_t)executed);
            m68k_set_irq(0);
            ++timer_irqs;
        }
        uint64_t elapsed_audio_cycles = (uint64_t)(executed - audio_scheduled_cycle);
        audio_scheduled_cycle = executed;
        audio_cycle_accumulator += elapsed_audio_cycles * audio_wav_rate;
        uint64_t frames_due = audio_cycle_accumulator / cpu_clock_rate;
        audio_cycle_accumulator %= cpu_clock_rate;
        while (frames_due) {
            int32_t buses[ES5505_STEREO_BUSES * 2][64];
            int32_t *bus_outputs[ES5505_STEREO_BUSES * 2];
            int16_t live_pcm[128];
            size_t chunk = frames_due > 64 ? 64 : (size_t)frames_due;
            for (unsigned int output = 0;
                 output < ES5505_STEREO_BUSES * 2; ++output)
                bus_outputs[output] = buses[output];
            es5505_core_render_buses(&es5505, bus_outputs, chunk);
            audio_frames += chunk;
            frames_due -= chunk;
            for (size_t frame = 0; frame < chunk; ++frame) {
                for (unsigned int bus = 0; bus < ES5505_STEREO_BUSES; ++bus) {
                    int32_t left = buses[bus * 2][frame];
                    int32_t right = buses[bus * 2 + 1][frame];
                    int32_t magnitude = left < 0 ? -left : left;
                    if (magnitude > es5505_bus_peak[bus])
                        es5505_bus_peak[bus] = magnitude;
                    magnitude = right < 0 ? -right : right;
                    if (magnitude > es5505_bus_peak[bus])
                        es5505_bus_peak[bus] = magnitude;
                }
                /* EPS-16 signal pump: the original OS configures SER1 as the
                   output and SER0/SER2/SER3 as inputs (Host Serial Control
                   0x48).  ES5505 Bus1..3 feed those three input ports; the
                   fourth ES5505 assignment remains the separate Aux1 bus. */
                const int16_t esp_inputs[8] = {
                    audio_to_pcm16(buses[0][frame]),
                    audio_to_pcm16(buses[1][frame]),
                    0, 0,
                    audio_to_pcm16(buses[2][frame]),
                    audio_to_pcm16(buses[3][frame]),
                    audio_to_pcm16(buses[4][frame]),
                    audio_to_pcm16(buses[5][frame])
                };
                int16_t esp_outputs[2] = {0, 0};
                if (es5510_host_upload_active &&
                    (uint64_t)current_cycle >= es5510_host_access_until) {
                    es5510_host_upload_active = 0;
                    es5510_core_set_halted(&es5510, 0);
                }
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
                int32_t output_left = apply_master_volume((int32_t)esp_outputs[0] << 4);
                int32_t output_right = apply_master_volume((int32_t)esp_outputs[1] << 4);
                int32_t magnitude = output_left < 0 ? -output_left : output_left;
                if (magnitude > audio_peak) audio_peak = magnitude;
                magnitude = output_right < 0 ? -output_right : output_right;
                if (magnitude > audio_peak) audio_peak = magnitude;
                live_pcm[frame * 2] = audio_to_pcm16(output_left);
                live_pcm[frame * 2 + 1] = audio_to_pcm16(output_right);
                if (audio_wav && current_cycle >= audio_start_cycle &&
                    current_cycle <= audio_end_cycle) {
                    write_le16(audio_wav, (uint16_t)audio_to_pcm16(output_left));
                    write_le16(audio_wav, (uint16_t)audio_to_pcm16(output_right));
                    ++audio_wav_frames;
                }
            }
            if (live_mode) live_host_write(live_pcm, chunk);
        }
        if (es5505_core_irq_pending(&es5505)) {
            m68k_set_irq(1);
            current_cycle = executed;
            executed += m68k_execute(128);
            current_cycle = executed;
            duart_service_time((uint64_t)executed);
            m68k_set_irq(0);
            ++es5505_irqs;
        }
        if (slice <= 0 && timer_irqs > 100000) break;
    }
    if (live_mode) live_host_stop();
    if (getenv("EPS16_TRACE_ES5510_PROGRAM")) {
        for (unsigned int pc = 0; pc < ES5510_INSTRUCTIONS; ++pc)
            printf("es5510_program[%03u]=%012llx gpr=%06x\n", pc,
                   (unsigned long long)(es5510_instruction[pc] &
                                        UINT64_C(0xffffffffffff)),
                   es5510_gpr[pc] & 0xffffff);
    }
    printf("executed=%lld pc=0x%06x sp=0x%06x sr=0x%04x traces=%zu timer_irqs=%u\n",
           executed,
           m68k_get_reg(NULL, M68K_REG_PC),
           m68k_get_reg(NULL, M68K_REG_SP),
           m68k_get_reg(NULL, M68K_REG_SR),
           trace_count, timer_irqs);
    struct { uint32_t pc, count; } hottest[12] = {{0, 0}};
    for (size_t index = 0; index < ROM_SIZE / 2; ++index) {
        uint32_t count = postboot_pc_counts[index];
        if (count <= hottest[11].count) continue;
        size_t position = 11;
        while (position && count > hottest[position - 1].count) {
            hottest[position] = hottest[position - 1];
            --position;
        }
        hottest[position].pc = ROM_BASE + (uint32_t)(index << 1);
        hottest[position].count = count;
    }
    printf("postboot_instructions=%llu hot_pcs=", (unsigned long long)postboot_instruction_count);
    for (size_t index = 0; index < 12 && hottest[index].count; ++index)
        printf("%s%06x:%u", index ? "," : "", hottest[index].pc, hottest[index].count);
    putchar('\n');
    printf("postboot_regions=rom:%llu low:%llu sample:%llu os:%llu other:%llu\n",
           (unsigned long long)postboot_rom_instructions,
           (unsigned long long)postboot_low_ram_instructions,
           (unsigned long long)postboot_sample_ram_instructions,
           (unsigned long long)postboot_os_ram_instructions,
           (unsigned long long)postboot_other_instructions);
    memset(hottest, 0, sizeof(hottest));
    for (size_t index = 0; index < OS_RAM_SIZE / 2; ++index) {
        uint32_t count = postboot_os_pc_counts[index];
        if (count <= hottest[11].count) continue;
        size_t position = 11;
        while (position && count > hottest[position - 1].count) {
            hottest[position] = hottest[position - 1];
            --position;
        }
        hottest[position].pc = OS_RAM_BASE + (uint32_t)(index << 1);
        hottest[position].count = count;
    }
    printf("postboot_os_hot_pcs=");
    for (size_t index = 0; index < 12 && hottest[index].count; ++index)
        printf("%s%06x:%u", index ? "," : "", hottest[index].pc, hottest[index].count);
    putchar('\n');
    for (size_t index = 0; index < trace_count; ++index) {
        Trace *trace = &traces[index];
        printf("%c%u 0x%06x value=0x%0*x count=%u\n",
               trace->write ? 'W' : 'R', trace->width * 8,
               trace->address, trace->width * 2, trace->value, trace->count);
    }
    printf("panel_tx_count=%zu panel_tx=", panel_tx_count);
    size_t shown = panel_tx_count < 128 ? panel_tx_count : 128;
    for (size_t index = 0; index < shown; ++index) printf("%02x", panel_tx[index]);
    if (shown < panel_tx_count) printf("...");
    putchar('\n');
    printf("panel_ascii=");
    for (size_t index = 0; index < panel_tx_count; ++index) {
        uint8_t value = panel_tx[index];
        putchar(value >= 32 && value < 127 ? value : '.');
    }
    putchar('\n');
    printf("panel_display=|%s|\n", panel_display);
    printf("panel_milestones=pick_instrument:%d file_loaded:%d\n",
           panel_pick_instrument_seen, panel_file_loaded_seen);
    printf("panel_rx_pending=%zu panel_rx_consumed=%zu event_injected=%d\n",
           panel_rx_count, panel_rx_consumed, panel_event_injected);
    printf("panel_parser=handler:%02x%02x phase:%02x lock:%02x\n",
           low_ram[0x0ad6], low_ram[0x0ad7], low_ram[0x0ada], low_ram[0x0adb]);
    printf("duart_imr=0x%02x duart_isr=0x%02x duart_ivr=0x%02x duart_srb=0x%02x duart_output=0x%02x "
           "acr=0x%02x ct=0x%02x%02x\n",
           duart_registers[5], duart_interrupt_status(), duart_registers[12],
           (duart_tx_b_enabled && duart_tx_b_ready ? 0x0c : 0x00) |
               (panel_rx_count ? 0x01 : 0x00),
           duart_output & 0xff, duart_registers[4], duart_registers[6], duart_registers[7]);
    printf("midi_tx_enabled=%d ready=%d midi_tx_count=%zu midi_tx=",
           duart_tx_a_enabled, duart_tx_a_ready, midi_tx_count);
    for (size_t index = 0; index < midi_tx_count; ++index) printf("%02x", midi_tx[index]);
    putchar('\n');
    printf("illegal_instructions=%zu", illegal_instruction_count);
    for (size_t index = 0; index < illegal_instruction_count; ++index) {
        const IllegalInstruction *event = &illegal_instructions[index];
        printf("%s%06x:%04x@%lld", index ? "," : " ", event->pc,
               event->opcode, event->cycle);
    }
    putchar('\n');
    if (getenv("EPS16_TRACE_IRQ")) {
        for (size_t index = 0; index < irq_trace_count; ++index)
            printf("irq_trace[%zu]=level:%u vector:%02x pc:%06x cycle:%lld\n",
                   index, irq_trace[index].level, irq_trace[index].vector,
                   irq_trace[index].pc, irq_trace[index].cycle);
    }
    if (fatal_history_captured) {
        printf("fatal_history=");
        for (size_t index = 0; index < 64; ++index)
            printf("%s%06x", index ? "," : "", fatal_history[index]);
        putchar('\n');
    }
    printf("panel_script_events=%zu disk_swapped=%d swap_cycle=%lld\n",
           panel_script_count, disk_swapped, swap_cycle);
    if (kpc_firmware_execution) {
        printf("kpc_cpu=pc:%04x cycles:%llu illegal:%d "
               "e4:%02x e5:%02x timer:%04x spcr:%02x sccr2:%02x baud:%02x "
               "txbusy:%u txpending:%d hprio:%02x tmsk1:%02x tflg1:%02x "
               "spi_count:%zu captures:%u\n",
               kpc_device.cpu.pc,
               (unsigned long long)kpc_device.cpu.cycles,
               kpc_device.cpu.illegal,
               kpc_device.writable[0x00e4],
               kpc_device.writable[0x00e5],
               kpc_device.timer_counter,
               kpc_device.writable[0x1028],
               kpc_device.writable[0x102d],
               kpc_device.writable[0x102b],
               kpc_device.sci_tx_cycles_remaining,
               kpc_device.sci_tx_pending,
               kpc_device.writable[0x103c],
               kpc_device.writable[0x1022],
               kpc_device.writable[0x1023],
               kpc_device.spi_recent_count,
               kpc_device.capture_count);
        printf("kpc_spi_recent=");
        size_t recent = kpc_device.spi_recent_count < 64
                            ? kpc_device.spi_recent_count : 64;
        size_t start = kpc_device.spi_recent_count - recent;
        for (size_t index = 0; index < recent; ++index) {
            size_t slot = (start + index) & 0xff;
            printf("%02x/%02x%s", kpc_device.spi_recent_tx[slot],
                   kpc_device.spi_recent_rx[slot],
                   index + 1 == recent ? "" : ",");
        }
        putchar('\n');
        printf("kpc_sci_rx=");
        size_t sci_rx_start = kpc_device.sci_rx_total > 128
                                  ? kpc_device.sci_rx_total - 128 : 0;
        for (size_t index = sci_rx_start; index < kpc_device.sci_rx_total;
             ++index)
            printf("%s%02x", index == sci_rx_start ? "" : ",",
                   kpc_device.sci_recent_rx[index & 0xff]);
        printf(" total:%zu\nkpc_sci_tx=", kpc_device.sci_rx_total);
        size_t sci_tx_start = kpc_device.sci_tx_total > 128
                                  ? kpc_device.sci_tx_total - 128 : 0;
        for (size_t index = sci_tx_start; index < kpc_device.sci_tx_total;
             ++index)
            printf("%s%02x", index == sci_tx_start ? "" : ",",
                   kpc_device.sci_recent_tx[index & 0xff]);
        printf(" total:%zu\n", kpc_device.sci_tx_total);
    }
    if (getenv("EPS16_TRACE_PANEL")) {
        for (size_t index = 0; index < panel_trace_count; ++index) {
            const PanelTrace *event = &panel_trace[index];
            printf("panel_trace[%zu]=%c value:%02x pc:%06x cycle:%lld\n",
                   index, event->direction, event->value, event->pc, event->cycle);
        }
    }
    printf("es5505_page=%u active_voices=%u irq_vector=0x%02x irqs=%u audio_frames=%llu peak=%d\n",
           es5505.page, es5505.active_voice, es5505.irq_vector,
           es5505_irqs, (unsigned long long)audio_frames, audio_peak);
    printf("audio_route_peaks=bus1:%d,bus2:%d,bus3:%d,aux:%d,esp_out:%d\n",
           es5505_bus_peak[0], es5505_bus_peak[1], es5505_bus_peak[2],
           es5505_bus_peak[3], es5510_return_peak);
    unsigned int es5505_running_voices = 0;
    for (unsigned int index = 0; index < ES5505_VOICES; ++index)
        if (!(es5505.voices[index].control & ES5505_STOP_MASK))
            ++es5505_running_voices;
    printf("es5505_writes=%llu running_voices=%u\n",
           (unsigned long long)es5505_writes, es5505_running_voices);
    if (getenv("EPS16_TRACE_VOICES")) {
        for (unsigned int index = 0; index < ES5505_VOICES; ++index) {
            const Es5505Voice *voice = &es5505.voices[index];
            printf("voice[%u]=control:%04x freq:%08x start:%08x end:%08x accum:%08x "
                   "k1:%04x k2:%04x left:%02x right:%02x\n",
                   index, voice->control, voice->frequency, voice->start,
                   voice->end, voice->accumulator, voice->k1, voice->k2,
                   voice->left_volume, voice->right_volume);
        }
    }
    size_t sample_ram_nonzero = 0;
    uint32_t sample_ram_fnv1a = 2166136261U;
    for (size_t index = 0; index < SAMPLE_RAM_SIZE; ++index) {
        if (sample_ram[index]) ++sample_ram_nonzero;
        sample_ram_fnv1a = (sample_ram_fnv1a ^ sample_ram[index]) * 16777619U;
    }
    printf("sample_ram=write_bytes:%llu nonzero:%zu fnv1a:%08x\n",
           (unsigned long long)sample_ram_write_bytes,
           sample_ram_nonzero, sample_ram_fnv1a);
    printf("analog_reads=");
    for (size_t channel = 0; channel < 8; ++channel)
        printf("%s%zu:%u", channel ? "," : "", channel, analog_reads[channel]);
    putchar('\n');
    printf("es5510_dram_reads=%u dram_writes=%u gpr_writes=%u instruction_writes=%u\n",
           es5510_dram_reads, es5510_dram_writes,
           es5510_gpr_writes, es5510_instruction_writes);
    printf("es5510_frames=%llu instructions=%llu halted=%d\n",
           (unsigned long long)es5510.frames,
           (unsigned long long)es5510.instructions_executed,
           es5510.halted);
    printf("es5510_host_serial=%02x writes=%u\n",
           es5510_host_serial, es5510_host_serial_writes);
    if (es5510_verify_failures)
        printf("es5510_verify_failures=%u type=%02x register=%02x expected=%02x actual=%02x "
               "source=%06x target=%06x\n",
               es5510_verify_failures, es5510_verify_d3 & 0xff,
               es5510_verify_d1 & 0xff, es5510_verify_d2 & 0xff,
               es5510_verify_actual, es5510_verify_a3, es5510_verify_a6);
    printf("dmac_transfers=%u dmac_irq_channel=%d\n", dmac_transfers, dmac_irq_channel);
    for (unsigned int channel = 0; channel < 4; ++channel) {
        printf("dmac_channel[%u]=csr:%02x cer:%02x dcr:%02x ocr:%02x scr:%02x ccr:%02x mtc:%04x mar:%08x dar:%08x niv:%02x eiv:%02x\n",
               channel, dmac_registers[channel][0], dmac_registers[channel][1],
               dmac_registers[channel][4], dmac_registers[channel][5],
               dmac_registers[channel][6], dmac_registers[channel][7],
               dmac_get16(channel, 0x0a), dmac_get32(channel, 0x0c),
               dmac_get32(channel, 0x14), dmac_registers[channel][0x25],
               dmac_registers[channel][0x27]);
    }
    if (getenv("EPS16_TRACE_DMA")) {
        for (size_t index = 0; index < dma_trace_count; ++index) {
            const DmaTrace *event = &dma_trace[index];
            printf("dma_trace[%zu]=%c channel:%u reg:%02x value:%02x pc:%06x cycle:%lld\n",
                   index, event->kind, event->channel, event->reg, event->value,
                   event->pc, event->cycle);
        }
    }
    printf("disk_loaded=%d fdc_reads=%u fdc_last_command=0x%02x fdc_track=%u physical_track=%u fdc_sector=%u remaining=%zu side=%u\n",
           disk_loaded, fdc_reads, fdc_last_command, fdc_track, fdc_physical_track,
           fdc_sector, fdc_remaining,
           floppy_side());
    printf("fdc_data_reads=%u fdc_read_pcs=", fdc_data_reads);
    for (size_t index = 0; index < fdc_read_pc_count; ++index)
        printf("%s%06x", index ? "," : "", fdc_read_pcs[index]);
    putchar('\n');
    for (size_t index = 0; index < fdc_event_count; ++index) {
        const FdcEvent *event = &fdc_events[index];
        printf("fdc_event[%zu]=cmd:%02x track:%u sector:%u side:%u block:%u dest:%06x\n",
               index, event->command, event->track, event->sector,
               event->side, event->block, event->destination);
    }
    const char *dump_path = getenv("EPS16_DUMP_OS_RAM");
    if (dump_path && *dump_path) {
        FILE *dump = fopen(dump_path, "wb");
        if (!dump || fwrite(os_ram, 1, sizeof(os_ram), dump) != sizeof(os_ram)) {
            perror(dump_path);
            if (dump) fclose(dump);
            return 1;
        }
        fclose(dump);
    }
    dump_path = getenv("EPS16_DUMP_LOW_RAM");
    if (dump_path && *dump_path) {
        FILE *dump = fopen(dump_path, "wb");
        if (!dump || fwrite(low_ram, 1, sizeof(low_ram), dump) != sizeof(low_ram)) {
            perror(dump_path);
            if (dump) fclose(dump);
            return 1;
        }
        fclose(dump);
    }
    dump_path = getenv("EPS16_DUMP_SAMPLE_RAM");
    if (dump_path && *dump_path) {
        FILE *dump = fopen(dump_path, "wb");
        if (!dump || fwrite(sample_ram, 1, sizeof(sample_ram), dump) != sizeof(sample_ram)) {
            perror(dump_path);
            if (dump) fclose(dump);
            return 1;
        }
        fclose(dump);
    }
    if (audio_wav) {
        if (!wav_finish(audio_wav, audio_wav_frames, audio_wav_rate) || fclose(audio_wav)) {
            perror(audio_wav_path);
            return 1;
        }
        printf("audio_wav=%s frames=%u rate=%u\n",
               audio_wav_path, audio_wav_frames, audio_wav_rate);
    }
    if (sample_live_trace) fclose(sample_live_trace);
    if (scsi_image) fclose(scsi_image);
    return 0;
}
