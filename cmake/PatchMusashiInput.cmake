# Writes a patched copy of Musashi's m68k_in.c. Used by CMakeLists.txt (include)
# and by the Makefile (cmake -DEPS16_MUSASHI_IN=... -DEPS16_MUSASHI_OUT=... -P).
#
# 1. Opcode-table bounds: Musashi's generated opcode-table builder walks mask
#    groups using an unbounded sentinel search.  MSVC can order the generated
#    groups such that the first search reaches and steps past the sentinel.
#    Give every group walk an explicit end pointer; the existing generic first
#    pass still handles any group that is not in the expected optimized order.
#
# 2. STOP with an interrupt already pending: STOP sets the new SR, and
#    m68ki_set_sr() takes an interrupt that is pending above the new mask at
#    once, which clears CPU_STOPPED.  Unpatched Musashi then still charges the
#    rest of the time slice, as if the CPU had stayed stopped.  The EPS OS idles
#    in "STOP #$2000" right after polling with the mask at 7, so a 1 kHz DUART
#    timer tick that arrived during the poll was serviced up to a whole slice
#    late, and the next tick was then lost (the sequencer ran 0.2-1.5 % slow).
#    Only burn the remaining cycles while the CPU really stays stopped.
if(NOT EPS16_MUSASHI_IN OR NOT EPS16_MUSASHI_OUT)
  message(FATAL_ERROR "EPS16_MUSASHI_IN and EPS16_MUSASHI_OUT must be set")
endif()
file(READ "${EPS16_MUSASHI_IN}" EPS16_MUSASHI_INPUT_SOURCE)

string(REPLACE
  "const opcode_handler_struct *ostruct;"
  "const opcode_handler_struct *ostruct;\n\tconst opcode_handler_struct *table_end;"
  EPS16_MUSASHI_INPUT_SOURCE "${EPS16_MUSASHI_INPUT_SOURCE}")
string(REPLACE
  "ostruct = m68k_opcode_handler_table;"
  "ostruct = m68k_opcode_handler_table;\n\ttable_end = m68k_opcode_handler_table + (sizeof(m68k_opcode_handler_table) / sizeof(m68k_opcode_handler_table[0])) - 1;"
  EPS16_MUSASHI_INPUT_SOURCE "${EPS16_MUSASHI_INPUT_SOURCE}")
string(REPLACE "while(ostruct->mask"
               "while(ostruct < table_end && ostruct->mask"
               EPS16_MUSASHI_INPUT_SOURCE "${EPS16_MUSASHI_INPUT_SOURCE}")
if(EPS16_MUSASHI_INPUT_SOURCE MATCHES "while\\(ostruct->mask")
  message(FATAL_ERROR "Musashi opcode-table bounds patch was incomplete")
endif()

set(EPS16_STOP_OLD "\t\tCPU_STOPPED |= STOP_LEVEL_STOP;\n\t\tm68ki_set_sr(new_sr);\n\t\tif(m68ki_remaining_cycles >= CYC_INSTRUCTION[REG_IR])\n\t\t\tm68ki_remaining_cycles = CYC_INSTRUCTION[REG_IR];\n\t\telse\n\t\t\tUSE_ALL_CYCLES();\n")
set(EPS16_STOP_NEW "\t\tCPU_STOPPED |= STOP_LEVEL_STOP;\n\t\tm68ki_set_sr(new_sr);\n\t\tif(CPU_STOPPED)\n\t\t{\n\t\t\tif(m68ki_remaining_cycles >= CYC_INSTRUCTION[REG_IR])\n\t\t\t\tm68ki_remaining_cycles = CYC_INSTRUCTION[REG_IR];\n\t\t\telse\n\t\t\t\tUSE_ALL_CYCLES();\n\t\t}\n")
string(FIND "${EPS16_MUSASHI_INPUT_SOURCE}" "${EPS16_STOP_OLD}" EPS16_STOP_POSITION)
if(EPS16_STOP_POSITION EQUAL -1)
  message(FATAL_ERROR "Musashi STOP patch did not apply")
endif()
string(REPLACE "${EPS16_STOP_OLD}" "${EPS16_STOP_NEW}"
       EPS16_MUSASHI_INPUT_SOURCE "${EPS16_MUSASHI_INPUT_SOURCE}")

file(WRITE "${EPS16_MUSASHI_OUT}" "${EPS16_MUSASHI_INPUT_SOURCE}")
