#include "HostInputResampler.h"

#include <math.h>
#include <string.h>

enum { EPS16_CPU_CLOCK_HZ = 10000000 };

static double normalized_sinc(double value) {
    if (fabs(value) < 1.0e-12) return 1.0;
    const double angle = 3.14159265358979323846 * value;
    return sin(angle) / angle;
}

int eps16_host_input_resampler_prepare(Eps16HostInputResampler *resampler,
                                       double host_rate,
                                       uint64_t cpu_rate) {
    if (!resampler || !isfinite(host_rate) || host_rate < 8000.0 ||
        host_rate > 768000.0 || !cpu_rate)
        return 0;
    memset(resampler, 0, sizeof(*resampler));
    resampler->host_rate = host_rate;
    resampler->cpu_rate = cpu_rate;

    /* Reconstruct the continuous host waveform below 95 percent of host
       Nyquist. The EPS sampling-input circuit remains responsible for the
       original rate-dependent analogue anti-alias response before its ADC. */
    const double cutoff = 0.5 * 0.95;
    for (size_t phase = 0; phase <= EPS16_HOST_INPUT_PHASES; ++phase) {
        const double fraction = (double)phase / EPS16_HOST_INPUT_PHASES;
        double sum = 0.0;
        for (size_t tap = 0; tap < EPS16_HOST_INPUT_TAPS; ++tap) {
            const int offset = (int)tap - (EPS16_HOST_INPUT_TAPS / 2 - 1);
            const double distance = fraction - offset;
            const double window_position =
                distance / (EPS16_HOST_INPUT_TAPS / 2.0);
            double weight = 0.0;
            if (fabs(window_position) <= 1.0) {
                const double window =
                    0.42 + 0.5 * cos(3.14159265358979323846 * window_position) +
                    0.08 * cos(2.0 * 3.14159265358979323846 * window_position);
                weight = 2.0 * cutoff *
                         normalized_sinc(2.0 * cutoff * distance) * window;
            }
            resampler->coefficients[phase][tap] = (float)weight;
            sum += weight;
        }
        if (fabs(sum) > 1.0e-12)
            for (size_t tap = 0; tap < EPS16_HOST_INPUT_TAPS; ++tap)
                resampler->coefficients[phase][tap] =
                    (float)(resampler->coefficients[phase][tap] / sum);
    }
    resampler->configured = 1;
    return 1;
}

void eps16_host_input_resampler_reset(Eps16HostInputResampler *resampler) {
    if (!resampler) return;
    resampler->origin_cycle = 0;
    resampler->samples_pushed = 0;
    resampler->history_write = 0;
    resampler->history_count = 0;
    memset(resampler->history, 0, sizeof(resampler->history));
}

void eps16_host_input_resampler_push(Eps16HostInputResampler *resampler,
                                     uint64_t cpu_cycle, float sample) {
    if (!resampler || !resampler->configured) return;
    if (!resampler->samples_pushed) resampler->origin_cycle = cpu_cycle;
    resampler->history[resampler->history_write] = sample;
    resampler->history_write =
        (resampler->history_write + 1) % EPS16_HOST_INPUT_HISTORY;
    if (resampler->history_count < EPS16_HOST_INPUT_HISTORY)
        ++resampler->history_count;
    ++resampler->samples_pushed;
}

static int history_sample(const Eps16HostInputResampler *resampler,
                          int64_t absolute_index, float *sample) {
    const uint64_t oldest =
        resampler->samples_pushed - resampler->history_count;
    if (absolute_index < 0 || (uint64_t)absolute_index < oldest ||
        (uint64_t)absolute_index >= resampler->samples_pushed)
        return 0;
    const size_t oldest_slot =
        (resampler->history_write + EPS16_HOST_INPUT_HISTORY -
         resampler->history_count) % EPS16_HOST_INPUT_HISTORY;
    const size_t offset = (size_t)((uint64_t)absolute_index - oldest);
    *sample = resampler->history[
        (oldest_slot + offset) % EPS16_HOST_INPUT_HISTORY];
    return 1;
}

int eps16_host_input_resampler_sample(const Eps16HostInputResampler *resampler,
                                      uint64_t cpu_cycle, float *sample) {
    if (!resampler || !sample || !resampler->configured ||
        resampler->history_count < EPS16_HOST_INPUT_TAPS ||
        cpu_cycle < resampler->origin_cycle)
        return 0;

    const double host_position =
        (double)(cpu_cycle - resampler->origin_cycle) *
        resampler->host_rate / (double)resampler->cpu_rate;
    /* A symmetric reconstruction filter requires future samples. Delay the
       external input by 24 host frames so every ADC query remains causal;
       no EPS clock or recorded sample-rate metadata is changed. */
    const double position =
        host_position - (double)(EPS16_HOST_INPUT_TAPS / 2);
    const int64_t base = (int64_t)floor(position);
    const double fraction = position - floor(position);
    const double exact_phase = fraction * EPS16_HOST_INPUT_PHASES;
    size_t phase = (size_t)floor(exact_phase);
    if (phase >= EPS16_HOST_INPUT_PHASES) phase = EPS16_HOST_INPUT_PHASES - 1;
    const double phase_fraction = exact_phase - phase;

    double result = 0.0;
    for (size_t tap = 0; tap < EPS16_HOST_INPUT_TAPS; ++tap) {
        const int offset = (int)tap - (EPS16_HOST_INPUT_TAPS / 2 - 1);
        float value = 0.0f;
        if (!history_sample(resampler, base + offset, &value)) return 0;
        const double coefficient =
            resampler->coefficients[phase][tap] + phase_fraction *
            (resampler->coefficients[phase + 1][tap] -
             resampler->coefficients[phase][tap]);
        result += value * coefficient;
    }
    *sample = (float)result;
    return 1;
}
