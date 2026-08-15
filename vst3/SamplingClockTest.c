#include "ProbeMachine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void print_state(const char *label) {
    char display[23];
    eps16_probe_machine_display(display);
    printf("%s display=|%s| cycles=%llu conversions=%llu\n",
           label, display,
           (unsigned long long)eps16_probe_machine_cycles(),
           (unsigned long long)eps16_probe_machine_sampling_input_conversions());
}

static int wait_for_display(const char *prefix, uint64_t timeout) {
    const uint64_t deadline = eps16_probe_machine_cycles() + timeout;
    char display[23];
    while (eps16_probe_machine_cycles() < deadline) {
        eps16_probe_machine_display(display);
        if (!strncmp(display, prefix, strlen(prefix))) return 1;
        run_for(10000000);
    }
    eps16_probe_machine_display(display);
    fprintf(stderr, "timed out waiting for |%s|; display=|%s|\n",
            prefix, display);
    return 0;
}

static int measure_clock(unsigned int expected_selector) {
    const uint64_t duration = 50000000;
    eps16_probe_machine_sampling_input(0.25f, 0.25f);
    const uint64_t before = eps16_probe_machine_sampling_input_conversions();
    const uint64_t start = eps16_probe_machine_cycles();
    run_for(duration);
    const uint64_t elapsed = eps16_probe_machine_cycles() - start;
    const uint64_t conversions =
        eps16_probe_machine_sampling_input_conversions() - before;
    const double measured = 10000000.0 * (double)conversions / (double)elapsed;
    const double ideal = 10000000.0 / (112.0 * expected_selector);
    const double error_percent = 100.0 * (measured / ideal - 1.0);
    printf("selector=%u elapsed=%llu conversions=%llu measured=%.6f ideal=%.6f error=%.6f%%\n",
           expected_selector, (unsigned long long)elapsed,
           (unsigned long long)conversions, measured, ideal,
           error_percent);
    return fabs(error_percent) < 0.01;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s COMBINED_ROM KPC_ROM OS_DISK\n", argv[0]);
        return 2;
    }
    Eps16ProbeMachine *machine = eps16_probe_machine_create();
    if (!machine || !eps16_probe_machine_begin(machine)) return 1;
    char error[256];
    if (!eps16_probe_machine_initialize(argv[1], argv[2], argv[3],
                                        error, sizeof(error))) {
        fprintf(stderr, "initialization failed: %s\n", error);
        return 1;
    }
    eps16_probe_machine_run_until(220000000);
    click(0x20);
    click(0x02);
    eps16_probe_machine_sampling_input(0.25f, 0.25f);
    run_for(50000000);
    print_state("level");

    click(0x11);
    print_state("right-1");

    const size_t snapshot_size = eps16_probe_machine_state_size();
    void *rate_page = malloc(snapshot_size);
    if (!rate_page ||
        !eps16_probe_machine_save_state(rate_page, snapshot_size))
        return 1;
    int all_rates_ok = 1;
    for (unsigned int selector = 2; selector <= 8; ++selector) {
        if (!eps16_probe_machine_load_state(rate_page, snapshot_size,
                                            error, sizeof(error))) {
            fprintf(stderr, "snapshot restore failed: %s\n", error);
            return 1;
        }
        run_for(20000000);
        for (unsigned int step = 2; step < selector; ++step) click(0x0b);
        click(0x10);
        print_state("level-rate");
        click(0x23);
        print_state("recording-rate");
        if (!measure_clock(selector)) all_rates_ok = 0;
        click(0x23);
        if (!wait_for_display("PLAY ROOT KEY", 200000000))
            all_rates_ok = 0;
        eps16_probe_machine_midi(0x90, 60, 100);
        run_for(1000000);
        const uint32_t frequency =
            eps16_probe_machine_last_keyon_frequency();
        const double ideal_increment = 3.0 / (double)selector;
        const double actual_increment = (double)frequency / 2048.0;
        const double pitch_error_cents =
            1200.0 * log2(actual_increment / ideal_increment);
        printf("selector=%u frequency=0x%04x increment=%.6f ideal=%.6f pitch_error=%.3f cents\n",
               selector, frequency, actual_increment, ideal_increment,
               pitch_error_cents);
        if (!frequency || fabs(pitch_error_cents) > 5.0) all_rates_ok = 0;
        eps16_probe_machine_midi(0x80, 60, 0);
        run_for(1000000);
    }
    free(rate_page);
    eps16_probe_machine_end(machine);
    eps16_probe_machine_destroy(machine);
    return all_rates_ok ? 0 : 1;
}
