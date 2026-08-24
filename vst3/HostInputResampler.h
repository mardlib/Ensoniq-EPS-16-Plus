#ifndef EPS16_VST3_HOST_INPUT_RESAMPLER_H
#define EPS16_VST3_HOST_INPUT_RESAMPLER_H

#include <stddef.h>
#include <stdint.h>

enum {
    EPS16_HOST_INPUT_TAPS = 48,
    EPS16_HOST_INPUT_PHASES = 512,
    EPS16_HOST_INPUT_HISTORY = 256
};

typedef struct {
    double host_rate;
    uint64_t cpu_rate;
    uint64_t origin_cycle;
    uint64_t samples_pushed;
    float history[EPS16_HOST_INPUT_HISTORY];
    size_t history_write;
    size_t history_count;
    float coefficients[EPS16_HOST_INPUT_PHASES + 1][EPS16_HOST_INPUT_TAPS];
    int configured;
} Eps16HostInputResampler;

int eps16_host_input_resampler_prepare(Eps16HostInputResampler *resampler,
                                       double host_rate,
                                       uint64_t cpu_rate);
void eps16_host_input_resampler_reset(Eps16HostInputResampler *resampler);
void eps16_host_input_resampler_push(Eps16HostInputResampler *resampler,
                                     uint64_t cpu_cycle, float sample);
int eps16_host_input_resampler_sample(const Eps16HostInputResampler *resampler,
                                      uint64_t cpu_cycle, float *sample);

#endif
