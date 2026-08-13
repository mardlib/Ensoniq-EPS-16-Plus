/*
 * Ensoniq ES5510 execution core.
 *
 * The pipeline and instruction semantics follow the BSD-3-Clause MAME ES5510
 * device by Christian Brunschen and MAMEdev contributors, adapted here to a
 * small standalone C core:
 * https://github.com/mamedev/mame/tree/master/src/devices/cpu/es5510
 *
 * See THIRD_PARTY_NOTICES.md for attribution and the BSD-3-Clause notice.
 */
#include "es5510_core.h"

#include <limits.h>
#include <string.h>

enum {
    FLAG_N = 0x80, FLAG_C = 0x40, FLAG_V = 0x20,
    FLAG_LT = 0x10, FLAG_Z = 0x08, FLAG_NOT = 0x04,
    FLAG_MASK = FLAG_N | FLAG_C | FLAG_V | FLAG_LT | FLAG_Z,
    SRC_REG = 1, SRC_DELAY = 2, SRC_BOTH = 3,
    RAM_READ = 0, RAM_WRITE = 1, RAM_DUMP = 2,
    ACCESS_DELAY = 0, ACCESS_A = 1, ACCESS_B = 2, ACCESS_IO = 3
};

typedef struct { uint8_t alu_src, alu_dst, mac_src, mac_dst; } OperandSelect;
static const OperandSelect operand_select[16] = {
    {SRC_REG,SRC_REG,SRC_REG,SRC_REG},       {SRC_REG,SRC_REG,SRC_REG,SRC_DELAY},
    {SRC_REG,SRC_REG,SRC_REG,SRC_BOTH},      {SRC_REG,SRC_REG,SRC_DELAY,SRC_REG},
    {SRC_REG,SRC_REG,SRC_DELAY,SRC_BOTH},    {SRC_REG,SRC_DELAY,SRC_REG,SRC_REG},
    {SRC_REG,SRC_DELAY,SRC_DELAY,SRC_REG},   {SRC_REG,SRC_BOTH,SRC_REG,SRC_REG},
    {SRC_REG,SRC_BOTH,SRC_DELAY,SRC_REG},    {SRC_DELAY,SRC_REG,SRC_REG,SRC_REG},
    {SRC_DELAY,SRC_REG,SRC_REG,SRC_DELAY},   {SRC_DELAY,SRC_REG,SRC_REG,SRC_BOTH},
    {SRC_DELAY,SRC_REG,SRC_DELAY,SRC_REG},   {SRC_DELAY,SRC_REG,SRC_DELAY,SRC_BOTH},
    {SRC_DELAY,SRC_BOTH,SRC_REG,SRC_REG},    {SRC_DELAY,SRC_BOTH,SRC_DELAY,SRC_REG}
};

typedef struct { uint8_t cycle, access; } RamControl;
static const RamControl ram_control[8] = {
    {RAM_READ,ACCESS_DELAY}, {RAM_WRITE,ACCESS_DELAY},
    {RAM_READ,ACCESS_A}, {RAM_WRITE,ACCESS_A},
    {RAM_READ,ACCESS_B}, {RAM_DUMP,ACCESS_DELAY},
    {RAM_READ,ACCESS_IO}, {RAM_WRITE,ACCESS_IO}
};

static int32_t sext24(uint32_t value) {
    value &= 0xffffff;
    return (value & 0x800000) ? (int32_t)(value | 0xff000000U) : (int32_t)value;
}

static int64_t sext48(uint64_t value) {
    value &= UINT64_C(0xffffffffffff);
    return (value & UINT64_C(0x800000000000))
        ? (int64_t)(value | UINT64_C(0xffff000000000000)) : (int64_t)value;
}

static void set_flag(uint8_t *flags, uint8_t flag, int enabled) {
    if (enabled) *flags |= flag;
    else *flags &= (uint8_t)~flag;
}

static uint32_t add24(uint32_t a, uint32_t b, uint8_t *flags) {
    uint32_t sum = (a & 0xffffff) + (b & 0xffffff);
    int a_sign = (a & 0x800000) != 0;
    int b_sign = (b & 0x800000) != 0;
    int result_sign = (sum & 0x800000) != 0;
    int overflow = a_sign == b_sign && a_sign != result_sign;
    set_flag(flags, FLAG_C, (sum & 0x1000000) != 0);
    set_flag(flags, FLAG_N, result_sign);
    set_flag(flags, FLAG_Z, (sum & 0xffffff) == 0);
    set_flag(flags, FLAG_V, overflow);
    set_flag(flags, FLAG_LT, overflow ? !result_sign : result_sign);
    return sum & 0xffffff;
}

