CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
MUSASHI_DEFINES := -DM68K_EMULATE_FC=M68K_OPT_SPECIFY_HANDLER \
	'-DM68K_SET_FC_CALLBACK(A)=CALLBACK_SET_FC(A)' \
	-DM68K_EMULATE_INT_ACK=M68K_OPT_ON \
	-DM68K_ILLG_HAS_CALLBACK=M68K_OPT_ON \
	-DM68K_INSTRUCTION_HOOK=M68K_OPT_ON
MUSASHI_DIR ?= work/deps/Musashi
BUILD_DIR ?= work/build
GENERATED_DIR := $(BUILD_DIR)/generated
M68KMAKE := $(BUILD_DIR)/m68kmake
CPU_SMOKE := $(BUILD_DIR)/eps16_cpu_smoke
DISASM := $(BUILD_DIR)/eps16_disasm
ROM_PROBE := $(BUILD_DIR)/eps16_rom_probe
ES5505_TEST := $(BUILD_DIR)/eps16_es5505_core_test
ES5510_TEST := $(BUILD_DIR)/eps16_es5510_core_test
M68HC11_TEST := $(BUILD_DIR)/eps16_m68hc11_core_test
KPC_BOOT_TEST := $(BUILD_DIR)/eps16_kpc_boot_test
ifeq ($(shell uname -s),Darwin)
LIVE_LIBS := -framework AudioToolbox -framework CoreMIDI -framework CoreFoundation -lpthread
endif

.PHONY: all test clean

all: $(CPU_SMOKE) $(DISASM) $(ROM_PROBE) $(ES5505_TEST) $(ES5510_TEST) $(M68HC11_TEST) $(KPC_BOOT_TEST)

$(KPC_BOOT_TEST): native/m68hc11_core.c native/m68hc11_core.h native/kpc_firmware.c native/kpc_firmware.h native/kpc_device.c native/kpc_device.h native/kpc_boot_test.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Inative -o $@ native/m68hc11_core.c native/kpc_firmware.c native/kpc_device.c native/kpc_boot_test.c

$(M68HC11_TEST): native/m68hc11_core.c native/m68hc11_core.h native/m68hc11_core_test.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Inative -o $@ native/m68hc11_core.c native/m68hc11_core_test.c

$(ES5505_TEST): native/es5505_core.c native/es5505_core.h native/es5505_core_test.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Inative -o $@ native/es5505_core.c native/es5505_core_test.c

$(ES5510_TEST): native/es5510_core.c native/es5510_core.h native/es5510_core_test.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -Inative -o $@ native/es5510_core.c native/es5510_core_test.c

$(M68KMAKE): $(MUSASHI_DIR)/m68kmake.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/musashi-input/m68k_in.c: $(MUSASHI_DIR)/m68k_in.c cmake/PatchMusashiInput.cmake
	@mkdir -p $(BUILD_DIR)/musashi-input
	cmake -DEPS16_MUSASHI_IN=$(MUSASHI_DIR)/m68k_in.c -DEPS16_MUSASHI_OUT=$@ -P cmake/PatchMusashiInput.cmake

$(GENERATED_DIR)/m68kops.c $(GENERATED_DIR)/m68kops.h &: $(M68KMAKE) $(BUILD_DIR)/musashi-input/m68k_in.c
	@mkdir -p $(GENERATED_DIR)
	$(M68KMAKE) $(GENERATED_DIR) $(BUILD_DIR)/musashi-input/m68k_in.c

$(CPU_SMOKE): native/cpu_smoke.c $(MUSASHI_DIR)/m68kcpu.c \
              $(MUSASHI_DIR)/m68kdasm.c $(MUSASHI_DIR)/softfloat/softfloat.c \
              $(GENERATED_DIR)/m68kops.c $(GENERATED_DIR)/m68kops.h
	$(CC) $(CFLAGS) $(MUSASHI_DEFINES) -I$(MUSASHI_DIR) -I$(MUSASHI_DIR)/softfloat \
		-I$(GENERATED_DIR) -o $@ native/cpu_smoke.c \
		$(MUSASHI_DIR)/m68kcpu.c $(MUSASHI_DIR)/m68kdasm.c \
		$(MUSASHI_DIR)/softfloat/softfloat.c $(GENERATED_DIR)/m68kops.c -lm

$(DISASM): native/disasm.c $(MUSASHI_DIR)/m68kcpu.c \
           $(MUSASHI_DIR)/m68kdasm.c $(MUSASHI_DIR)/softfloat/softfloat.c \
           $(GENERATED_DIR)/m68kops.c $(GENERATED_DIR)/m68kops.h
	$(CC) $(CFLAGS) $(MUSASHI_DEFINES) -I$(MUSASHI_DIR) -I$(MUSASHI_DIR)/softfloat \
		-I$(GENERATED_DIR) -o $@ native/disasm.c \
		$(MUSASHI_DIR)/m68kcpu.c $(MUSASHI_DIR)/m68kdasm.c \
		$(MUSASHI_DIR)/softfloat/softfloat.c $(GENERATED_DIR)/m68kops.c -lm

$(ROM_PROBE): native/rom_probe.c native/kpc_legacy.c native/kpc_legacy.h \
              native/kpc_firmware.c native/kpc_firmware.h \
              native/kpc_device.c native/kpc_device.h \
              native/m68hc11_core.c native/m68hc11_core.h \
              native/es5505_core.c native/es5505_core.h \
              native/es5510_core.c native/es5510_core.h \
              native/hfe_disk.c native/hfe_disk.h native/live_host.c native/live_host.h $(MUSASHI_DIR)/m68kcpu.c \
              $(MUSASHI_DIR)/m68kdasm.c $(MUSASHI_DIR)/softfloat/softfloat.c \
              $(GENERATED_DIR)/m68kops.c $(GENERATED_DIR)/m68kops.h
	$(CC) $(CFLAGS) $(MUSASHI_DEFINES) -I$(MUSASHI_DIR) -I$(MUSASHI_DIR)/softfloat \
		-I$(GENERATED_DIR) -Inative -o $@ native/rom_probe.c native/kpc_legacy.c native/kpc_firmware.c native/kpc_device.c native/m68hc11_core.c native/es5505_core.c native/es5510_core.c \
		native/hfe_disk.c native/live_host.c \
		$(MUSASHI_DIR)/m68kcpu.c $(MUSASHI_DIR)/m68kdasm.c \
		$(MUSASHI_DIR)/softfloat/softfloat.c $(GENERATED_DIR)/m68kops.c -lm $(LIVE_LIBS)

test: $(CPU_SMOKE) $(DISASM) $(ROM_PROBE) $(ES5505_TEST) $(ES5510_TEST) $(M68HC11_TEST)
	$(CPU_SMOKE)
	$(ES5505_TEST)
	$(ES5510_TEST)
	$(M68HC11_TEST)
	PYTHONPATH=. python3 -m unittest discover -s tests -v

clean:
	rm -rf $(BUILD_DIR)
