/*
 * Standalone Ensoniq ES5505 voice-chip core.
 *
 * Hardware behavior, register semantics, interpolation, filtering, looping
 * and volume handling are informed by the BSD-3-Clause MAME ES5505/ES5506
 * device by Aaron Giles and MAMEdev contributors:
 * https://github.com/mamedev/mame/blob/master/src/devices/sound/es5506.cpp
 *
 * See THIRD_PARTY_NOTICES.md for attribution and the BSD-3-Clause notice.
 */
#include "es5505_core.h"

#include <string.h>

enum { ACCUMULATOR_MASK = 0x7fffffffU, FRACTION_BITS = 11, FILTER_SHIFT = 4 };

static int32_t lowpass(int32_t sample, uint16_t cutoff, int32_t state) {
    return ((int32_t)(cutoff >> FILTER_SHIFT) * (sample - state) / 4096) + state;
}

static int32_t highpass(int32_t sample, uint16_t cutoff, int32_t state, int32_t previous) {
    return sample - previous + ((int32_t)(cutoff >> FILTER_SHIFT) * state) / 8192 + state / 2;
}

static int32_t filter_sample(Es5505Voice *voice, int32_t sample) {
    sample = lowpass(sample, voice->k1, voice->pole1);
    voice->pole1 = sample;
    sample = lowpass(sample, voice->k1, voice->pole2);
    voice->pole2_previous = voice->pole2;
    voice->pole2 = sample;
    switch ((voice->control >> 10) & 3) {
        case 0:
            sample = highpass(sample, voice->k2, voice->pole3, voice->pole2_previous);
            voice->pole3_previous = voice->pole3;
            voice->pole3 = sample;
            sample = highpass(sample, voice->k2, voice->pole4, voice->pole3_previous);
            voice->pole4 = sample;
            break;
        case 1:
            sample = lowpass(sample, voice->k1, voice->pole3);
            voice->pole3_previous = voice->pole3;
            voice->pole3 = sample;
            sample = highpass(sample, voice->k2, voice->pole4, voice->pole3_previous);
            voice->pole4 = sample;
            break;
        case 2:
            sample = lowpass(sample, voice->k2, voice->pole3);
            voice->pole3_previous = voice->pole3;
            voice->pole3 = sample;
            sample = lowpass(sample, voice->k2, voice->pole4);
            voice->pole4 = sample;
            break;
        default:
            sample = lowpass(sample, voice->k1, voice->pole3);
            voice->pole3_previous = voice->pole3;
            voice->pole3 = sample;
            sample = lowpass(sample, voice->k2, voice->pole4);
            voice->pole4 = sample;
            break;
    }
    return sample;
}

static void handle_end(Es5505Voice *voice) {
    uint16_t loop = voice->control & (ES5505_LOOP_ENABLE | ES5505_BIDIRECTIONAL);
    if (!(voice->control & ES5505_REVERSE)) {
        if (voice->accumulator <= voice->end) return;
        if (voice->control & ES5505_IRQ_ENABLE) voice->control |= ES5505_IRQ;
        if (loop == ES5505_LOOP_ENABLE)
            voice->accumulator = (voice->start + voice->accumulator - voice->end) & ACCUMULATOR_MASK;
        else if (loop == (ES5505_LOOP_ENABLE | ES5505_BIDIRECTIONAL)) {
            voice->accumulator = (voice->end - (voice->accumulator - voice->end)) & ACCUMULATOR_MASK;
            voice->control ^= ES5505_REVERSE;
        } else voice->control |= 1;
    } else {
        if (voice->accumulator >= voice->start) return;
        if (voice->control & ES5505_IRQ_ENABLE) voice->control |= ES5505_IRQ;
        if (loop == ES5505_LOOP_ENABLE)
            voice->accumulator = (voice->end - (voice->start - voice->accumulator)) & ACCUMULATOR_MASK;
        else if (loop == (ES5505_LOOP_ENABLE | ES5505_BIDIRECTIONAL)) {
            voice->accumulator = (voice->start + (voice->start - voice->accumulator)) & ACCUMULATOR_MASK;
            voice->control ^= ES5505_REVERSE;
        } else voice->control |= 1;
    }
}

