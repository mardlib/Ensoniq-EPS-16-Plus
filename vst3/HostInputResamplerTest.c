#include "HostInputResampler.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

enum { CPU_RATE = 10000000 };

static int test_rate(unsigned int selector) {
    const double host_rate = 44100.0;
    const double input_frequency = 997.0;
    const uint64_t adc_period = 112U * selector;
    const uint64_t duration_cycles = 2U * CPU_RATE;
    Eps16HostInputResampler resampler;
    if (!eps16_host_input_resampler_prepare(&resampler, host_rate, CPU_RATE))
        return 0;

    uint64_t host_remainder = 0;
    uint64_t host_cycle = 0;
    uint64_t adc_cycle = 0;
    double squared_error = 0.0;
    double squared_signal = 0.0;
    size_t measured = 0;
    for (uint64_t host_index = 0; host_cycle < duration_cycles; ++host_index) {
        const float input = (float)sin(2.0 * 3.14159265358979323846 *
                                       input_frequency * host_index / host_rate);
        eps16_host_input_resampler_push(&resampler, host_cycle, input);
        host_remainder += CPU_RATE;
        const uint64_t next_host_cycle = host_cycle +
            host_remainder / (uint64_t)host_rate;
        host_remainder %= (uint64_t)host_rate;
        while (adc_cycle < next_host_cycle) {
            float output = 0.0f;
            if (eps16_host_input_resampler_sample(
                    &resampler, adc_cycle, &output)) {
                const double delayed_host_position =
                    (double)adc_cycle * host_rate / CPU_RATE -
                    EPS16_HOST_INPUT_TAPS / 2.0;
                const double expected = sin(2.0 * 3.14159265358979323846 *
                                             input_frequency *
                                             delayed_host_position / host_rate);
                const double error = output - expected;
                squared_error += error * error;
                squared_signal += expected * expected;
                ++measured;
            }
            adc_cycle += adc_period;
        }
        host_cycle = next_host_cycle;
    }
    const double relative_error = sqrt(squared_error / squared_signal);
    printf("selector=%u measured=%zu relative_error=%.9f (%.2f dB)\n",
           selector, measured, relative_error,
           20.0 * log10(relative_error));
    return measured > 10000 && relative_error < 0.00001;
}

int main(void) {
    for (unsigned int selector = 2; selector <= 8; ++selector)
        if (!test_rate(selector)) return 1;
    return 0;
}
