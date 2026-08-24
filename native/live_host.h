#ifndef EPS16_LIVE_HOST_H
#define EPS16_LIVE_HOST_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} LiveMidiEvent;

int live_host_start(uint32_t sample_rate);
void live_host_stop(void);
void live_host_write(const int16_t *interleaved_stereo, size_t frames);
int live_host_poll_line(char *line, size_t size);
int live_host_poll_midi(LiveMidiEvent *event);
int live_host_audio_input_sample(uint32_t target_rate,
                                 uint64_t conversion_cycle,
                                 int16_t *sample);
void live_host_audio_input_prepare_recording(void);
void live_host_clear_display_hold(void);
void live_host_display(const char display[23], uint32_t decimal_mask,
                       int cursor_start, int cursor_end);
void live_host_panel_tx(uint8_t value);
void live_host_adc_state(const uint16_t values[8], const unsigned int reads[8],
                         unsigned int duart_opr, unsigned int es5505_page);

#endif
