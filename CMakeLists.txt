cmake_minimum_required(VERSION 3.20)
if(CMAKE_HOST_APPLE AND
   (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR
    CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL ""))
  if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    set(EPS16_DEFAULT_MACOS_TARGET "10.13")
  else()
    set(EPS16_DEFAULT_MACOS_TARGET "11.0")
  endif()
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${EPS16_DEFAULT_MACOS_TARGET}" CACHE STRING
      "Minimum macOS version" FORCE)
endif()
project(eps16_emulator_foundation VERSION 1.0.9 LANGUAGES C CXX)

option(EPS16_BUILD_VST3 "Build the separated macOS VST3 prototype" OFF)

set(MUSASHI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/work/deps/Musashi" CACHE PATH "Musashi source directory")
if(NOT EXISTS "${MUSASHI_DIR}/m68kcpu.c")
  message(FATAL_ERROR "Musashi not found. Clone https://github.com/kstenerud/Musashi into ${MUSASHI_DIR}")
endif()

set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

add_executable(m68kmake "${MUSASHI_DIR}/m68kmake.c")
add_custom_command(
  OUTPUT "${GENERATED_DIR}/m68kops.c" "${GENERATED_DIR}/m68kops.h"
  COMMAND m68kmake "${GENERATED_DIR}" "${MUSASHI_DIR}/m68k_in.c"
  DEPENDS m68kmake "${MUSASHI_DIR}/m68k_in.c"
  VERBATIM
)

add_library(musashi STATIC
  "${MUSASHI_DIR}/m68kcpu.c"
  "${MUSASHI_DIR}/m68kdasm.c"
  "${MUSASHI_DIR}/softfloat/softfloat.c"
  "${GENERATED_DIR}/m68kops.c"
)
target_compile_definitions(musashi PUBLIC
  M68K_EMULATE_FC=M68K_OPT_SPECIFY_HANDLER
  M68K_EMULATE_INT_ACK=M68K_OPT_ON
  M68K_INSTRUCTION_HOOK=M68K_OPT_ON
)
if(MSVC)
  target_compile_options(musashi PUBLIC "/FI${CMAKE_CURRENT_SOURCE_DIR}/native/musashi_config.h")
else()
  target_compile_options(musashi PUBLIC
    "-include${CMAKE_CURRENT_SOURCE_DIR}/native/musashi_config.h"
  )
endif()
target_include_directories(musashi PUBLIC
  "${MUSASHI_DIR}"
  "${MUSASHI_DIR}/softfloat"
  "${GENERATED_DIR}"
)
if(NOT MSVC)
  target_link_libraries(musashi PUBLIC m)
endif()

add_executable(eps16_cpu_smoke native/cpu_smoke.c)
target_link_libraries(eps16_cpu_smoke PRIVATE musashi)

add_executable(eps16_disasm native/disasm.c)
target_link_libraries(eps16_disasm PRIVATE musashi)

add_executable(eps16_rom_probe
  native/rom_probe.c
  native/kpc_legacy.c
  native/kpc_firmware.c
  native/kpc_device.c
  native/m68hc11_core.c
  native/es5505_core.c
  native/es5510_core.c
  native/sampling_input_circuit.c
  native/hfe_disk.c
  native/live_host.c
)
target_include_directories(eps16_rom_probe PRIVATE native)
target_link_libraries(eps16_rom_probe PRIVATE musashi)
if(APPLE)
  target_link_libraries(eps16_rom_probe PRIVATE
    "-framework AudioToolbox"
    "-framework CoreMIDI"
    "-framework CoreFoundation"
    pthread
  )
endif()

add_executable(eps16_es5505_core_test
  native/es5505_core.c
  native/es5505_core_test.c
)
target_include_directories(eps16_es5505_core_test PRIVATE native)

add_executable(eps16_es5510_core_test
  native/es5510_core.c
  native/es5510_core_test.c
)
target_include_directories(eps16_es5510_core_test PRIVATE native)

add_executable(eps16_sampling_input_circuit_test
  native/sampling_input_circuit.c
  native/sampling_input_circuit_test.c
)
target_include_directories(eps16_sampling_input_circuit_test PRIVATE native)
if(NOT MSVC)
  target_link_libraries(eps16_sampling_input_circuit_test PRIVATE m)
endif()

add_executable(eps16_disk_input_test native/hfe_disk.c native/hfe_disk_test.c)
target_include_directories(eps16_disk_input_test PRIVATE native)

add_executable(eps16_hfe_roundtrip_test
  native/hfe_disk.c
  native/hfe_disk_roundtrip_test.c
)
target_include_directories(eps16_hfe_roundtrip_test PRIVATE native)

add_executable(eps16_m68hc11_core_test
  native/m68hc11_core.c
  native/m68hc11_core_test.c
)
target_include_directories(eps16_m68hc11_core_test PRIVATE native)

enable_testing()
add_test(NAME cpu_smoke COMMAND eps16_cpu_smoke)
add_test(NAME es5505_core COMMAND eps16_es5505_core_test)
add_test(NAME es5510_core COMMAND eps16_es5510_core_test)
add_test(NAME sampling_input_circuit COMMAND eps16_sampling_input_circuit_test)
add_test(NAME hfe_roundtrip COMMAND eps16_hfe_roundtrip_test)
add_test(NAME m68hc11_core COMMAND eps16_m68hc11_core_test)

if(EPS16_BUILD_VST3)
  add_subdirectory(vst3)
endif()
