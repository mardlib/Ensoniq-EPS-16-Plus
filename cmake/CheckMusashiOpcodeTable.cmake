# Fails the build if the generated Musashi opcode table is wrong.
#
# The official Windows x64 1.0.10 VST3 contains an opcode handler table in
# which bit 14 (0x4000) is missing from every mask and match value: 0 of 1967
# entries are correct, and the line-A entry reads mask 0xb000, match 0xa000
# instead of 0xf000/0xa000.  The emulated 68000 then decodes opcodes wrongly
# (and the table walk overran its sentinel, the crash in issue #9).  A
# Windows rebuild of the same source by SteveJamesMusic contains the correct
# table, identical to the Linux/macOS output, so the fault is in how
# m68kmake was built, not in the sources.  These spot checks catch it.
#
# Usage: cmake -DEPS16_M68KOPS=<path to generated m68kops.c> -P CheckMusashiOpcodeTable.cmake
file(READ "${EPS16_M68KOPS}" EPS16_OPS)
foreach(entry
    "m68k_op_1010 +, 0xf000, 0xa000,"
    "m68k_op_1111 +, 0xf000, 0xf000,"
    "m68k_op_moveq_32 +, 0xf100, 0x7000,"
    "m68k_op_rts_32 +, 0xffff, 0x4e75,"
    "m68k_op_stop +, 0xffff, 0x4e72,")
  if(NOT EPS16_OPS MATCHES "\\{${entry}")
    message(FATAL_ERROR
      "Generated Musashi opcode table is wrong (expected {${entry} in "
      "${EPS16_M68KOPS}). m68kmake was probably miscompiled; see "
      "cmake/CheckMusashiOpcodeTable.cmake.")
  endif()
endforeach()
