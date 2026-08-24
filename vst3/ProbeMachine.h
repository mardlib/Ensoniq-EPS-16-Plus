#ifndef EPS16_VST3_PROBE_MACHINE_H
#define EPS16_VST3_PROBE_MACHINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Eps16ProbeMachine Eps16ProbeMachine;

Eps16ProbeMachine *eps16_probe_machine_create(void);
void eps16_probe_machine_destroy(Eps16ProbeMachine *machine);
/* Selects this machine for the calling thread. All operations below must occur
   between a successful begin/end pair. Different machines may be active on
   different threads at the same time. */
int eps16_probe_machine_begin(Eps16ProbeMachine *machine);
void eps16_probe_machine_end(Eps16ProbeMachine *machine);

int eps16_probe_machine_initialize(const char *rom_path, const char *kpc_path,
                                   const char *os_disk_path,
                                   char *error, size_t error_size);
int eps16_probe_machine_initialize_split_rom(const char *upper_rom_path,
                                             const char *lower_rom_path,
                                             const char *kpc_path,
                                             const char *os_disk_path,
                                             char *error, size_t error_size);
int eps16_probe_machine_insert_disk(const char *disk_path,
                                    char *error, size_t error_size);
int eps16_probe_machine_insert_scsi_cd(const char *image_path,
                                       char *error, size_t error_size);
int eps16_probe_machine_create_blank_disk(char *error, size_t error_size);
int eps16_probe_machine_save_disk(const char *disk_path, int hfe_format,
                                  char *error, size_t error_size);
int eps16_probe_machine_is_initialized(void);
void eps16_probe_machine_run_until(uint64_t cpu_cycle);
typedef struct {
    uint64_t cpu_cycle;
    uint32_t clock_divider;
    float left;
    float right;
} Eps16ProbeAudioFrame;
size_t eps16_probe_machine_drain_audio(Eps16ProbeAudioFrame *frames,
                                       size_t capacity);
void eps16_probe_machine_midi(uint8_t status, uint8_t data1, uint8_t data2);
void eps16_probe_machine_keyboard(uint8_t note, uint8_t velocity,
                                  int pressed);
size_t eps16_probe_machine_midi_bytes(const uint8_t *bytes, size_t size);
size_t eps16_probe_machine_drain_midi_output(uint8_t *bytes, size_t capacity);
size_t eps16_probe_machine_midi_rx_consumed(void);
void eps16_probe_machine_panel_byte(uint8_t value);
void eps16_probe_machine_analog(unsigned int channel, uint16_t value);
void eps16_probe_machine_sampling_input_rate(double sample_rate);
float eps16_probe_machine_sampling_input(float left, float right);
int eps16_probe_machine_sampling_mic_input(void);
void eps16_probe_machine_stereo_output(float *left, float *right);
void eps16_probe_machine_display(char display[23]);
uint32_t eps16_probe_machine_decimal_mask(void);
uint32_t eps16_probe_machine_cursor_segment_mask(void);
uint16_t eps16_probe_machine_indicator_on(unsigned int bank);
uint16_t eps16_probe_machine_indicator_flash(unsigned int bank);
int eps16_probe_machine_sampling_monitor_active(void);
unsigned int eps16_probe_machine_master_volume(void);
unsigned int eps16_probe_machine_analog_value(unsigned int channel);
size_t eps16_probe_machine_panel_rx_consumed(void);
uint32_t eps16_probe_machine_last_keyon_frequency(void);
void eps16_probe_machine_cursor(int *start, int *end);
uint64_t eps16_probe_machine_cycles(void);
size_t eps16_probe_machine_illegal_instructions(void);
uint64_t eps16_probe_machine_sample_ram_write_bytes(void);
uint64_t eps16_probe_machine_sampling_input_conversions(void);
void eps16_probe_machine_reset_audio_peaks(void);
float eps16_probe_machine_es5505_bus_peak(unsigned int bus);
float eps16_probe_machine_es5510_return_peak(void);
float eps16_probe_machine_output_peak(void);
size_t eps16_probe_machine_state_size(void);
int eps16_probe_machine_save_state(void *data, size_t size);
int eps16_probe_machine_load_state(const void *data, size_t size,
                                   char *error, size_t error_size);

/* Read-only diagnostic access for original-OS RAM mapping probes. This never
   reads hardware registers and is not used by the plug-in GUI or audio path. */
size_t eps16_probe_machine_debug_read_ram(uint32_t address, void *data,
                                          size_t size);
/* Diagnostic-only direct RAM write used by mapping probes. The caller must
   already own the machine context; this is deliberately not a plug-in control
   API and does not emulate a CPU or peripheral write. */
size_t eps16_probe_machine_debug_write_ram(uint32_t address, const void *data,
                                           size_t size);

#ifdef __cplusplus
}
#endif

#endif