static uint32_t negate24(uint32_t value) {
    return ((value ^ 0xffffff) + 1) & 0xffffff;
}

static uint32_t saturate24(uint32_t value, uint8_t *flags, int negative) {
    if (!(*flags & FLAG_V)) return value & 0xffffff;
    set_flag(flags, FLAG_N, negative);
    set_flag(flags, FLAG_Z, 0);
    return negative ? 0x800000 : 0x7fffff;
}

static uint32_t shift_left(uint32_t value, unsigned int shift, uint8_t *flags) {
    int64_t shifted = (int64_t)sext24(value) << shift;
    int overflow = shifted > 0x7fffff || shifted < -0x800000;
    uint32_t result = overflow ? (shifted < 0 ? 0x800000 : 0x7fffff)
                               : (uint32_t)shifted & 0xffffff;
    set_flag(flags, FLAG_C, ((value & 0xffffff) >> (24 - shift)) & 1);
    set_flag(flags, FLAG_V, overflow);
    set_flag(flags, FLAG_N, (result & 0x800000) != 0);
    set_flag(flags, FLAG_Z, result == 0);
    return result;
}

static uint32_t alu_operation(uint8_t op, uint32_t a, uint32_t b, uint8_t *flags) {
    uint32_t result;
    switch (op) {
        case 0: result = add24(a, b, flags); return saturate24(result, flags, a & 0x800000);
        case 1: result = add24(a, negate24(b), flags); return saturate24(result, flags, a & 0x800000);
        case 2: return add24(a, b, flags);
        case 3: return add24(a, negate24(b), flags);
        case 4: (void)add24(a, negate24(b), flags); return a;
        case 5:
            result = a & b; set_flag(flags, FLAG_N, result & 0x800000);
            set_flag(flags, FLAG_Z, result == 0); return result;
        case 6:
            result = a | b; set_flag(flags, FLAG_N, result & 0x800000);
            set_flag(flags, FLAG_Z, result == 0); return result;
        case 7:
            result = a ^ b; set_flag(flags, FLAG_N, result & 0x800000);
            set_flag(flags, FLAG_Z, result == 0); return result;
        case 8:
            *flags &= (uint8_t)~FLAG_N; set_flag(flags, FLAG_C, b & 0x800000);
            result = (b & 0x800000) ? (0xffffff ^ b) : b;
            set_flag(flags, FLAG_Z, (result & 0xffffff) == 0); return result;
        case 9: return b;
        case 10: return shift_left(b, 2, flags);
        case 11: return shift_left(b, 8, flags);
        case 12:
            *flags &= (uint8_t)~FLAG_N; set_flag(flags, FLAG_C, b & 0x800000);
            return (b << 15) & 0x7fffff;
        case 13: return add24(0x7fffff, negate24(b), flags);
        case 14:
            set_flag(flags, FLAG_N, b & 0x800000); set_flag(flags, FLAG_C, b & 1);
            return (b >> 1) | (b & 0x800000);
        default: return 0;
    }
}

static int16_t round24to16(uint32_t value) {
    int32_t signed_value = sext24(value);
    int add = signed_value < 0 && (value & 0xff) != 0;
    return (int16_t)((signed_value >> 8) + add);
}

static void write_dol(Es5510Core *core, uint32_t value) {
    int16_t sample = round24to16(value);
    if (core->dol_count >= 2) {
        core->dol[0] = core->dol[1];
        core->dol[1] = sample;
    } else core->dol[core->dol_count++] = sample;
}

void es5510_core_init(Es5510Core *core) {
    memset(core, 0, sizeof(*core));
    core->memsiz = 0xffffff;
    core->memincrement = 0x1000000;
    core->memshift = 24;
    core->halted = 1;
}

uint32_t es5510_core_read_reg(Es5510Core *core, uint8_t reg) {
    if (reg < 0xc0) return core->gpr[reg] & 0xffffff;
    if (reg >= 0xea && reg <= 0xf1) {
        static const uint8_t serial_index[8] = {1,0,3,2,5,4,7,6};
        return (uint32_t)(uint16_t)core->serial[serial_index[reg - 0xea]] << 8;
    }
    switch (reg) {
        case 0xf2:
            return core->mac_overflow ? (core->machl < 0 ? 0 : 0xffffff)
                                      : (uint32_t)core->machl & 0xffffff;
        case 0xf3:
            return core->mac_overflow ? (core->machl < 0 ? 0x800000 : 0x7fffff)
                                      : ((uint64_t)core->machl >> 24) & 0xffffff;
        case 0xf4: return (uint32_t)(uint16_t)core->dil << 8;
        case 0xf5: return core->dlength & 0xffffff;
        case 0xf6: return core->abase & 0xffffff;
        case 0xf7: return core->bbase & 0xffffff;
        case 0xf8: return core->dbase & 0xffffff;
        case 0xf9: return core->sigreg & 0xffffff;
        case 0xfa: return (uint32_t)core->ccr << 16;
        case 0xfb: return (uint32_t)core->cmr << 16;
        case 0xfc: return 0xffffff;
        case 0xfd: return 0x800000;
        case 0xfe: return 0x7fffff;
        default: return 0;
    }
}

