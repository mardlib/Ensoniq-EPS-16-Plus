#include "ProbeMachine.h"

#include <stdio.h>
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

static void print_display(const char *step) {
    char display[23];
    eps16_probe_machine_display(display);
    printf("%s=|%s| cycles=%llu\n", step, display,
           (unsigned long long)eps16_probe_machine_cycles());
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s ROM KPC_ROM OS_DISK SCSI_ISO\n", argv[0]);
        return 2;
    }
    Eps16ProbeMachine *machine = eps16_probe_machine_create();
    if (!machine || !eps16_probe_machine_begin(machine)) return 1;
    char error[256] = {0};
    if (!eps16_probe_machine_initialize(argv[1], argv[2], argv[3],
                                        error, sizeof(error))) {
        fprintf(stderr, "initialization failed: %s\n", error);
        return 1;
    }
    run_for(220000000);
    if (!eps16_probe_machine_insert_scsi_cd(argv[4], error, sizeof(error))) {
        fprintf(stderr, "SCSI mount failed: %s\n", error);
        return 1;
    }

    click(0x06); /* COMMAND */
    click(0x1b); /* SYSTEM/MIDI */
    for (unsigned int page = 0; page < 5; ++page) click(0x11);
    click(0x23); /* CHANGE STORAGE DEVICE */
    click(0x0a); /* FLOPPY -> SCSI 0 */
    click(0x23);
    click(0x1a); /* LOAD */
    click(0x1b); /* SYSTEM/MIDI directories */

    print_display("browser");
    click(0x23); /* enter STRINGED directory */
    print_display("inside-stringed");
    click(0x23); /* enter STRING SECTN directory */
    print_display("inside-section");
    click(0x0f); /* INSTRUMENT file page */
    print_display("first-browser");
    click(0x23); /* choose first instrument */
    print_display("first-pick");
    click(0x02); /* Instrument/Track 1 */
    print_display("first-start");
    char display[23];
    eps16_probe_machine_display(display);
    const int first_loaded = strstr(display, "FILE LOADED") != NULL;
    run_for(100000000);
    print_display("first-finish");

    click(0x1a); /* LOAD: retained instrument page */
    click(0x0f); /* INSTRUMENT */
    print_display("second-browser");
    click(0x23); /* choose the same instrument */
    print_display("second-pick");
    click(0x02); /* replace Instrument/Track 1 */
    print_display("second-start");
    eps16_probe_machine_display(display);
    const int second_loaded = strstr(display, "FILE LOADED") != NULL;
    run_for(100000000);
    print_display("second-finish");

    eps16_probe_machine_display(display);
    printf("display=|%s| illegal=%zu\n", display,
           eps16_probe_machine_illegal_instructions());
    const int ok = first_loaded && second_loaded &&
                   eps16_probe_machine_illegal_instructions() == 0;
    eps16_probe_machine_end(machine);
    eps16_probe_machine_destroy(machine);
    return ok ? 0 : 1;
}