void es5505_core_init(Es5505Core *core, Es5505SampleReader reader, void *context) {
    memset(core, 0, sizeof(*core));
    core->active_voice = 31;
    core->irq_vector = 0x80;
    core->sample_reader = reader;
    core->sample_context = context;
    for (unsigned int voice = 0; voice < ES5505_VOICES; ++voice) {
        core->voices[voice].control = 0xf003;
        core->voices[voice].left_volume = 0x80;
        core->voices[voice].right_volume = 0x80;
    }
    for (unsigned int value = 0; value < 256; ++value) {
        unsigned int exponent = value >> 4;
        unsigned int mantissa = (value & 15) | 16;
        core->volume_table[value] = (mantissa << 11) >> (16 - exponent);
    }
}

void es5505_core_set_port_reader(Es5505Core *core, Es5505PortReader reader, void *context) {
    core->port_reader = reader;
    core->port_context = context;
}

uint16_t es5505_core_read(Es5505Core *core, unsigned int reg) {
    const Es5505Voice *voice = &core->voices[core->page & 31];
    reg &= 15;
    if (reg == 13) return core->active_voice;
    if (reg == 14) {
        uint16_t result = core->irq_vector;
        core->irq_vector = 0x80;
        return result;
    }
    if (reg == 15) return core->page;
    if (core->page >= 0x40) {
        if (reg == 8) return core->mode | 0x07f8;
        if (reg == 9 && core->port_reader) return core->port_reader(core->port_context) & 0xffc0;
        return 0;
    }
    if (core->page >= 0x20) {
        if (reg == 0) return voice->control | 0xf000;
        if (reg == 1) return (uint16_t)voice->pole4;
        if (reg == 2) return (uint16_t)voice->pole3;
        if (reg == 3) return (uint16_t)voice->pole3_previous;
        if (reg == 4) return (uint16_t)voice->pole2;
        if (reg == 5) return (uint16_t)voice->pole2_previous;
        if (reg == 6) return (uint16_t)voice->pole1;
        return 0;
    }
    if (reg == 0) return voice->control | 0xf000;
    if (reg == 1) return voice->frequency >> 1;
    if (reg == 2) return (voice->start >> 18) & 0x1fff;
    if (reg == 3) return (voice->start >> 2) & 0xffff;
    if (reg == 4) return (voice->end >> 18) & 0x1fff;
    if (reg == 5) return (voice->end >> 2) & 0xffff;
    if (reg == 6) return voice->k2;
    if (reg == 7) return voice->k1;
    if (reg == 8) return voice->left_volume << 8;
    if (reg == 9) return voice->right_volume << 8;
    if (reg == 10) return (voice->accumulator >> 18) & 0x1fff;
    if (reg == 11) return (voice->accumulator >> 2) & 0xffff;
    return 0;
}

int es5505_core_irq_pending(const Es5505Core *core) {
    return !(core->irq_vector & 0x80);
}

uint32_t es5505_core_output_divider(const Es5505Core *core) {
    return 16U * ((uint32_t)core->active_voice + 1U);
}

uint32_t es5505_core_output_rate(const Es5505Core *core, uint32_t clock_rate) {
    return clock_rate / es5505_core_output_divider(core);
}