void es5510_core_write_reg(Es5510Core *core, uint8_t reg, uint32_t value) {
    value &= 0xffffff;
    if (reg < 0xc0) { core->gpr[reg] = value; return; }
    if (reg >= 0xea && reg <= 0xf1) {
        static const uint8_t serial_index[8] = {1,0,3,2,5,4,7,6};
        core->serial[serial_index[reg - 0xea]] = (int16_t)(value >> 8);
        return;
    }
    switch (reg) {
        case 0xf2:
            core->machl = sext48(((uint64_t)core->machl & UINT64_C(0xffffff000000)) | value);
            break;
        case 0xf3:
            core->machl = sext48(((uint64_t)value << 24) |
                                 ((uint64_t)core->machl & UINT64_C(0xffffff)));
            core->mac_overflow = 0;
            break;
        case 0xf4: {
            unsigned int low_ones = 0;
            while (low_ones < 24 && ((value >> low_ones) & 1)) ++low_ones;
            core->memshift = (int)low_ones;
            core->memsiz = 0xffffffU >> (24 - low_ones);
            core->memmask = 0xffffffU & ~core->memsiz;
            core->memincrement = 1U << low_ones;
            core->dbase &= core->memmask;
            break;
        }
        case 0xf5: core->dlength = value; break;
        case 0xf6: core->abase = value; break;
        case 0xf7: core->bbase = value; break;
        case 0xf8: core->dbase = value; break;
        case 0xf9: core->sigreg = value; break;
        case 0xfa: core->ccr = (value >> 16) & FLAG_MASK; break;
        case 0xfb: core->cmr = (value >> 16) & (FLAG_MASK | FLAG_NOT); break;
        default: break;
    }
}

void es5510_core_set_halted(Es5510Core *core, int halted) {
    core->halted = halted != 0;
}

void es5510_core_set_host_serial(Es5510Core *core, uint8_t value) {
    core->host_serial = value;
}

unsigned int es5510_core_dram_address(const Es5510Core *core,
                                      uint32_t address) {
    return ((address & core->memmask) >> core->memshift) &
           (ES5510_DRAM_WORDS - 1);
}

static int32_t ram_address(Es5510Core *core, uint8_t access, uint32_t offset) {
    uint32_t address;
    if (access == ACCESS_DELAY) {
        uint32_t length = core->dlength + core->memincrement;
        address = length ? (core->dbase + offset) % length : 0;
    } else if (access == ACCESS_A) address = core->abase + offset;
    else if (access == ACCESS_B) address = core->bbase + offset;
    else return (int32_t)(offset & 0xfffff0);
    return (int32_t)es5510_core_dram_address(core, address);
}

static void eject_dol(Es5510Core *core) {
    core->dol[0] = core->dol[1];
    if (core->dol_count) --core->dol_count;
}

