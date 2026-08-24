#include "ProbeMachine.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void run_for(uint64_t cycles) {
    eps16_probe_machine_run_until(eps16_probe_machine_cycles() + cycles);
}

static void edge(uint8_t code, int pressed) {
    eps16_probe_machine_panel_byte((uint8_t)(code | (pressed ? 0x80 : 0)));
    eps16_probe_machine_panel_byte(0);
    run_for(12000000);
}

static void click(uint8_t code) {
    edge(code, 1);
    edge(code, 0);
}

static int display_starts_with(const char *expected) {
    char display[23];
    eps16_probe_machine_display(display);
    printf("display=|%s| expected=|%s|\n", display, expected);
    return !strncmp(display, expected, strlen(expected));
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s COMBINED_ROM KPC_ROM OS_DISK\n", argv[0]);
        return 2;
    }
    Eps16ProbeMachine *machine = eps16_probe_machine_create();
    char error[256] = {0};
    if (!machine || !eps16_probe_machine_begin(machine) ||
        !eps16_probe_machine_initialize(argv[1], argv[2], argv[3], error,
                                        sizeof(error))) {
        fprintf(stderr, "machine initialization failed: %s\n", error);
        return 1;
    }
    eps16_probe_machine_run_until(220000000);

    /* Create one instrument entirely through the original COMMAND path. */
    click(0x06);
    click(0x0f);
    click(0x23);
    click(0x23);

    /* Hardware recording holds RECORD while PLAY is pressed. These are the
       verified physical raw matrix positions, not OS/menu shortcuts. */
    edge(0x03, 1);
    edge(0x1d, 1);
    edge(0x1d, 0);
    edge(0x03, 0);
    run_for(100000000);
    if (!display_starts_with("SEQUENCE 01") ||
        !(eps16_probe_machine_indicator_on(2) & 0x0008U))
        return 1;

    click(0x17);
    run_for(50000000);
    if (!display_starts_with("SEQUENCE 01") ||
        !(eps16_probe_machine_indicator_on(2) & 0x0800U) ||
        eps16_probe_machine_illegal_instructions())
        return 1;

    /* PLAY and STOP/CONT can follow short BAR/status field writes with a
       complete 60 01 VFD text frame. Repeated transport changes must not
       inherit the preceding field's write position and rotate the 22 cells. */
    for (unsigned int replay = 0; replay < 3; ++replay) {
        click(0x1d);
        if (!display_starts_with("SEQUENCE 01") ||
            eps16_probe_machine_illegal_instructions())
            return 1;
        click(0x17);
        if (!display_starts_with("SEQUENCE 01") ||
            eps16_probe_machine_illegal_instructions())
            return 1;
    }
    eps16_probe_machine_destroy(machine);
    return 0;
}