void es5505_core_write(Es5505Core *core, unsigned int reg, uint16_t value) {
    Es5505Voice *voice = &core->voices[core->page & 31];
    reg &= 15;
    if (reg == 13) {
        core->active_voice = value & 31;
        return;
    }
    if (reg == 15) {
        core->page = value & 0x7f;
        return;
    }
    if (reg == 14) return;
    if (core->page >= 0x40) {
        if (reg == 8) core->mode = (core->mode & 0x07f8) | (value & 0xf807);
        return;
    }
    if (core->page >= 0x20) {
        if (reg == 0) voice->control = value | 0xf000;
        else if (reg == 1) voice->pole4 = (int16_t)value;
        else if (reg == 2) voice->pole3 = (int16_t)value;
        else if (reg == 3) voice->pole3_previous = (int16_t)value;
        else if (reg == 4) voice->pole2 = (int16_t)value;
        else if (reg == 5) voice->pole2_previous = (int16_t)value;
        else if (reg == 6) voice->pole1 = (int16_t)value;
        return;
    }
    if (reg == 0) voice->control = value | 0xf000;
    else if (reg == 1) voice->frequency = (value & 0xfffe) << 1;
    else if (reg == 2) voice->start = (voice->start & 0x0003ffff) | ((uint32_t)(value & 0x1fff) << 18);
    else if (reg == 3) voice->start = (voice->start & 0x7ffc0000) | ((uint32_t)(value & 0xffe0) << 2);
    else if (reg == 4) voice->end = (voice->end & 0x0003ffff) | ((uint32_t)(value & 0x1fff) << 18);
    else if (reg == 5) voice->end = (voice->end & 0x7ffc0000) | ((uint32_t)(value & 0xffe0) << 2);
    else if (reg == 6) voice->k2 = value & 0xfff0;
    else if (reg == 7) voice->k1 = value & 0xfff0;
    else if (reg == 8) voice->left_volume = value >> 8;
    else if (reg == 9) voice->right_volume = value >> 8;
    else if (reg == 10) voice->accumulator = (voice->accumulator & 0x0003ffff) | ((uint32_t)(value & 0x1fff) << 18);
    else if (reg == 11) voice->accumulator = (voice->accumulator & 0x7ffc0000) | ((uint32_t)value << 2);
}

void es5505_core_render_buses(Es5505Core *core,
                              int32_t *outputs[ES5505_STEREO_BUSES * 2],
                              size_t frames) {
    for (size_t frame = 0; frame < frames; ++frame) {
        for (unsigned int output = 0; output < ES5505_STEREO_BUSES * 2; ++output)
            outputs[output][frame] = 0;
        for (unsigned int index = 0; index <= core->active_voice; ++index) {
            Es5505Voice *voice = &core->voices[index];
            if ((voice->control & ES5505_STOP_MASK) || !core->sample_reader) continue;
            uint32_t address = voice->accumulator >> FRACTION_BITS;
            uint32_t fraction = voice->accumulator & ((1U << FRACTION_BITS) - 1);
            unsigned int bank = (voice->control >> 2) & 1;
            int32_t first = (int16_t)core->sample_reader(core->sample_context, bank, address);
            int32_t second = (int16_t)core->sample_reader(core->sample_context, bank, address + 1);
            int32_t sample = (first * (2048 - (int32_t)fraction) + second * (int32_t)fraction) >> 11;
            sample = filter_sample(voice, sample);
            unsigned int bus = (voice->control >> 8) & 3;
            outputs[bus * 2][frame] +=
                (sample * (int32_t)core->volume_table[voice->left_volume]) >> 11;
            outputs[bus * 2 + 1][frame] +=
                (sample * (int32_t)core->volume_table[voice->right_volume]) >> 11;
            if (voice->control & ES5505_REVERSE)
                voice->accumulator = (voice->accumulator - voice->frequency) & ACCUMULATOR_MASK;
            else
                voice->accumulator = (voice->accumulator + voice->frequency) & ACCUMULATOR_MASK;
            handle_end(voice);
            if ((voice->control & ES5505_IRQ) && (core->irq_vector & 0x80)) {
                core->irq_vector = (uint8_t)index;
                voice->control &= (uint16_t)~ES5505_IRQ;
            }
        }
    }
}

void es5505_core_render(Es5505Core *core, int32_t *left, int32_t *right,
                        size_t frames) {
    int32_t buses[ES5505_STEREO_BUSES * 2][frames];
    int32_t *outputs[ES5505_STEREO_BUSES * 2];
    for (unsigned int output = 0; output < ES5505_STEREO_BUSES * 2; ++output)
        outputs[output] = buses[output];
    es5505_core_render_buses(core, outputs, frames);
    for (size_t frame = 0; frame < frames; ++frame) {
        left[frame] = 0;
        right[frame] = 0;
        for (unsigned int bus = 0; bus < ES5505_STEREO_BUSES; ++bus) {
            left[frame] += buses[bus * 2][frame];
            right[frame] += buses[bus * 2 + 1][frame];
        }
    }
}