void es5510_core_process(Es5510Core *core, const int16_t inputs[8],
                         int16_t outputs[2]) {
    /* Host Serial Control bits 2..5 select the direction of ports 0..3.
       Input pins update only input-configured registers; output registers
       retain the DSP result until the program writes them again. */
    for (unsigned int port = 0; port < 4; ++port) {
        if (!(core->host_serial & (uint8_t)(0x04U << port))) {
            core->serial[port * 2] = inputs[port * 2];
            core->serial[port * 2 + 1] = inputs[port * 2 + 1];
        }
    }
    if (!core->halted) {
        unsigned int pc = 0;
        for (; pc < ES5510_INSTRUCTIONS; ++pc) {
            uint64_t instruction = core->instruction[pc];
            core->ram_pp = core->ram_p;
            core->ram_p = core->ram;
            if (core->ram_pp.cycle != RAM_WRITE)
                core->dil = core->ram_pp.io ? 0
                    : core->dram[(unsigned int)core->ram_pp.address & (ES5510_DRAM_WORDS - 1)];

            RamControl control = ram_control[(instruction >> 3) & 7];
            core->ram.cycle = control.cycle;
            core->ram.io = control.access == ACCESS_IO;
            core->ram.address = ram_address(core, control.access, core->gpr[pc]);

            OperandSelect select = operand_select[(instruction >> 8) & 15];
            int skippable = (instruction >> 7) & 1;
            int condition = (core->ccr & core->cmr & FLAG_MASK) != 0;
            if (core->cmr & FLAG_NOT) condition = !condition;
            int skip = skippable && condition;

            if (core->mac.write_result) {
                int shift = (core->sigreg & 0x400000) ? 1 : 2;
                core->mac.product = (int64_t)sext24(core->mac.c_value) *
                                    (int64_t)sext24(core->mac.d_value) << shift;
                core->mac.result = core->mac.accumulate
                    ? core->mac.product + core->machl : core->mac.product;
                core->mac_overflow = core->mac.result < -(INT64_C(1) << 47) ||
                                     core->mac.result >= (INT64_C(1) << 47);
                core->machl = core->mac.result;
                uint32_t result = core->mac_overflow
                    ? (core->machl < 0 ? 0x800000 : 0x7fffff)
                    : ((uint64_t)core->mac.result >> 24) & 0xffffff;
                if (core->mac.dst & SRC_REG)
                    es5510_core_write_reg(core, core->mac.c_reg, result);
                if (core->mac.dst & SRC_DELAY) write_dol(core, result);
            }

            core->mac.c_reg = (instruction >> 32) & 0xff;
            core->mac.d_reg = (instruction >> 40) & 0xff;
            core->mac.src = select.mac_src;
            core->mac.dst = select.mac_dst;
            core->mac.accumulate = (instruction >> 6) & 1;
            core->mac.write_result = !skip;
            core->mac.c_value = core->mac.src == SRC_REG
                ? es5510_core_read_reg(core, core->mac.c_reg)
                : (uint32_t)(uint16_t)core->dil << 8;
            core->mac.d_value = es5510_core_read_reg(core, core->mac.d_reg);

            if (core->alu.write_result) {
                uint8_t flags = core->ccr;
                core->alu.result = alu_operation(core->alu.op, core->alu.a_value,
                                                 core->alu.b_value, &flags);
                if (core->alu.op != 4) {
                    if (core->alu.dst & SRC_REG)
                        es5510_core_write_reg(core, core->alu.a_reg, core->alu.result);
                    if (core->alu.dst & SRC_DELAY) write_dol(core, core->alu.result);
                }
                if (core->alu.update_ccr) core->ccr = flags;
            }

            core->alu.a_reg = (instruction >> 16) & 0xff;
            core->alu.b_reg = (instruction >> 24) & 0xff;
            core->alu.op = (instruction >> 12) & 15;
            core->alu.src = select.alu_src;
            core->alu.dst = select.alu_dst;
            core->alu.write_result = !skip || core->alu.op == 4;
            core->alu.update_ccr = !skippable || core->alu.op == 4;
            int end = core->alu.op == 15;
            if (!end) {
                int operands = core->alu.op >= 8 ? 1 : 2;
                if (operands == 1) {
                    core->alu.b_value = core->alu.src == SRC_REG
                        ? es5510_core_read_reg(core, core->alu.b_reg)
                        : (uint32_t)(uint16_t)core->dil << 8;
                } else {
                    core->alu.a_value = core->alu.src == SRC_REG
                        ? es5510_core_read_reg(core, core->alu.a_reg)
                        : (uint32_t)(uint16_t)core->dil << 8;
                    core->alu.b_value = es5510_core_read_reg(core, core->alu.b_reg);
                }
            }

            if (core->ram_p.cycle != RAM_READ) {
                if (core->ram_p.cycle == RAM_WRITE && !core->ram_p.io && core->dol_count)
                    core->dram[(unsigned int)core->ram_p.address & (ES5510_DRAM_WORDS - 1)] = core->dol[0];
                eject_dol(core);
            }
            ++core->instructions_executed;
            if (end) {
                int64_t next = (int64_t)core->dbase - core->memincrement;
                core->dbase = next < 0 ? core->dlength : (uint32_t)next;
                break;
            }
        }
        ++core->frames;
    }
    outputs[0] = 0;
    outputs[1] = 0;
    for (unsigned int port = 0; port < 4; ++port) {
        if (core->host_serial & (uint8_t)(0x04U << port)) {
            outputs[0] = core->serial[port * 2];
            outputs[1] = core->serial[port * 2 + 1];
            break;
        }
    }
}
