// Copyright 2012 Michael Kang, 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#define CITRA_IGNORE_EXIT(x)

#include <algorithm>
#include <unordered_map>
#include <stdio.h>
#include <assert.h>
#include <cstdio>
#include <vector>

using namespace std;

#include "core/arm/skyeye_common/armdefs.h"
#include "core/arm/skyeye_common/armmmu.h"
#include "arm_dyncom_thumb.h"
#include "arm_dyncom_run.h"
#include "core/arm/skyeye_common/vfp/vfp.h"
#include "core/arm/disassembler/arm_disasm.h"

#include "core/mem_map.h"
#include "core/hle/hle.h"

enum {
    COND            = (1 << 0),
    NON_BRANCH      = (1 << 1),
    DIRECT_BRANCH   = (1 << 2),
    INDIRECT_BRANCH = (1 << 3),
    CALL            = (1 << 4),
    RET             = (1 << 5),
    END_OF_PAGE     = (1 << 6),
    THUMB           = (1 << 7)
};

#define RM    BITS(sht_oper, 0, 3)
#define RS    BITS(sht_oper, 8, 11)

#define glue(x, y)            x ## y
#define DPO(s)                glue(DataProcessingOperands, s)
#define ROTATE_RIGHT(n, i, l) ((n << (l - i)) | (n >> i))
#define ROTATE_LEFT(n, i, l)  ((n >> (l - i)) | (n << i))
#define ROTATE_RIGHT_32(n, i) ROTATE_RIGHT(n, i, 32)
#define ROTATE_LEFT_32(n, i)  ROTATE_LEFT(n, i, 32)

typedef arm_core_t arm_processor;
typedef unsigned int (*shtop_fp_t)(arm_processor *cpu, unsigned int sht_oper);

// Defines a reservation granule of 2 words, which protects the first 2 words starting at the tag.
// This is the smallest granule allowed by the v7 spec, and is coincidentally just large enough to
// support LDR/STREXD.
static const ARMword RESERVATION_GRANULE_MASK = 0xFFFFFFF8;

// Exclusive memory access
static int exclusive_detect(ARMul_State* state, ARMword addr){
    if(state->exclusive_tag == (addr & RESERVATION_GRANULE_MASK))
        return 0;
    else
        return -1;
}

static void add_exclusive_addr(ARMul_State* state, ARMword addr){
    state->exclusive_tag = addr & RESERVATION_GRANULE_MASK;
    return;
}

static void remove_exclusive(ARMul_State* state, ARMword addr){
    state->exclusive_tag = 0xFFFFFFFF;
}

unsigned int DPO(Immediate)(arm_processor *cpu, unsigned int sht_oper) {
    unsigned int immed_8 = BITS(sht_oper, 0, 7);
    unsigned int rotate_imm = BITS(sht_oper, 8, 11);
    unsigned int shifter_operand = ROTATE_RIGHT_32(immed_8, rotate_imm * 2);
    if (rotate_imm == 0) 
        cpu->shifter_carry_out = cpu->CFlag;
    else
        cpu->shifter_carry_out = BIT(shifter_operand, 31);
    return shifter_operand;
}

unsigned int DPO(Register)(arm_processor *cpu, unsigned int sht_oper) {
    unsigned int rm = CHECK_READ_REG15(cpu, RM);
    unsigned int shifter_operand = rm;
    cpu->shifter_carry_out = cpu->CFlag;
    return shifter_operand;
}

unsigned int DPO(LogicalShiftLeftByImmediate)(arm_processor *cpu, unsigned int sht_oper) {
    int shift_imm = BITS(sht_oper, 7, 11);
    unsigned int rm = CHECK_READ_REG15(cpu, RM);
    unsigned int shifter_operand;
    if (shift_imm == 0) {
        shifter_operand = rm;
        cpu->shifter_carry_out = cpu->CFlag;
    } else {
        shifter_operand = rm << shift_imm;
        cpu->shifter_carry_out = BIT(rm, 32 - shift_imm);
    }
    return shifter_operand;
}

unsigned int DPO(LogicalShiftLeftByRegister)(arm_processor *cpu, unsigned int sht_oper) {
    int shifter_operand;
    unsigned int rm = CHECK_READ_REG15(cpu, RM);
    unsigned int rs = CHECK_READ_REG15(cpu, RS);
    if (BITS(rs, 0, 7) == 0) {
        shifter_operand = rm;
        cpu->shifter_carry_out = cpu->CFlag;
    } else if (BITS(rs, 0, 7) < 32) {
        shifter_operand = rm << BITS(rs, 0, 7);
        cpu->shifter_carry_out = BIT(rm, 32 - BITS(rs, 0, 7));
    } else if (BITS(rs, 0, 7) == 32) {
        shifter_operand = 0;
        cpu->shifter_carry_out = BIT(rm, 0);
    } else {
        shifter_operand = 0;
        cpu->shifter_carry_out = 0;
    }
    return shifter_operand;
}

unsigned int DPO(LogicalShiftRightByImmediate)(arm_processor *cpu, unsigned int sht_oper) {
    unsigned int rm = CHECK_READ_REG15(cpu, RM);
    unsigned int shifter_operand;
    int shift_imm = BITS(sht_oper, 7, 11);
    if (shift_imm == 0) {
        shifter_operand = 0;
        cpu->shifter_carry_out = BIT(rm, 31);
    } else {
        shifter_operand = rm >> shift_imm;
        cpu->shifter_carry_out = BIT(rm, shift_imm - 1);
    }
    return shifter_operand;
}

unsigned int DPO(LogicalShiftRightByRegister)(arm_processor *cpu, unsigned int sht_oper) {
    unsigned int rs = CHECK_READ_REG15(cpu, RS);
    unsigned int rm = CHECK_READ_REG15(cpu, RM);
    unsigned int shifter_operand;
    if (BITS(rs, 0, 7) == 0) {
        shifter_operand = rm;
        cpu->shifter_carry_out = cpu->CFlag;
    } else if (BITS(rs, 0, 7) < 32) {
        shifter_operand = rm >> BITS(rs, 0, 7);
        cpu->shifter_carry_out = BIT(rm, BITS(rs, 0, 7) - 1);
    } else if (BITS(rs, 0, 7) == 32) {
        shifter_operand = 0;
        cpu->shifter_carry_out = BIT(rm, 31);
    } else {
        shifter_operand = 0;
        cpu->shifter_carry_out = 0;
    }
    return shifter_operand;
}

unsigned int DPO(ArithmeticShiftRightByImmediate)(arm_processor *cpu, unsigned int sht_oper) {
    unsigned int rm = CHECK_READ_REG15(cpu, RM);
    unsigned int shifter_operand;
    int shift_imm = BITS(sht_oper, 7, 11);
    if (shift_imm == 0) {
        if (BIT(rm, 31) == 0)
            shifter_operand = 0;
        else
            shifter_operand = 0xFFFFFFFF;
        cpu->shifter_carry_out = BIT(rm, 31);
    } else {
        shifter_operand = static_cast<int>(rm) >> shift_imm;
        cpu->shifter_carry_out = BIT(rm, shift_imm - 1);
    }
    return shifter_operand;
}

unsigned int DPO(ArithmeticShiftRightByRegister)(arm_processor *cpu, unsigned int sht_oper) {
    unsigned int rs = CHECK_READ_REG15(cpu, RS);
    unsigned int rm = CHECK_READ_REG15(cpu, RM);
    unsigned int shifter_operand;
    if (BITS(rs, 0, 7) == 0) {
        shifter_operand = rm;
        cpu->shifter_carry_out = cpu->CFlag;
    } else if (BITS(rs, 0, 7) < 32) {
        shifter_operand = static_cast<int>(rm) >> BITS(rs, 0, 7);
        cpu->shifter_carry_out = BIT(rm, BITS(rs, 0, 7) - 1);
    } else {
        if (BIT(rm, 31) == 0)
            shifter_operand = 0;
        else
            shifter_operand = 0xffffffff;
        cpu->shifter_carry_out = BIT(rm, 31);
    }
    return shifter_operand;
}

unsigned int DPO(RotateRightByImmediate)(arm_processor *cpu, unsigned int sht_oper) {
    unsigned int shifter_operand;
    unsigned int rm = CHECK_READ_REG15(cpu, RM);
    int shift_imm = BITS(sht_oper, 7, 11);
    if (shift_imm == 0) {
        shifter_operand = (cpu->CFlag << 31) | (rm >> 1);
        cpu->shifter_carry_out = BIT(rm, 0);
    } else {
        shifter_operand = ROTATE_RIGHT_32(rm, shift_imm);
        cpu->shifter_carry_out = BIT(rm, shift_imm - 1);
    }
    return shifter_operand;
}

unsigned int DPO(RotateRightByRegister)(arm_processor *cpu, unsigned int sht_oper) {
    unsigned int rm = CHECK_READ_REG15(cpu, RM);
    unsigned int rs = CHECK_READ_REG15(cpu, RS);
    unsigned int shifter_operand;
    if (BITS(rs, 0, 7) == 0) {
        shifter_operand = rm;
        cpu->shifter_carry_out = cpu->CFlag;
    } else if (BITS(rs, 0, 4) == 0) {
        shifter_operand = rm;
        cpu->shifter_carry_out = BIT(rm, 31);
    } else {
        shifter_operand = ROTATE_RIGHT_32(rm, BITS(rs, 0, 4));
        cpu->shifter_carry_out = BIT(rm, BITS(rs, 0, 4) - 1);
    }
    return shifter_operand;
}

typedef void (*get_addr_fp_t)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw);

typedef struct _ldst_inst {
    unsigned int inst;
    get_addr_fp_t get_addr;
} ldst_inst;
#define DEBUG_MSG LOG_DEBUG(Core_ARM11, "inst is %x", inst); CITRA_IGNORE_EXIT(0)

int CondPassed(arm_processor *cpu, unsigned int cond);

#define LnSWoUB(s)   glue(LnSWoUB, s)
#define MLnS(s)      glue(MLnS, s)
#define LdnStM(s)    glue(LdnStM, s)

#define W_BIT        BIT(inst, 21)
#define U_BIT        BIT(inst, 23)
#define I_BIT        BIT(inst, 25)
#define P_BIT        BIT(inst, 24)
#define OFFSET_12    BITS(inst, 0, 11)

void LnSWoUB(ImmediateOffset)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int addr;

    if (U_BIT)
        addr = CHECK_READ_REG15_WA(cpu, Rn) + OFFSET_12;
    else
        addr = CHECK_READ_REG15_WA(cpu, Rn) - OFFSET_12;

    virt_addr = addr;
}

void LnSWoUB(RegisterOffset)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int Rm = BITS(inst, 0, 3);
    unsigned int rn = CHECK_READ_REG15_WA(cpu, Rn);
    unsigned int rm = CHECK_READ_REG15_WA(cpu, Rm);
    unsigned int addr;

    if (U_BIT)
        addr = rn + rm;
    else
        addr = rn - rm;

    virt_addr = addr;
}

void LnSWoUB(ImmediatePostIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int addr = CHECK_READ_REG15_WA(cpu, Rn);

    if (U_BIT)
        cpu->Reg[Rn] += OFFSET_12;
    else
        cpu->Reg[Rn] -= OFFSET_12;

    virt_addr = addr;
}

void LnSWoUB(ImmediatePreIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int addr;

    if (U_BIT)
        addr = CHECK_READ_REG15_WA(cpu, Rn) + OFFSET_12;
    else
        addr = CHECK_READ_REG15_WA(cpu, Rn) - OFFSET_12;

    virt_addr = addr;

    if (CondPassed(cpu, BITS(inst, 28, 31)))
        cpu->Reg[Rn] = addr;
}

void MLnS(RegisterPreIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int addr;
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int Rm = BITS(inst,  0,  3);
    unsigned int rn = CHECK_READ_REG15_WA(cpu, Rn);
    unsigned int rm = CHECK_READ_REG15_WA(cpu, Rm);

    if (U_BIT)
        addr = rn + rm;
    else
        addr = rn - rm;

    virt_addr = addr;

    if (CondPassed(cpu, BITS(inst, 28, 31)))
        cpu->Reg[Rn] = addr;
}

void LnSWoUB(RegisterPreIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int Rm = BITS(inst, 0, 3);
    unsigned int rn = CHECK_READ_REG15_WA(cpu, Rn);
    unsigned int rm = CHECK_READ_REG15_WA(cpu, Rm);
    unsigned int addr;

    if (U_BIT)
        addr = rn + rm;
    else
        addr = rn - rm;

    virt_addr = addr;

    if (CondPassed(cpu, BITS(inst, 28, 31))) {
        cpu->Reg[Rn] = addr;
    }
}

void LnSWoUB(ScaledRegisterPreIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int shift = BITS(inst, 5, 6);
    unsigned int shift_imm = BITS(inst, 7, 11);
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int Rm = BITS(inst, 0, 3);
    unsigned int index;
    unsigned int addr;
    unsigned int rm = CHECK_READ_REG15_WA(cpu, Rm);
    unsigned int rn = CHECK_READ_REG15_WA(cpu, Rn);

    switch (shift) {
    case 0:
        index = rm << shift_imm;
        break;
    case 1:
        if (shift_imm == 0) {
            index = 0;
        } else {
            index = rm >> shift_imm;
        }
        break;
    case 2:
        if (shift_imm == 0) { // ASR #32
            if (BIT(rm, 31) == 1)
                index = 0xFFFFFFFF;
            else
                index = 0;
        } else {
            index = static_cast<int>(rm) >> shift_imm;
        }
        break;
    case 3:
        if (shift_imm == 0) {
            index = (cpu->CFlag << 31) | (rm >> 1);
        } else {
            index = ROTATE_RIGHT_32(rm, shift_imm);
        }
        break;
    }

    if (U_BIT)
        addr = rn + index;
    else
        addr = rn - index;

    virt_addr = addr;

    if (CondPassed(cpu, BITS(inst, 28, 31)))
        cpu->Reg[Rn] = addr;
}

void LnSWoUB(ScaledRegisterPostIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int shift = BITS(inst, 5, 6);
    unsigned int shift_imm = BITS(inst, 7, 11);
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int Rm = BITS(inst, 0, 3);
    unsigned int index;
    unsigned int addr = CHECK_READ_REG15_WA(cpu, Rn);
    unsigned int rm = CHECK_READ_REG15_WA(cpu, Rm);

    switch (shift) {
    case 0:
        index = rm << shift_imm;
        break;
    case 1:
        if (shift_imm == 0) {
            index = 0;
        } else {
            index = rm >> shift_imm;
        }
        break;
    case 2:
        if (shift_imm == 0) { // ASR #32
            if (BIT(rm, 31) == 1)
                index = 0xFFFFFFFF;
            else
                index = 0;
        } else {
            index = static_cast<int>(rm) >> shift_imm;
        }
        break;
    case 3:
        if (shift_imm == 0) {
            index = (cpu->CFlag << 31) | (rm >> 1);
        } else {
            index = ROTATE_RIGHT_32(rm, shift_imm);
        }
        break;
    }

    virt_addr = addr;

    if (CondPassed(cpu, BITS(inst, 28, 31))) {
        if (U_BIT)
            cpu->Reg[Rn] += index;
        else
            cpu->Reg[Rn] -= index;
    }
}

void LnSWoUB(RegisterPostIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int Rm = BITS(inst,  0,  3);
    unsigned int rm = CHECK_READ_REG15_WA(cpu, Rm);

    virt_addr = CHECK_READ_REG15_WA(cpu, Rn);

    if (CondPassed(cpu, BITS(inst, 28, 31))) {
        if (U_BIT) {
            cpu->Reg[Rn] += rm;
        } else {
            cpu->Reg[Rn] -= rm;
        }
    }
}

void MLnS(ImmediateOffset)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int immedL = BITS(inst, 0, 3);
    unsigned int immedH = BITS(inst, 8, 11);
    unsigned int Rn     = BITS(inst, 16, 19);
    unsigned int addr;

    unsigned int offset_8 = (immedH << 4) | immedL;

    if (U_BIT)
        addr = CHECK_READ_REG15_WA(cpu, Rn) + offset_8;
    else
        addr = CHECK_READ_REG15_WA(cpu, Rn) - offset_8;
    
    virt_addr = addr;
}

void MLnS(RegisterOffset)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int addr;
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int Rm = BITS(inst,  0,  3);
    unsigned int rn = CHECK_READ_REG15_WA(cpu, Rn);
    unsigned int rm = CHECK_READ_REG15_WA(cpu, Rm);

    if (U_BIT)
        addr = rn + rm;
    else
        addr = rn - rm;

    virt_addr = addr;
}

void MLnS(ImmediatePreIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn     = BITS(inst, 16, 19);
    unsigned int immedH = BITS(inst,  8, 11);
    unsigned int immedL = BITS(inst,  0,  3);
    unsigned int addr;
    unsigned int rn = CHECK_READ_REG15_WA(cpu, Rn);
    unsigned int offset_8 = (immedH << 4) | immedL;

    if (U_BIT)
        addr = rn + offset_8;
    else 
        addr = rn - offset_8;

    virt_addr = addr;

    if (CondPassed(cpu, BITS(inst, 28, 31)))
        cpu->Reg[Rn] = addr;
}

void MLnS(ImmediatePostIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn     = BITS(inst, 16, 19);
    unsigned int immedH = BITS(inst,  8, 11);
    unsigned int immedL = BITS(inst,  0,  3);
    unsigned int rn = CHECK_READ_REG15_WA(cpu, Rn);

    virt_addr = rn;

    if (CondPassed(cpu, BITS(inst, 28, 31))) {
        unsigned int offset_8 = (immedH << 4) | immedL;
        if (U_BIT)
            rn += offset_8;
        else
            rn -= offset_8;

        cpu->Reg[Rn] = rn;
    }
}

void MLnS(RegisterPostIndexed)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int Rm = BITS(inst,  0,  3);
    unsigned int rm = CHECK_READ_REG15_WA(cpu, Rm);

    virt_addr = CHECK_READ_REG15_WA(cpu, Rn);

    if (CondPassed(cpu, BITS(inst, 28, 31))) {
        if (U_BIT)
            cpu->Reg[Rn] += rm;
        else
            cpu->Reg[Rn] -= rm;
    }
}

void LdnStM(DecrementBefore)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int i = BITS(inst, 0, 15);
    int count = 0;

    while (i) {
        if (i & 1) count++;
        i = i >> 1;
    }

    virt_addr = CHECK_READ_REG15_WA(cpu, Rn) - count * 4;

    if (CondPassed(cpu, BITS(inst, 28, 31)) && BIT(inst, 21))
        cpu->Reg[Rn] -= count * 4;
}

void LdnStM(IncrementBefore)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int i = BITS(inst, 0, 15);
    int count = 0;

    while (i) {
        if (i & 1) count++;
        i = i >> 1;
    }

    virt_addr = CHECK_READ_REG15_WA(cpu, Rn) + 4;

    if (CondPassed(cpu, BITS(inst, 28, 31)) && BIT(inst, 21))
        cpu->Reg[Rn] += count * 4;
}

void LdnStM(IncrementAfter)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int i = BITS(inst, 0, 15);
    int count = 0;

    while(i) {
        if (i & 1) count++;
        i = i >> 1;
    }

    virt_addr = CHECK_READ_REG15_WA(cpu, Rn);

    if (CondPassed(cpu, BITS(inst, 28, 31)) && BIT(inst, 21))
        cpu->Reg[Rn] += count * 4;
}

void LdnStM(DecrementAfter)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int i = BITS(inst, 0, 15);
    int count = 0;
    while(i) {
        if(i & 1) count++;
        i = i >> 1;
    }
    unsigned int rn = CHECK_READ_REG15_WA(cpu, Rn);
    unsigned int start_addr = rn - count * 4 + 4;

    virt_addr = start_addr;

    if (CondPassed(cpu, BITS(inst, 28, 31)) && BIT(inst, 21)) {
        cpu->Reg[Rn] -= count * 4;
    }
}

void LnSWoUB(ScaledRegisterOffset)(arm_processor *cpu, unsigned int inst, unsigned int &virt_addr, unsigned int rw) {
    unsigned int shift = BITS(inst, 5, 6);
    unsigned int shift_imm = BITS(inst, 7, 11);
    unsigned int Rn = BITS(inst, 16, 19);
    unsigned int Rm = BITS(inst, 0, 3);
    unsigned int index;
    unsigned int addr;
    unsigned int rm = CHECK_READ_REG15_WA(cpu, Rm);
    unsigned int rn = CHECK_READ_REG15_WA(cpu, Rn);

    switch (shift) {
    case 0:
        index = rm << shift_imm;
        break;
    case 1:
        if (shift_imm == 0) {
            index = 0;
        } else {
            index = rm >> shift_imm;
        }
        break;
    case 2:
        if (shift_imm == 0) { // ASR #32
            if (BIT(rm, 31) == 1)
                index = 0xFFFFFFFF;
            else
                index = 0;
        } else {
            index = static_cast<int>(rm) >> shift_imm;
        }
        break;
    case 3:
        if (shift_imm == 0) {
            index = (cpu->CFlag << 31) | (rm >> 1);
        } else {
            index = ROTATE_RIGHT_32(rm, shift_imm);
        }
        break;
    }

    if (U_BIT) {
        addr = rn + index;
    } else
        addr = rn - index;

    virt_addr = addr;
}

typedef struct _arm_inst {
    unsigned int idx;
    unsigned int cond;
    int br;
    int load_r15;
    char component[0];
} arm_inst;

typedef struct generic_arm_inst {
    u32 Ra;
    u32 Rm;
    u32 Rn;
    u32 Rd;
    u8 op1;
    u8 op2;
} generic_arm_inst;

typedef struct _adc_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} adc_inst;

typedef struct _add_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} add_inst;

typedef struct _orr_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} orr_inst;

typedef struct _and_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} and_inst;

typedef struct _eor_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} eor_inst;

typedef struct _bbl_inst {
    unsigned int L;
    int signed_immed_24;
    unsigned int next_addr;
    unsigned int jmp_addr;
} bbl_inst;

typedef struct _bx_inst {
    unsigned int Rm;
} bx_inst;

typedef struct _blx_inst {
    union {
        int32_t signed_immed_24;
        uint32_t Rm;
    } val;
    unsigned int inst;
} blx_inst;

typedef struct _clz_inst {
    unsigned int Rm;
    unsigned int Rd;
} clz_inst;

typedef struct _cps_inst {
    unsigned int imod0;
    unsigned int imod1;
    unsigned int mmod;
    unsigned int A, I, F;
    unsigned int mode;
} cps_inst;

typedef struct _clrex_inst {
} clrex_inst;

typedef struct _cpy_inst {
    unsigned int Rm;
    unsigned int Rd;
} cpy_inst;

typedef struct _bic_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} bic_inst;

typedef struct _sub_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} sub_inst;

typedef struct _tst_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} tst_inst;

typedef struct _cmn_inst {
    unsigned int I;
    unsigned int Rn;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} cmn_inst;

typedef struct _teq_inst {
    unsigned int I;
    unsigned int Rn;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} teq_inst;

typedef struct _stm_inst {
    unsigned int inst;
} stm_inst;

struct bkpt_inst {
};

struct blx1_inst {
    unsigned int addr;
};

struct blx2_inst {
    unsigned int Rm;
};

typedef struct _stc_inst {
} stc_inst;

typedef struct _ldc_inst {
} ldc_inst;

typedef struct _swi_inst {
    unsigned int num;
} swi_inst;

typedef struct _cmp_inst {
    unsigned int I;
    unsigned int Rn;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} cmp_inst;

typedef struct _mov_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} mov_inst;

typedef struct _mvn_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} mvn_inst;

typedef struct _rev_inst {
    unsigned int Rd;
    unsigned int Rm;
    unsigned int op1;
    unsigned int op2;
} rev_inst;

typedef struct _rsb_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} rsb_inst;

typedef struct _rsc_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} rsc_inst;

typedef struct _sbc_inst {
    unsigned int I;
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int shifter_operand;
    shtop_fp_t shtop_func;
} sbc_inst;

typedef struct _mul_inst {
    unsigned int S;
    unsigned int Rd;
    unsigned int Rs;
    unsigned int Rm;
} mul_inst;

typedef struct _smul_inst {
    unsigned int Rd;
    unsigned int Rs;
    unsigned int Rm;
    unsigned int x;
    unsigned int y;
} smul_inst;

typedef struct _umull_inst {
    unsigned int S;
    unsigned int RdHi;
    unsigned int RdLo;
    unsigned int Rs;
    unsigned int Rm;
} umull_inst;
typedef struct _smlad_inst {
    unsigned int m;
    unsigned int Rm;
    unsigned int Rd;
    unsigned int Ra;
    unsigned int Rn;
    unsigned int op1;
    unsigned int op2;
} smlad_inst;

typedef struct _smla_inst {
    unsigned int x;
    unsigned int y;
    unsigned int Rm;
    unsigned int Rd;
    unsigned int Rs;
    unsigned int Rn;
} smla_inst;

typedef struct smlalxy_inst {
    unsigned int x;
    unsigned int y;
    unsigned int RdLo;
    unsigned int RdHi;
    unsigned int Rm;
    unsigned int Rn;
} smlalxy_inst;

typedef struct ssat_inst {
    unsigned int Rn;
    unsigned int Rd;
    unsigned int imm5;
    unsigned int sat_imm;
    unsigned int shift_type;
} ssat_inst;

typedef struct umaal_inst {
    unsigned int Rn;
    unsigned int Rm;
    unsigned int RdHi;
    unsigned int RdLo;
} umaal_inst;

typedef struct _umlal_inst {
    unsigned int S;
    unsigned int Rm;
    unsigned int Rs;
    unsigned int RdHi;
    unsigned int RdLo;
} umlal_inst;

typedef struct _smlal_inst {
    unsigned int S;
    unsigned int Rm;
    unsigned int Rs;
    unsigned int RdHi;
    unsigned int RdLo;
} smlal_inst;

typedef struct smlald_inst {
    unsigned int RdLo;
    unsigned int RdHi;
    unsigned int Rm;
    unsigned int Rn;
    unsigned int swap;
    unsigned int op1;
    unsigned int op2;
} smlald_inst;

typedef struct _mla_inst {
    unsigned int S;
    unsigned int Rn;
    unsigned int Rd;
    unsigned int Rs;
    unsigned int Rm;
} mla_inst;

typedef struct _mrc_inst {
    unsigned int opcode_1;
    unsigned int opcode_2;
    unsigned int cp_num;
    unsigned int crn;
    unsigned int crm;
    unsigned int Rd;
    unsigned int inst;
} mrc_inst;

typedef struct _mcr_inst {
    unsigned int opcode_1;
    unsigned int opcode_2;
    unsigned int cp_num;
    unsigned int crn;
    unsigned int crm;
    unsigned int Rd;
    unsigned int inst;
} mcr_inst;

typedef struct _mrs_inst {
    unsigned int R;
    unsigned int Rd;
} mrs_inst;

typedef struct _msr_inst {
    unsigned int field_mask;
    unsigned int R;
    unsigned int inst;
} msr_inst;

typedef struct _pld_inst {
} pld_inst;

typedef struct _sxtb_inst {
    unsigned int Rd;
    unsigned int Rm;
    unsigned int rotate;
} sxtb_inst;

typedef struct _sxtab_inst {
    unsigned int Rd;
    unsigned int Rn;
    unsigned int Rm;
    unsigned rotate;
} sxtab_inst;

typedef struct _sxtah_inst {
    unsigned int Rd;
    unsigned int Rn;
    unsigned int Rm;
    unsigned int rotate;
} sxtah_inst;

typedef struct _sxth_inst {
    unsigned int Rd;
    unsigned int Rm;
    unsigned int rotate;
} sxth_inst;

typedef struct _uxtab_inst {
    unsigned int Rn;
    unsigned int Rd;
    unsigned int rotate;
    unsigned int Rm;
} uxtab_inst;

typedef struct _uxtah_inst {
    unsigned int Rn;
    unsigned int Rd;
    unsigned int rotate;
    unsigned int Rm;
} uxtah_inst;

typedef struct _uxth_inst {
    unsigned int Rd;
    unsigned int Rm;
    unsigned int rotate;
} uxth_inst;

typedef struct _cdp_inst {
    unsigned int opcode_1;
    unsigned int CRn;
    unsigned int CRd;
    unsigned int cp_num;
    unsigned int opcode_2;
    unsigned int CRm;
    unsigned int inst;
}cdp_inst;

typedef struct _uxtb_inst {
    unsigned int Rd;
    unsigned int Rm;
    unsigned int rotate;
} uxtb_inst;

typedef struct _swp_inst {
    unsigned int Rn;
    unsigned int Rd;
    unsigned int Rm;
} swp_inst;

typedef struct _b_2_thumb {
    unsigned int imm;
}b_2_thumb;
typedef struct _b_cond_thumb {
    unsigned int imm;
    unsigned int cond;
}b_cond_thumb;

typedef struct _bl_1_thumb {
    unsigned int imm;
}bl_1_thumb;
typedef struct _bl_2_thumb {
    unsigned int imm;
}bl_2_thumb;
typedef struct _blx_1_thumb {
    unsigned int imm;
    unsigned int instr;
}blx_1_thumb;

typedef struct _pkh_inst {
    unsigned int Rm;
    unsigned int Rn;
    unsigned int Rd;
    unsigned char imm;
} pkh_inst;

typedef arm_inst * ARM_INST_PTR;

#define CACHE_BUFFER_SIZE    (64 * 1024 * 2000)
char inst_buf[CACHE_BUFFER_SIZE];
int top = 0;
inline void *AllocBuffer(unsigned int size) {
    int start = top;
    top += size;
    if (top > CACHE_BUFFER_SIZE) {
        LOG_ERROR(Core_ARM11, "inst_buf is full");
        CITRA_IGNORE_EXIT(-1);
    }
    return (void *)&inst_buf[start];
}

int CondPassed(arm_processor *cpu, unsigned int cond) {
    #define NFLAG        cpu->NFlag
    #define ZFLAG        cpu->ZFlag
    #define CFLAG        cpu->CFlag
    #define VFLAG        cpu->VFlag

    int temp;

    switch (cond) {
    case 0x0:
        temp = ZFLAG;
        break;
    case 0x1: // NE
        temp = !ZFLAG;
        break;
    case 0x6: // VS
        temp = VFLAG;
        break;
    case 0x7: // VC
        temp = !VFLAG;
        break;
    case 0x4: // MI
        temp = NFLAG;
        break;
    case 0x5: // PL
        temp = !NFLAG;
        break;
    case 0x2: // CS
        temp = CFLAG;
        break;
    case 0x3: // CC
        temp = !CFLAG;
        break;
    case 0x8: // HI
        temp = (CFLAG && !ZFLAG);
        break;
    case 0x9: // LS
        temp = (!CFLAG || ZFLAG);
        break;
    case 0xa: // GE
        temp = ((!NFLAG && !VFLAG) || (NFLAG && VFLAG));
        break;
    case 0xb: // LT
        temp = ((NFLAG && !VFLAG) || (!NFLAG && VFLAG));
        break;
    case 0xc: // GT
        temp = ((!NFLAG && !VFLAG && !ZFLAG) || (NFLAG && VFLAG && !ZFLAG));
        break;
    case 0xd: // LE
        temp = ((NFLAG && !VFLAG) || (!NFLAG && VFLAG)) || ZFLAG;
        break;
    case 0xe: // AL
        temp = 1;
        break;
    case 0xf:
        temp = 1;
        break;
    }
    return temp;
}

enum DECODE_STATUS {
    DECODE_SUCCESS,
    DECODE_FAILURE
};

int decode_arm_instr(uint32_t instr, int32_t *idx);

shtop_fp_t get_shtop(unsigned int inst) {
    if (BIT(inst, 25)) {
        return DPO(Immediate);
    } else if (BITS(inst, 4, 11) == 0) {
        return DPO(Register);
    } else if (BITS(inst, 4, 6) == 0) {
        return DPO(LogicalShiftLeftByImmediate);
    } else if (BITS(inst, 4, 7) == 1) {
        return DPO(LogicalShiftLeftByRegister);
    } else if (BITS(inst, 4, 6) == 2) {
        return DPO(LogicalShiftRightByImmediate);
    } else if (BITS(inst, 4, 7) == 3) {
        return DPO(LogicalShiftRightByRegister);
    } else if (BITS(inst, 4, 6) == 4) {
        return DPO(ArithmeticShiftRightByImmediate);
    } else if (BITS(inst, 4, 7) == 5) {
        return DPO(ArithmeticShiftRightByRegister);
    } else if (BITS(inst, 4, 6) == 6) {
        return DPO(RotateRightByImmediate);
    } else if (BITS(inst, 4, 7) == 7) {
        return DPO(RotateRightByRegister);
    }
    return nullptr;
}

get_addr_fp_t get_calc_addr_op(unsigned int inst) {
    if (BITS(inst, 24, 27) == 5 && BIT(inst, 21) == 0) {
        return LnSWoUB(ImmediateOffset);
    } else if (BITS(inst, 24, 27) == 7 && BIT(inst, 21) == 0 && BITS(inst, 4, 11) == 0) {
        return LnSWoUB(RegisterOffset);
    } else if (BITS(inst, 24, 27) == 7 && BIT(inst, 21) == 0 && BIT(inst, 4) == 0) {
        return LnSWoUB(ScaledRegisterOffset);
    } else if (BITS(inst, 24, 27) == 5 && BIT(inst, 21) == 1) {
        return LnSWoUB(ImmediatePreIndexed);
    } else if (BITS(inst, 24, 27) == 7 && BIT(inst, 21) == 1 && BITS(inst, 4, 11) == 0) {
        return LnSWoUB(RegisterPreIndexed);
    } else if (BITS(inst, 24, 27) == 7 && BIT(inst, 21) == 1 && BIT(inst, 4) == 0) {
        return LnSWoUB(ScaledRegisterPreIndexed);
    } else if (BITS(inst, 24, 27) == 4 && BIT(inst, 21) == 0) {
        return LnSWoUB(ImmediatePostIndexed);
    } else if (BITS(inst, 24, 27) == 6 && BIT(inst, 21) == 0 && BITS(inst, 4, 11) == 0) {
        return LnSWoUB(RegisterPostIndexed);
    } else if (BITS(inst, 24, 27) == 6 && BIT(inst, 21) == 0 && BIT(inst, 4) == 0) {
        return LnSWoUB(ScaledRegisterPostIndexed);
    } else if (BITS(inst, 24, 27) == 1 && BITS(inst, 21, 22) == 2 && BIT(inst, 7) == 1 && BIT(inst, 4) == 1) {
        return MLnS(ImmediateOffset);
    } else if (BITS(inst, 24, 27) == 1 && BITS(inst, 21, 22) == 0 && BIT(inst, 7) == 1 && BIT(inst, 4) == 1) {
        return MLnS(RegisterOffset);
    } else if (BITS(inst, 24, 27) == 1 && BITS(inst, 21, 22) == 3 && BIT(inst, 7) == 1 && BIT(inst, 4) == 1) {
        return MLnS(ImmediatePreIndexed);
    } else if (BITS(inst, 24, 27) == 1 && BITS(inst, 21, 22) == 1 && BIT(inst, 7) == 1 && BIT(inst, 4) == 1) {
        return MLnS(RegisterPreIndexed);
    } else if (BITS(inst, 24, 27) == 0 && BITS(inst, 21, 22) == 2 && BIT(inst, 7) == 1 && BIT(inst, 4) == 1) {
        return MLnS(ImmediatePostIndexed);
    } else if (BITS(inst, 24, 27) == 0 && BITS(inst, 21, 22) == 0 && BIT(inst, 7) == 1 && BIT(inst, 4) == 1) {
        return MLnS(RegisterPostIndexed);
    } else if (BITS(inst, 23, 27) == 0x11) {
        return LdnStM(IncrementAfter);
    } else if (BITS(inst, 23, 27) == 0x13) {
        return LdnStM(IncrementBefore);
    } else if (BITS(inst, 23, 27) == 0x10) {
        return LdnStM(DecrementAfter);
    } else if (BITS(inst, 23, 27) == 0x12) {
        return LdnStM(DecrementBefore);
    }
    return nullptr;
}

#define INTERPRETER_TRANSLATE(s) glue(InterpreterTranslate_, s)

#define CHECK_RN            (inst_cream->Rn == 15)
#define CHECK_RM            (inst_cream->Rm == 15)
#define CHECK_RS            (inst_cream->Rs == 15)

#define UNIMPLEMENTED_INSTRUCTION(mnemonic) \
    LOG_ERROR(Core_ARM11, "unimplemented instruction: %s", mnemonic); \
    CITRA_IGNORE_EXIT(-1); \
    return nullptr;

ARM_INST_PTR INTERPRETER_TRANSLATE(adc)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(adc_inst));
    adc_inst *inst_cream = (adc_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(add)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(add_inst));
    add_inst *inst_cream = (add_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(and)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(and_inst));
    and_inst *inst_cream = (and_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    if (inst_cream->Rd == 15) 
        inst_base->br = INDIRECT_BRANCH;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(bbl)(unsigned int inst, int index)
{
    #define POSBRANCH ((inst & 0x7fffff) << 2)
    #define NEGBRANCH ((0xff000000 |(inst & 0xffffff)) << 2)

    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(bbl_inst));
    bbl_inst *inst_cream = (bbl_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = DIRECT_BRANCH;

    if (BIT(inst, 24))
        inst_base->br = CALL;
    if (BITS(inst, 28, 31) <= 0xe)
        inst_base->br |= COND;

    inst_cream->L      = BIT(inst, 24);
    inst_cream->signed_immed_24 = BIT(inst, 23) ? NEGBRANCH : POSBRANCH;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(bic)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(bic_inst));
    bic_inst *inst_cream = (bic_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);

    if (inst_cream->Rd == 15) 
        inst_base->br = INDIRECT_BRANCH;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(bkpt)(unsigned int inst, int index) { UNIMPLEMENTED_INSTRUCTION("BKPT"); }
ARM_INST_PTR INTERPRETER_TRANSLATE(blx)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(blx_inst));
    blx_inst *inst_cream = (blx_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = INDIRECT_BRANCH;

    inst_cream->inst = inst;
    if (BITS(inst, 20, 27) == 0x12 && BITS(inst, 4, 7) == 0x3) {
        inst_cream->val.Rm = BITS(inst, 0, 3);
    } else {
        inst_cream->val.signed_immed_24 = BITS(inst, 0, 23);
    }

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(bx)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(bx_inst));
    bx_inst *inst_cream = (bx_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = INDIRECT_BRANCH;

    inst_cream->Rm  = BITS(inst, 0, 3);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(bxj)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(bx)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(cdp)(unsigned int inst, int index){
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(cdp_inst));
    cdp_inst *inst_cream = (cdp_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->CRm      = BITS(inst,  0,  3);
    inst_cream->CRd      = BITS(inst, 12, 15);
    inst_cream->CRn      = BITS(inst, 16, 19);
    inst_cream->cp_num   = BITS(inst, 8, 11);
    inst_cream->opcode_2 = BITS(inst, 5, 7);
    inst_cream->opcode_1 = BITS(inst, 20, 23);
    inst_cream->inst = inst;

    LOG_TRACE(Core_ARM11, "inst %x index %x", inst, index);
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(clrex)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(clrex_inst));
    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(clz)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(clz_inst));
    clz_inst *inst_cream = (clz_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm = BITS(inst,  0,  3);
    inst_cream->Rd = BITS(inst, 12, 15);
    if (CHECK_RM) 
        inst_base->load_r15 = 1;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(cmn)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(cmn_inst));
    cmn_inst *inst_cream = (cmn_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->Rn = BITS(inst, 16, 19);

    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(cmp)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(cmp_inst));
    cmp_inst *inst_cream = (cmp_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->Rn = BITS(inst, 16, 19);
    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(cps)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(cps_inst));
    cps_inst *inst_cream = (cps_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->imod0 = BIT(inst, 18);
    inst_cream->imod1 = BIT(inst, 19);
    inst_cream->mmod  = BIT(inst, 17);
    inst_cream->A     = BIT(inst, 8);
    inst_cream->I     = BIT(inst, 7);
    inst_cream->F     = BIT(inst, 6);
    inst_cream->mode  = BITS(inst, 0, 4);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(cpy)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(mov_inst));
    mov_inst *inst_cream = (mov_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);

    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(eor)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(eor_inst));
    eor_inst *inst_cream = (eor_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldc)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldc_inst));
    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldm)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BIT(inst, 15)) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(sxth)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(sxtb_inst));
    sxtb_inst *inst_cream = (sxtb_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->Rm     = BITS(inst,  0,  3);
    inst_cream->rotate = BITS(inst, 10, 11);
    if (CHECK_RM) 
        inst_base->load_r15 = 1;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldr)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(ldrcond)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(uxth)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(uxth_inst));
    uxth_inst *inst_cream = (uxth_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->rotate = BITS(inst, 10, 11);
    inst_cream->Rm     = BITS(inst,  0,  3);
    if (CHECK_RM) 
        inst_base->load_r15 = 1;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uxtah)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(uxtah_inst));
    uxtah_inst *inst_cream = (uxtah_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rn     = BITS(inst, 16, 19);
    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->rotate = BITS(inst, 10, 11);
    inst_cream->Rm     = BITS(inst,  0,  3);
    if (CHECK_RM || CHECK_RN) 
        inst_base->load_r15 = 1;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrb)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrbt)(unsigned int inst, int index)
{
    arm_inst* inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst* inst_cream = (ldst_inst*)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    if (BITS(inst, 25, 27) == 2) {
        inst_cream->get_addr = LnSWoUB(ImmediatePostIndexed);
    } else if (BITS(inst, 25, 27) == 3) {
        inst_cream->get_addr = LnSWoUB(ScaledRegisterPostIndexed);
    } else {
        DEBUG_MSG;
    }

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrd)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrex)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst *inst_cream = (generic_arm_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = (BITS(inst, 12, 15) == 15) ? INDIRECT_BRANCH : NON_BRANCH; // Branch if dest is R15

    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrexb)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(ldrex)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrexh)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(ldrex)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrexd)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(ldrex)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrh)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrsb)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrsh)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ldrt)(unsigned int inst, int index)
{
    arm_inst* inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst* inst_cream = (ldst_inst*)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    if (BITS(inst, 25, 27) == 2) {
        inst_cream->get_addr = LnSWoUB(ImmediatePostIndexed);
    } else if (BITS(inst, 25, 27) == 3) {
        inst_cream->get_addr = LnSWoUB(ScaledRegisterPostIndexed);
    } else {
        // Reaching this would indicate the thumb version
        // of this instruction, however the 3DS CPU doesn't
        // support this variant (the 3DS CPU is only ARMv6K,
        // while this variant is added in ARMv6T2).
        // So it's sufficient for citra to not implement this.
        DEBUG_MSG;
    }

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(mcr)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(mcr_inst));
    mcr_inst *inst_cream = (mcr_inst *)inst_base->component;
    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->crn      = BITS(inst, 16, 19);
    inst_cream->crm      = BITS(inst,  0,  3);
    inst_cream->opcode_1 = BITS(inst, 21, 23);
    inst_cream->opcode_2 = BITS(inst,  5,  7);
    inst_cream->Rd       = BITS(inst, 12, 15);
    inst_cream->cp_num   = BITS(inst,  8, 11);
    inst_cream->inst     = inst;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(mcrr)(unsigned int inst, int index) { UNIMPLEMENTED_INSTRUCTION("MCRR"); }
ARM_INST_PTR INTERPRETER_TRANSLATE(mla)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(mla_inst));
    mla_inst *inst_cream = (mla_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 12, 15);
    inst_cream->Rd = BITS(inst, 16, 19);
    inst_cream->Rs = BITS(inst,  8, 11);
    inst_cream->Rm = BITS(inst,  0,  3);

    if (CHECK_RM || CHECK_RN || CHECK_RS) 
        inst_base->load_r15 = 1;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(mov)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(mov_inst));
    mov_inst *inst_cream = (mov_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);

    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(mrc)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(mrc_inst));
    mrc_inst *inst_cream = (mrc_inst *)inst_base->component;
    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->crn      = BITS(inst, 16, 19);
    inst_cream->crm      = BITS(inst,  0,  3);
    inst_cream->opcode_1 = BITS(inst, 21, 23);
    inst_cream->opcode_2 = BITS(inst,  5,  7);
    inst_cream->Rd       = BITS(inst, 12, 15);
    inst_cream->cp_num   = BITS(inst,  8, 11);
    inst_cream->inst     = inst;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(mrrc)(unsigned int inst, int index) { UNIMPLEMENTED_INSTRUCTION("MRRC"); }
ARM_INST_PTR INTERPRETER_TRANSLATE(mrs)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(mrs_inst));
    mrs_inst *inst_cream = (mrs_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->R  = BIT(inst, 22);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(msr)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(msr_inst));
    msr_inst *inst_cream = (msr_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->field_mask = BITS(inst, 16, 19);
    inst_cream->R          = BIT(inst, 22);
    inst_cream->inst       = inst;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(mul)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(mul_inst));
    mul_inst *inst_cream = (mul_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rm = BITS(inst, 0, 3);
    inst_cream->Rs = BITS(inst, 8, 11);
    inst_cream->Rd = BITS(inst, 16, 19);

    if (CHECK_RM || CHECK_RS) 
        inst_base->load_r15 = 1;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(mvn)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(mvn_inst));
    mvn_inst *inst_cream = (mvn_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);

    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;

}
ARM_INST_PTR INTERPRETER_TRANSLATE(orr)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(orr_inst));
    orr_inst *inst_cream = (orr_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);

    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(pkhbt)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(pkh_inst));
    pkh_inst *inst_cream = (pkh_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx = index;
    inst_base->br = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rm = BITS(inst, 0, 3);
    inst_cream->imm = BITS(inst, 7, 11);

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(pkhtb)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(pkhbt)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(pld)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(pld_inst));

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(qadd)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->op1 = BITS(inst, 21, 22);
    inst_cream->Rm  = BITS(inst, 0, 3);
    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(qdadd)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(qadd)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(qdsub)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(qadd)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(qsub)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(qadd)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(qadd8)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm  = BITS(inst, 0, 3);
    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);
    inst_cream->op1 = BITS(inst, 20, 21);
    inst_cream->op2 = BITS(inst, 5, 7);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(qadd16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(qadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(qaddsubx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(qadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(qsub8)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(qadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(qsub16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(qadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(qsubaddx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(qadd8)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(rev)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(rev_inst));
    rev_inst* const inst_cream = (rev_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm  = BITS(inst,  0,  3);
    inst_cream->Rd  = BITS(inst, 12, 15);
    inst_cream->op1 = BITS(inst, 20, 22);
    inst_cream->op2 = BITS(inst, 5, 7);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(rev16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(rev)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(revsh)(unsigned int inst, int index)
{
     return INTERPRETER_TRANSLATE(rev)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(rfe)(unsigned int inst, int index)   { UNIMPLEMENTED_INSTRUCTION("RFE"); }
ARM_INST_PTR INTERPRETER_TRANSLATE(rsb)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(rsb_inst));
    rsb_inst *inst_cream = (rsb_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    if (CHECK_RN) 
        inst_base->load_r15 = 1;

    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(rsc)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(rsc_inst));
    rsc_inst *inst_cream = (rsc_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    if (CHECK_RN)
        inst_base->load_r15 = 1;

    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(sadd8)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm  = BITS(inst, 0, 3);
    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);
    inst_cream->op1 = BITS(inst, 20, 21);
    inst_cream->op2 = BITS(inst, 5, 7);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(sadd16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(sadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(saddsubx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(sadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ssub8)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(sadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ssub16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(sadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ssubaddx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(sadd8)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(sbc)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(sbc_inst));
    sbc_inst *inst_cream = (sbc_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    if (CHECK_RN)
        inst_base->load_r15 = 1;

    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(sel)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm  = BITS(inst, 0, 3);
    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);
    inst_cream->op1 = BITS(inst, 20, 22);
    inst_cream->op2 = BITS(inst, 5, 7);

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(setend)(unsigned int inst, int index)    { UNIMPLEMENTED_INSTRUCTION("SETEND"); }

ARM_INST_PTR INTERPRETER_TRANSLATE(shadd8)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->op1 = BITS(inst, 20, 21);
    inst_cream->op2 = BITS(inst, 5, 7);
    inst_cream->Rm  = BITS(inst, 0, 3);
    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(shadd16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(shadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(shaddsubx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(shadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(shsub8)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(shadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(shsub16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(shadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(shsubaddx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(shadd8)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(smla)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(smla_inst));
    smla_inst *inst_cream = (smla_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->x  = BIT(inst, 5);
    inst_cream->y  = BIT(inst, 6);
    inst_cream->Rm = BITS(inst, 0, 3);
    inst_cream->Rs = BITS(inst, 8, 11);
    inst_cream->Rd = BITS(inst, 16, 19);
    inst_cream->Rn = BITS(inst, 12, 15);

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(smlad)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(smlad_inst));
    smlad_inst* const inst_cream = (smlad_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->m   = BIT(inst, 5);
    inst_cream->Rn  = BITS(inst, 0, 3);
    inst_cream->Rm  = BITS(inst, 8, 11);
    inst_cream->Rd  = BITS(inst, 16, 19);
    inst_cream->Ra  = BITS(inst, 12, 15);
    inst_cream->op1 = BITS(inst, 20, 22);
    inst_cream->op2 = BITS(inst, 5, 7);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(smuad)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(smlad)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(smusd)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(smlad)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(smlsd)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(smlad)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(smlal)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(umlal_inst));
    umlal_inst *inst_cream = (umlal_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->S    = BIT(inst, 20);
    inst_cream->Rm   = BITS(inst, 0, 3);
    inst_cream->Rs   = BITS(inst, 8, 11);
    inst_cream->RdHi = BITS(inst, 16, 19);
    inst_cream->RdLo = BITS(inst, 12, 15);

    if (CHECK_RM || CHECK_RS) 
        inst_base->load_r15 = 1;
    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(smlalxy)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(smlalxy_inst));
    smlalxy_inst* const inst_cream = (smlalxy_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->x    = BIT(inst, 5);
    inst_cream->y    = BIT(inst, 6);
    inst_cream->RdLo = BITS(inst, 12, 15);
    inst_cream->RdHi = BITS(inst, 16, 19);
    inst_cream->Rn   = BITS(inst, 0, 4);
    inst_cream->Rm   = BITS(inst, 8, 11);

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(smlaw)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(smlad_inst));
    smlad_inst* const inst_cream = (smlad_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Ra = BITS(inst, 12, 15);
    inst_cream->Rm = BITS(inst, 8, 11);
    inst_cream->Rn = BITS(inst, 0, 3);
    inst_cream->Rd = BITS(inst, 16, 19);
    inst_cream->m  = BIT(inst, 6);

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(smlald)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(smlald_inst));
    smlald_inst* const inst_cream = (smlald_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm   = BITS(inst, 8, 11);
    inst_cream->Rn   = BITS(inst, 0, 3);
    inst_cream->RdLo = BITS(inst, 12, 15);
    inst_cream->RdHi = BITS(inst, 16, 19);
    inst_cream->swap = BIT(inst, 5);
    inst_cream->op1  = BITS(inst, 20, 22);
    inst_cream->op2  = BITS(inst, 5, 7);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(smlsld)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(smlald)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(smmla)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(smlad_inst));
    smlad_inst* const inst_cream = (smlad_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->m   = BIT(inst, 5);
    inst_cream->Ra  = BITS(inst, 12, 15);
    inst_cream->Rm  = BITS(inst, 8, 11);
    inst_cream->Rn  = BITS(inst, 0, 3);
    inst_cream->Rd  = BITS(inst, 16, 19);
    inst_cream->op1 = BITS(inst, 20, 22);
    inst_cream->op2 = BITS(inst, 5, 7);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(smmls)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(smmla)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(smmul)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(smmla)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(smul)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(smul_inst));
    smul_inst *inst_cream = (smul_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rd = BITS(inst, 16, 19);
    inst_cream->Rs = BITS(inst,  8, 11);
    inst_cream->Rm = BITS(inst,  0,  3);

    inst_cream->x  = BIT(inst, 5);
    inst_cream->y  = BIT(inst, 6);

    if (CHECK_RM || CHECK_RS) 
        inst_base->load_r15 = 1;
    return inst_base;

}
ARM_INST_PTR INTERPRETER_TRANSLATE(smull)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(umull_inst));
    umull_inst *inst_cream = (umull_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->S    = BIT(inst, 20);
    inst_cream->Rm   = BITS(inst, 0, 3);
    inst_cream->Rs   = BITS(inst, 8, 11);
    inst_cream->RdHi = BITS(inst, 16, 19);
    inst_cream->RdLo = BITS(inst, 12, 15);

    if (CHECK_RM || CHECK_RS) 
        inst_base->load_r15 = 1;
    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(smulw)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(smlad_inst));
    smlad_inst *inst_cream = (smlad_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->m  = BIT(inst, 6);
    inst_cream->Rm = BITS(inst, 8, 11);
    inst_cream->Rn = BITS(inst, 0, 3);
    inst_cream->Rd = BITS(inst, 16, 19);

    if (CHECK_RM || CHECK_RN) 
        inst_base->load_r15 = 1;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(srs)(unsigned int inst, int index)      { UNIMPLEMENTED_INSTRUCTION("SRS"); }
ARM_INST_PTR INTERPRETER_TRANSLATE(ssat)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(ssat_inst));
    ssat_inst* const inst_cream = (ssat_inst*)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx = index;
    inst_base->br = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rn = BITS(inst, 0, 3);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->imm5 = BITS(inst, 7, 11);
    inst_cream->sat_imm = BITS(inst, 16, 20);
    inst_cream->shift_type = BIT(inst, 6);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(ssat16)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(ssat_inst));
    ssat_inst* const inst_cream = (ssat_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rn      = BITS(inst, 0, 3);
    inst_cream->Rd      = BITS(inst, 12, 15);
    inst_cream->sat_imm = BITS(inst, 16, 19);

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(stc)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(stc_inst));
    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(stm)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(sxtb)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(sxtb_inst));
    sxtb_inst *inst_cream = (sxtb_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->Rm     = BITS(inst,  0,  3);
    inst_cream->rotate = BITS(inst, 10, 11);

    if (CHECK_RM) 
        inst_base->load_r15 = 1;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(str)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uxtb)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(uxth_inst));
    uxth_inst *inst_cream = (uxth_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->rotate = BITS(inst, 10, 11);
    inst_cream->Rm     = BITS(inst,  0,  3);

    if (CHECK_RM) 
        inst_base->load_r15 = 1;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uxtab)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(uxtab_inst));
    uxtab_inst *inst_cream = (uxtab_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->rotate = BITS(inst, 10, 11);
    inst_cream->Rm     = BITS(inst,  0,  3);
    inst_cream->Rn     = BITS(inst, 16, 19);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(strb)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(strbt)(unsigned int inst, int index)
{
    arm_inst* inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst* inst_cream = (ldst_inst*)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;

    if (BITS(inst, 25, 27) == 2) {
        inst_cream->get_addr = LnSWoUB(ImmediatePostIndexed);
    } else if (BITS(inst, 25, 27) == 3) {
        inst_cream->get_addr = LnSWoUB(ScaledRegisterPostIndexed);
    } else {
        DEBUG_MSG;
    }

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(strd)(unsigned int inst, int index){
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(strex)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst *inst_cream = (generic_arm_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);
    inst_cream->Rm  = BITS(inst, 0,   3);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(strexb)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(strex)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(strexh)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(strex)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(strexd)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(strex)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(strh)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst *inst_cream = (ldst_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    inst_cream->get_addr = get_calc_addr_op(inst);

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(strt)(unsigned int inst, int index)
{
    arm_inst* inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(ldst_inst));
    ldst_inst* inst_cream = (ldst_inst*)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->inst = inst;
    if (BITS(inst, 25, 27) == 2) {
        inst_cream->get_addr = LnSWoUB(ImmediatePostIndexed);
    } else if (BITS(inst, 25, 27) == 3) {
        inst_cream->get_addr = LnSWoUB(ScaledRegisterPostIndexed);
    } else {
        // Reaching this would indicate the thumb version
        // of this instruction, however the 3DS CPU doesn't
        // support this variant (the 3DS CPU is only ARMv6K,
        // while this variant is added in ARMv6T2).
        // So it's sufficient for citra to not implement this.
        DEBUG_MSG;
    }

    if (BITS(inst, 12, 15) == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(sub)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(sub_inst));
    sub_inst *inst_cream = (sub_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I  = BIT(inst, 25);
    inst_cream->S  = BIT(inst, 20);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    if (CHECK_RN) 
        inst_base->load_r15 = 1;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(swi)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(swi_inst));
    swi_inst *inst_cream = (swi_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->num = BITS(inst, 0, 23);
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(swp)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(swp_inst));
    swp_inst *inst_cream = (swp_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);
    inst_cream->Rm  = BITS(inst,  0,  3);

    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(swpb)(unsigned int inst, int index){
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(swp_inst));
    swp_inst *inst_cream = (swp_inst *)inst_base->component;

    inst_base->cond = BITS(inst, 28, 31);
    inst_base->idx  = index;
    inst_base->br   = NON_BRANCH;

    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);
    inst_cream->Rm  = BITS(inst,  0,  3);

    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(sxtab)(unsigned int inst, int index){
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(sxtab_inst));
    sxtab_inst *inst_cream = (sxtab_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->rotate = BITS(inst, 10, 11);
    inst_cream->Rm     = BITS(inst,  0,  3);
    inst_cream->Rn     = BITS(inst, 16, 19);

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(sxtab16)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(sxtab_inst));
    sxtab_inst* const inst_cream = (sxtab_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm = BITS(inst, 0, 3);
    inst_cream->Rn = BITS(inst, 16, 19);
    inst_cream->Rd = BITS(inst, 12, 15);
    inst_cream->rotate = BITS(inst, 10, 11);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(sxtb16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(sxtab16)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(sxtah)(unsigned int inst, int index){
    LOG_WARNING(Core_ARM11, "SXTAH untested");
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(sxtah_inst));
    sxtah_inst *inst_cream = (sxtah_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->rotate = BITS(inst, 10, 11);
    inst_cream->Rm     = BITS(inst,  0,  3);
    inst_cream->Rn     = BITS(inst, 16, 19);

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(teq)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(teq_inst));
    teq_inst *inst_cream = (teq_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I               = BIT(inst, 25);
    inst_cream->Rn              = BITS(inst, 16, 19);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func      = get_shtop(inst);

    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(tst)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(tst_inst));
    tst_inst *inst_cream = (tst_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->I      = BIT(inst, 25);
    inst_cream->S      = BIT(inst, 20);
    inst_cream->Rn     = BITS(inst, 16, 19);
    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->shifter_operand = BITS(inst, 0, 11);
    inst_cream->shtop_func = get_shtop(inst);
    if (inst_cream->Rd == 15) {
        inst_base->br = INDIRECT_BRANCH;
    }

    if (CHECK_RN) 
        inst_base->load_r15 = 1;
    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(uadd8)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->op1 = BITS(inst, 20, 21);
    inst_cream->op2 = BITS(inst, 5, 7);
    inst_cream->Rm  = BITS(inst, 0, 3);
    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uadd16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uaddsubx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(usub8)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(usub16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(usubaddx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uadd8)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(uhadd8)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->op1 = BITS(inst, 20, 21);
    inst_cream->op2 = BITS(inst, 5, 7);
    inst_cream->Rm  = BITS(inst, 0, 3);
    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uhadd16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uhadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uhaddsubx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uhadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uhsub8)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uhadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uhsub16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uhadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uhsubaddx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uhadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(umaal)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(umaal_inst));
    umaal_inst* const inst_cream = (umaal_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm   = BITS(inst, 8, 11);
    inst_cream->Rn   = BITS(inst, 0, 3);
    inst_cream->RdLo = BITS(inst, 12, 15);
    inst_cream->RdHi = BITS(inst, 16, 19);

    if (CHECK_RM || CHECK_RN)
        inst_base->load_r15 = 1;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(umlal)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(umlal_inst));
    umlal_inst *inst_cream = (umlal_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->S    = BIT(inst, 20);
    inst_cream->Rm   = BITS(inst, 0, 3);
    inst_cream->Rs   = BITS(inst, 8, 11);
    inst_cream->RdHi = BITS(inst, 16, 19);
    inst_cream->RdLo = BITS(inst, 12, 15);

    if (CHECK_RM || CHECK_RS) 
        inst_base->load_r15 = 1;

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(umull)(unsigned int inst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(umull_inst));
    umull_inst *inst_cream = (umull_inst *)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->S    = BIT(inst, 20);
    inst_cream->Rm   = BITS(inst, 0, 3);
    inst_cream->Rs   = BITS(inst, 8, 11);
    inst_cream->RdHi = BITS(inst, 16, 19);
    inst_cream->RdLo = BITS(inst, 12, 15);

    if (CHECK_RM || CHECK_RS) 
        inst_base->load_r15 = 1;
    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(b_2_thumb)(unsigned int tinst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(b_2_thumb));
    b_2_thumb *inst_cream = (b_2_thumb *)inst_base->component;

    inst_cream->imm = ((tinst & 0x3FF) << 1) | ((tinst & (1 << 10)) ? 0xFFFFF800 : 0);

    inst_base->idx = index;
    inst_base->br  = DIRECT_BRANCH;

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(b_cond_thumb)(unsigned int tinst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(b_cond_thumb));
    b_cond_thumb *inst_cream = (b_cond_thumb *)inst_base->component;

    inst_cream->imm  = (((tinst & 0x7F) << 1) | ((tinst & (1 << 7)) ?    0xFFFFFF00 : 0));
    inst_cream->cond = ((tinst >> 8) & 0xf);
    inst_base->idx   = index;
    inst_base->br    = DIRECT_BRANCH;

    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(bl_1_thumb)(unsigned int tinst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(bl_1_thumb));
    bl_1_thumb *inst_cream = (bl_1_thumb *)inst_base->component;

    inst_cream->imm = (((tinst & 0x07FF) << 12) | ((tinst & (1 << 10)) ? 0xFF800000 : 0));

    inst_base->idx = index;
    inst_base->br  = NON_BRANCH;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(bl_2_thumb)(unsigned int tinst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(bl_2_thumb));
    bl_2_thumb *inst_cream = (bl_2_thumb *)inst_base->component;

    inst_cream->imm = (tinst & 0x07FF) << 1;

    inst_base->idx = index;
    inst_base->br  = DIRECT_BRANCH;
    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(blx_1_thumb)(unsigned int tinst, int index)
{
    arm_inst *inst_base = (arm_inst *)AllocBuffer(sizeof(arm_inst) + sizeof(blx_1_thumb));
    blx_1_thumb *inst_cream = (blx_1_thumb *)inst_base->component;

    inst_cream->imm   = (tinst & 0x07FF) << 1;
    inst_cream->instr = tinst;

    inst_base->idx    = index;
    inst_base->br     = DIRECT_BRANCH;
    return inst_base;
}

ARM_INST_PTR INTERPRETER_TRANSLATE(uqadd8)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm  = BITS(inst, 0, 3);
    inst_cream->Rn  = BITS(inst, 16, 19);
    inst_cream->Rd  = BITS(inst, 12, 15);
    inst_cream->op1 = BITS(inst, 20, 21);
    inst_cream->op2 = BITS(inst, 5, 7);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uqadd16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uqadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uqaddsubx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uqadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uqsub8)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uqadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uqsub16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uqadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uqsubaddx)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uqadd8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(usada8)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(generic_arm_inst));
    generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->op1 = BITS(inst, 20, 24);
    inst_cream->op2 = BITS(inst, 5, 7);
    inst_cream->Rm  = BITS(inst, 8, 11);
    inst_cream->Rn  = BITS(inst, 0, 3);
    inst_cream->Ra  = BITS(inst, 12, 15);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(usad8)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(usada8)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(usat)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(ssat)(inst, index);
}
ARM_INST_PTR INTERPRETER_TRANSLATE(usat16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(ssat16)(inst, index);
}

ARM_INST_PTR INTERPRETER_TRANSLATE(uxtab16)(unsigned int inst, int index)
{
    arm_inst* const inst_base = (arm_inst*)AllocBuffer(sizeof(arm_inst) + sizeof(uxtab_inst));
    uxtab_inst* const inst_cream = (uxtab_inst*)inst_base->component;

    inst_base->cond     = BITS(inst, 28, 31);
    inst_base->idx      = index;
    inst_base->br       = NON_BRANCH;
    inst_base->load_r15 = 0;

    inst_cream->Rm     = BITS(inst, 0, 3);
    inst_cream->Rn     = BITS(inst, 16, 19);
    inst_cream->Rd     = BITS(inst, 12, 15);
    inst_cream->rotate = BITS(inst, 10, 11);

    return inst_base;
}
ARM_INST_PTR INTERPRETER_TRANSLATE(uxtb16)(unsigned int inst, int index)
{
    return INTERPRETER_TRANSLATE(uxtab16)(inst, index);
}

// Floating point VFPv3 structures and instructions

#define VFP_INTERPRETER_STRUCT
#include "core/arm/skyeye_common/vfp/vfpinstr.cpp"
#undef VFP_INTERPRETER_STRUCT
 
#define VFP_INTERPRETER_TRANS
#include "core/arm/skyeye_common/vfp/vfpinstr.cpp"
#undef VFP_INTERPRETER_TRANS

typedef ARM_INST_PTR (*transop_fp_t)(unsigned int, int);

const transop_fp_t arm_instruction_trans[] = {
    INTERPRETER_TRANSLATE(vmla),
    INTERPRETER_TRANSLATE(vmls),
    INTERPRETER_TRANSLATE(vnmla),
    INTERPRETER_TRANSLATE(vnmla),
    INTERPRETER_TRANSLATE(vnmls),
    INTERPRETER_TRANSLATE(vnmul),
    INTERPRETER_TRANSLATE(vmul),
    INTERPRETER_TRANSLATE(vadd),
    INTERPRETER_TRANSLATE(vsub),
    INTERPRETER_TRANSLATE(vdiv),
    INTERPRETER_TRANSLATE(vmovi),
    INTERPRETER_TRANSLATE(vmovr),
    INTERPRETER_TRANSLATE(vabs),
    INTERPRETER_TRANSLATE(vneg),
    INTERPRETER_TRANSLATE(vsqrt),
    INTERPRETER_TRANSLATE(vcmp),
    INTERPRETER_TRANSLATE(vcmp2),
    INTERPRETER_TRANSLATE(vcvtbds),
    INTERPRETER_TRANSLATE(vcvtbff),
    INTERPRETER_TRANSLATE(vcvtbfi),
    INTERPRETER_TRANSLATE(vmovbrs),
    INTERPRETER_TRANSLATE(vmsr),
    INTERPRETER_TRANSLATE(vmovbrc),
    INTERPRETER_TRANSLATE(vmrs),
    INTERPRETER_TRANSLATE(vmovbcr),
    INTERPRETER_TRANSLATE(vmovbrrss),
    INTERPRETER_TRANSLATE(vmovbrrd),
    INTERPRETER_TRANSLATE(vstr),
    INTERPRETER_TRANSLATE(vpush),
    INTERPRETER_TRANSLATE(vstm),
    INTERPRETER_TRANSLATE(vpop),
    INTERPRETER_TRANSLATE(vldr),
    INTERPRETER_TRANSLATE(vldm),

    INTERPRETER_TRANSLATE(srs),
    INTERPRETER_TRANSLATE(rfe),
    INTERPRETER_TRANSLATE(bkpt),
    INTERPRETER_TRANSLATE(blx),
    INTERPRETER_TRANSLATE(cps),
    INTERPRETER_TRANSLATE(pld),
    INTERPRETER_TRANSLATE(setend),
    INTERPRETER_TRANSLATE(clrex),
    INTERPRETER_TRANSLATE(rev16),
    INTERPRETER_TRANSLATE(usad8),
    INTERPRETER_TRANSLATE(sxtb),
    INTERPRETER_TRANSLATE(uxtb),
    INTERPRETER_TRANSLATE(sxth),
    INTERPRETER_TRANSLATE(sxtb16),
    INTERPRETER_TRANSLATE(uxth),
    INTERPRETER_TRANSLATE(uxtb16),
    INTERPRETER_TRANSLATE(cpy),
    INTERPRETER_TRANSLATE(uxtab),
    INTERPRETER_TRANSLATE(ssub8),
    INTERPRETER_TRANSLATE(shsub8),
    INTERPRETER_TRANSLATE(ssubaddx),
    INTERPRETER_TRANSLATE(strex),
    INTERPRETER_TRANSLATE(strexb),
    INTERPRETER_TRANSLATE(swp),
    INTERPRETER_TRANSLATE(swpb),
    INTERPRETER_TRANSLATE(ssub16),
    INTERPRETER_TRANSLATE(ssat16),
    INTERPRETER_TRANSLATE(shsubaddx),
    INTERPRETER_TRANSLATE(qsubaddx),
    INTERPRETER_TRANSLATE(shaddsubx),
    INTERPRETER_TRANSLATE(shadd8),
    INTERPRETER_TRANSLATE(shadd16),
    INTERPRETER_TRANSLATE(sel),
    INTERPRETER_TRANSLATE(saddsubx),
    INTERPRETER_TRANSLATE(sadd8),
    INTERPRETER_TRANSLATE(sadd16),
    INTERPRETER_TRANSLATE(shsub16),
    INTERPRETER_TRANSLATE(umaal),
    INTERPRETER_TRANSLATE(uxtab16),
    INTERPRETER_TRANSLATE(usubaddx),
    INTERPRETER_TRANSLATE(usub8),
    INTERPRETER_TRANSLATE(usub16),
    INTERPRETER_TRANSLATE(usat16),
    INTERPRETER_TRANSLATE(usada8),
    INTERPRETER_TRANSLATE(uqsubaddx),
    INTERPRETER_TRANSLATE(uqsub8),
    INTERPRETER_TRANSLATE(uqsub16),
    INTERPRETER_TRANSLATE(uqaddsubx),
    INTERPRETER_TRANSLATE(uqadd8),
    INTERPRETER_TRANSLATE(uqadd16),
    INTERPRETER_TRANSLATE(sxtab),
    INTERPRETER_TRANSLATE(uhsubaddx),
    INTERPRETER_TRANSLATE(uhsub8),
    INTERPRETER_TRANSLATE(uhsub16),
    INTERPRETER_TRANSLATE(uhaddsubx),
    INTERPRETER_TRANSLATE(uhadd8),
    INTERPRETER_TRANSLATE(uhadd16),
    INTERPRETER_TRANSLATE(uaddsubx),
    INTERPRETER_TRANSLATE(uadd8),
    INTERPRETER_TRANSLATE(uadd16),
    INTERPRETER_TRANSLATE(sxtah),
    INTERPRETER_TRANSLATE(sxtab16),
    INTERPRETER_TRANSLATE(qadd8),
    INTERPRETER_TRANSLATE(bxj),
    INTERPRETER_TRANSLATE(clz),
    INTERPRETER_TRANSLATE(uxtah),
    INTERPRETER_TRANSLATE(bx),
    INTERPRETER_TRANSLATE(rev),
    INTERPRETER_TRANSLATE(blx),
    INTERPRETER_TRANSLATE(revsh),
    INTERPRETER_TRANSLATE(qadd),
    INTERPRETER_TRANSLATE(qadd16),
    INTERPRETER_TRANSLATE(qaddsubx),
    INTERPRETER_TRANSLATE(ldrex),
    INTERPRETER_TRANSLATE(qdadd),
    INTERPRETER_TRANSLATE(qdsub),
    INTERPRETER_TRANSLATE(qsub),
    INTERPRETER_TRANSLATE(ldrexb),
    INTERPRETER_TRANSLATE(qsub8),
    INTERPRETER_TRANSLATE(qsub16),
    INTERPRETER_TRANSLATE(smuad),
    INTERPRETER_TRANSLATE(smmul),
    INTERPRETER_TRANSLATE(smusd),
    INTERPRETER_TRANSLATE(smlsd),
    INTERPRETER_TRANSLATE(smlsld),
    INTERPRETER_TRANSLATE(smmla),
    INTERPRETER_TRANSLATE(smmls),
    INTERPRETER_TRANSLATE(smlald),
    INTERPRETER_TRANSLATE(smlad),
    INTERPRETER_TRANSLATE(smlaw),
    INTERPRETER_TRANSLATE(smulw),
    INTERPRETER_TRANSLATE(pkhtb),
    INTERPRETER_TRANSLATE(pkhbt),
    INTERPRETER_TRANSLATE(smul),
    INTERPRETER_TRANSLATE(smlalxy),
    INTERPRETER_TRANSLATE(smla),
    INTERPRETER_TRANSLATE(mcrr),
    INTERPRETER_TRANSLATE(mrrc),
    INTERPRETER_TRANSLATE(cmp),
    INTERPRETER_TRANSLATE(tst),
    INTERPRETER_TRANSLATE(teq),
    INTERPRETER_TRANSLATE(cmn),
    INTERPRETER_TRANSLATE(smull),
    INTERPRETER_TRANSLATE(umull),
    INTERPRETER_TRANSLATE(umlal),
    INTERPRETER_TRANSLATE(smlal),
    INTERPRETER_TRANSLATE(mul),
    INTERPRETER_TRANSLATE(mla),
    INTERPRETER_TRANSLATE(ssat),
    INTERPRETER_TRANSLATE(usat),
    INTERPRETER_TRANSLATE(mrs),
    INTERPRETER_TRANSLATE(msr),
    INTERPRETER_TRANSLATE(and),
    INTERPRETER_TRANSLATE(bic),
    INTERPRETER_TRANSLATE(ldm),
    INTERPRETER_TRANSLATE(eor),
    INTERPRETER_TRANSLATE(add),
    INTERPRETER_TRANSLATE(rsb),
    INTERPRETER_TRANSLATE(rsc),
    INTERPRETER_TRANSLATE(sbc),
    INTERPRETER_TRANSLATE(adc),
    INTERPRETER_TRANSLATE(sub),
    INTERPRETER_TRANSLATE(orr),
    INTERPRETER_TRANSLATE(mvn),
    INTERPRETER_TRANSLATE(mov),
    INTERPRETER_TRANSLATE(stm),
    INTERPRETER_TRANSLATE(ldm),
    INTERPRETER_TRANSLATE(ldrsh),
    INTERPRETER_TRANSLATE(stm),
    INTERPRETER_TRANSLATE(ldm),
    INTERPRETER_TRANSLATE(ldrsb),
    INTERPRETER_TRANSLATE(strd),
    INTERPRETER_TRANSLATE(ldrh),
    INTERPRETER_TRANSLATE(strh),
    INTERPRETER_TRANSLATE(ldrd),
    INTERPRETER_TRANSLATE(strt),
    INTERPRETER_TRANSLATE(strbt),
    INTERPRETER_TRANSLATE(ldrbt),
    INTERPRETER_TRANSLATE(ldrt),
    INTERPRETER_TRANSLATE(mrc),
    INTERPRETER_TRANSLATE(mcr),
    INTERPRETER_TRANSLATE(msr),
    INTERPRETER_TRANSLATE(ldrb),
    INTERPRETER_TRANSLATE(strb),
    INTERPRETER_TRANSLATE(ldr),
    INTERPRETER_TRANSLATE(ldrcond),
    INTERPRETER_TRANSLATE(str),
    INTERPRETER_TRANSLATE(cdp),
    INTERPRETER_TRANSLATE(stc),
    INTERPRETER_TRANSLATE(ldc),
    INTERPRETER_TRANSLATE(swi),
    INTERPRETER_TRANSLATE(bbl),
    INTERPRETER_TRANSLATE(ldrexd),
    INTERPRETER_TRANSLATE(strexd),
    INTERPRETER_TRANSLATE(ldrexh),
    INTERPRETER_TRANSLATE(strexh),

    // All the thumb instructions should be placed the end of table
    INTERPRETER_TRANSLATE(b_2_thumb), 
    INTERPRETER_TRANSLATE(b_cond_thumb), 
    INTERPRETER_TRANSLATE(bl_1_thumb), 
    INTERPRETER_TRANSLATE(bl_2_thumb),
    INTERPRETER_TRANSLATE(blx_1_thumb)
};

typedef std::unordered_map<u32, int> bb_map;
bb_map CreamCache;

void insert_bb(unsigned int addr, int start) {
    CreamCache[addr] = start;
}

int find_bb(unsigned int addr, int &start) {
    int ret = -1;
    bb_map::const_iterator it = CreamCache.find(addr);
    if (it != CreamCache.end()) {
        start = static_cast<int>(it->second);
        ret = 0;
    } else {
        ret = -1;
    }
    return ret;
}

enum {
    FETCH_SUCCESS,
    FETCH_FAILURE
};

static tdstate decode_thumb_instr(arm_processor *cpu, uint32_t inst, addr_t addr, uint32_t *arm_inst, uint32_t* inst_size, ARM_INST_PTR* ptr_inst_base){
    // Check if in Thumb mode
    tdstate ret = thumb_translate (addr, inst, arm_inst, inst_size);
    if(ret == t_branch){
        // TODO: FIXME, endian should be judged
        u32 tinstr;
        if((addr & 0x3) != 0)
            tinstr = inst >> 16;
        else
            tinstr = inst & 0xFFFF;

        int inst_index;
        int table_length = sizeof(arm_instruction_trans) / sizeof(transop_fp_t);

        switch((tinstr & 0xF800) >> 11){
        case 26:
        case 27:
            if (((tinstr & 0x0F00) != 0x0E00) && ((tinstr & 0x0F00) != 0x0F00)){
                inst_index = table_length - 4;
                *ptr_inst_base = arm_instruction_trans[inst_index](tinstr, inst_index);
            } else {
                LOG_ERROR(Core_ARM11, "thumb decoder error");
            }
            break;
        case 28:
            // Branch 2, unconditional branch
            inst_index = table_length - 5;
            *ptr_inst_base = arm_instruction_trans[inst_index](tinstr, inst_index);
            break;

        case 8:
        case 29:
            // For BLX 1 thumb instruction
            inst_index = table_length - 1;
            *ptr_inst_base = arm_instruction_trans[inst_index](tinstr, inst_index);
            break;
        case 30:
            // For BL 1 thumb instruction
            inst_index = table_length - 3;
            *ptr_inst_base = arm_instruction_trans[inst_index](tinstr, inst_index);
            break;
        case 31:
            // For BL 2 thumb instruction
            inst_index = table_length - 2;
            *ptr_inst_base = arm_instruction_trans[inst_index](tinstr, inst_index);
            break;
        default:
            ret = t_undefined;
            break;
        }
    }
    return ret;
}

enum {
    KEEP_GOING,
    FETCH_EXCEPTION
};

typedef struct instruction_set_encoding_item ISEITEM;

extern const ISEITEM arm_instruction[];

vector<uint64_t> code_page_set;

void flush_bb(uint32_t addr) {
    bb_map::iterator it;
    uint32_t start;

    addr  &= 0xfffff000;
    for (it = CreamCache.begin(); it != CreamCache.end(); ) {
        start = static_cast<uint32_t>(it->first);
        start &= 0xfffff000;
        if (start == addr) {
            CreamCache.erase(it++);
        } else
            ++it;
    }
}

int InterpreterTranslate(arm_processor *cpu, int &bb_start, addr_t addr) {
    // Decode instruction, get index
    // Allocate memory and init InsCream
    // Go on next, until terminal instruction
    // Save start addr of basicblock in CreamCache
    ARM_INST_PTR inst_base = nullptr;
    unsigned int inst, inst_size = 4;
    int idx;
    int ret = NON_BRANCH;
    int thumb = 0;
    int size = 0; // instruction size of basic block
    bb_start = top;

    if (cpu->TFlag)
        thumb = THUMB;

    addr_t phys_addr = addr;
    addr_t pc_start = cpu->Reg[15];

    while(ret == NON_BRANCH) {
        inst = Memory::Read32(phys_addr & 0xFFFFFFFC);

        size++;
        // If we are in thumb instruction, we will translate one thumb to one corresponding arm instruction
        if (cpu->TFlag) {
            uint32_t arm_inst;
            tdstate state;
            state = decode_thumb_instr(cpu, inst, phys_addr, &arm_inst, &inst_size, &inst_base);

            // We have translated the branch instruction of thumb in thumb decoder
            if(state == t_branch){
                goto translated;
            }
            inst = arm_inst;
        }

        ret = decode_arm_instr(inst, &idx);
        if (ret == DECODE_FAILURE) {
            std::string disasm = ARM_Disasm::Disassemble(phys_addr, inst);
            LOG_ERROR(Core_ARM11, "Decode failure.\tPC : [0x%x]\tInstruction : %s [%x]", phys_addr, disasm.c_str(), inst);
            LOG_ERROR(Core_ARM11, "cpsr=0x%x, cpu->TFlag=%d, r15=0x%x", cpu->Cpsr, cpu->TFlag, cpu->Reg[15]);
            CITRA_IGNORE_EXIT(-1);
        }
        inst_base = arm_instruction_trans[idx](inst, idx);
translated:
        phys_addr += inst_size;

        if ((phys_addr & 0xfff) == 0) {
            inst_base->br = END_OF_PAGE;
        }
        ret = inst_base->br;
    };
    insert_bb(pc_start, bb_start);
    return KEEP_GOING;
}

#define LOG_IN_CLR    skyeye_printf_in_color

int clz(unsigned int x) {
    int n;
    if (x == 0) return (32);
    n = 1;
    if ((x >> 16) == 0) { n = n + 16; x = x << 16;}
    if ((x >> 24) == 0) { n = n +  8; x = x <<  8;}
    if ((x >> 28) == 0) { n = n +  4; x = x <<  4;}
    if ((x >> 30) == 0) { n = n +  2; x = x <<  2;}
    n = n - (x >> 31);
    return n;
}

unsigned arm_dyncom_SWI (ARMul_State * state, ARMword number);

static bool InAPrivilegedMode(arm_core_t *core) {
    return (core->Mode != USER32MODE);
}

unsigned InterpreterMainLoop(ARMul_State* state) {
    #undef RM
    #undef RS

    #define CRn             inst_cream->crn
    #define OPCODE_2        inst_cream->opcode_2
    #define CRm             inst_cream->crm
    #define CP15_REG(n)     cpu->CP15[CP15(n)]
    #define RD              cpu->Reg[inst_cream->Rd]
    #define RD2             cpu->Reg[inst_cream->Rd + 1]
    #define RN              cpu->Reg[inst_cream->Rn]
    #define RM              cpu->Reg[inst_cream->Rm]
    #define RS              cpu->Reg[inst_cream->Rs]
    #define RDHI            cpu->Reg[inst_cream->RdHi]
    #define RDLO            cpu->Reg[inst_cream->RdLo]
    #define LINK_RTN_ADDR   (cpu->Reg[14] = cpu->Reg[15] + 4)
    #define SET_PC          (cpu->Reg[15] = cpu->Reg[15] + 8 + inst_cream->signed_immed_24)
    #define SHIFTER_OPERAND inst_cream->shtop_func(cpu, inst_cream->shifter_operand)

    #define FETCH_INST if (inst_base->br != NON_BRANCH) goto DISPATCH; \
                       inst_base = (arm_inst *)&inst_buf[ptr]

    #define INC_PC(l) ptr += sizeof(arm_inst) + l

// GCC and Clang have a C++ extension to support a lookup table of labels. Otherwise, fallback to a
// clunky switch statement.
#if defined __GNUC__ || defined __clang__
#define GOTO_NEXT_INST \
    if (num_instrs >= cpu->NumInstrsToExecute) goto END; \
    num_instrs++; \
    goto *InstLabel[inst_base->idx]
#else
#define GOTO_NEXT_INST \
    if (num_instrs >= cpu->NumInstrsToExecute) goto END; \
    num_instrs++; \
    switch(inst_base->idx) { \
    case 0: goto VMLA_INST; \
    case 1: goto VMLS_INST; \
    case 2: goto VNMLA_INST; \
    case 3: goto VNMLA_INST; \
    case 4: goto VNMLS_INST; \
    case 5: goto VNMUL_INST; \
    case 6: goto VMUL_INST; \
    case 7: goto VADD_INST; \
    case 8: goto VSUB_INST; \
    case 9: goto VDIV_INST; \
    case 10: goto VMOVI_INST; \
    case 11: goto VMOVR_INST; \
    case 12: goto VABS_INST; \
    case 13: goto VNEG_INST; \
    case 14: goto VSQRT_INST; \
    case 15: goto VCMP_INST; \
    case 16: goto VCMP2_INST; \
    case 17: goto VCVTBDS_INST; \
    case 18: goto VCVTBFF_INST; \
    case 19: goto VCVTBFI_INST; \
    case 20: goto VMOVBRS_INST; \
    case 21: goto VMSR_INST; \
    case 22: goto VMOVBRC_INST; \
    case 23: goto VMRS_INST; \
    case 24: goto VMOVBCR_INST; \
    case 25: goto VMOVBRRSS_INST; \
    case 26: goto VMOVBRRD_INST; \
    case 27: goto VSTR_INST; \
    case 28: goto VPUSH_INST; \
    case 29: goto VSTM_INST; \
    case 30: goto VPOP_INST; \
    case 31: goto VLDR_INST; \
    case 32: goto VLDM_INST ; \
    case 33: goto SRS_INST; \
    case 34: goto RFE_INST; \
    case 35: goto BKPT_INST; \
    case 36: goto BLX_INST; \
    case 37: goto CPS_INST; \
    case 38: goto PLD_INST; \
    case 39: goto SETEND_INST; \
    case 40: goto CLREX_INST; \
    case 41: goto REV16_INST; \
    case 42: goto USAD8_INST; \
    case 43: goto SXTB_INST; \
    case 44: goto UXTB_INST; \
    case 45: goto SXTH_INST; \
    case 46: goto SXTB16_INST; \
    case 47: goto UXTH_INST; \
    case 48: goto UXTB16_INST; \
    case 49: goto CPY_INST; \
    case 50: goto UXTAB_INST; \
    case 51: goto SSUB8_INST; \
    case 52: goto SHSUB8_INST; \
    case 53: goto SSUBADDX_INST; \
    case 54: goto STREX_INST; \
    case 55: goto STREXB_INST; \
    case 56: goto SWP_INST; \
    case 57: goto SWPB_INST; \
    case 58: goto SSUB16_INST; \
    case 59: goto SSAT16_INST; \
    case 60: goto SHSUBADDX_INST; \
    case 61: goto QSUBADDX_INST; \
    case 62: goto SHADDSUBX_INST; \
    case 63: goto SHADD8_INST; \
    case 64: goto SHADD16_INST; \
    case 65: goto SEL_INST; \
    case 66: goto SADDSUBX_INST; \
    case 67: goto SADD8_INST; \
    case 68: goto SADD16_INST; \
    case 69: goto SHSUB16_INST; \
    case 70: goto UMAAL_INST; \
    case 71: goto UXTAB16_INST; \
    case 72: goto USUBADDX_INST; \
    case 73: goto USUB8_INST; \
    case 74: goto USUB16_INST; \
    case 75: goto USAT16_INST; \
    case 76: goto USADA8_INST; \
    case 77: goto UQSUBADDX_INST; \
    case 78: goto UQSUB8_INST; \
    case 79: goto UQSUB16_INST; \
    case 80: goto UQADDSUBX_INST; \
    case 81: goto UQADD8_INST; \
    case 82: goto UQADD16_INST; \
    case 83: goto SXTAB_INST; \
    case 84: goto UHSUBADDX_INST; \
    case 85: goto UHSUB8_INST; \
    case 86: goto UHSUB16_INST; \
    case 87: goto UHADDSUBX_INST; \
    case 88: goto UHADD8_INST; \
    case 89: goto UHADD16_INST; \
    case 90: goto UADDSUBX_INST; \
    case 91: goto UADD8_INST; \
    case 92: goto UADD16_INST; \
    case 93: goto SXTAH_INST; \
    case 94: goto SXTAB16_INST; \
    case 95: goto QADD8_INST; \
    case 96: goto BXJ_INST; \
    case 97: goto CLZ_INST; \
    case 98: goto UXTAH_INST; \
    case 99: goto BX_INST; \
    case 100: goto REV_INST; \
    case 101: goto BLX_INST; \
    case 102: goto REVSH_INST; \
    case 103: goto QADD_INST; \
    case 104: goto QADD16_INST; \
    case 105: goto QADDSUBX_INST; \
    case 106: goto LDREX_INST; \
    case 107: goto QDADD_INST; \
    case 108: goto QDSUB_INST; \
    case 109: goto QSUB_INST; \
    case 110: goto LDREXB_INST; \
    case 111: goto QSUB8_INST; \
    case 112: goto QSUB16_INST; \
    case 113: goto SMUAD_INST; \
    case 114: goto SMMUL_INST; \
    case 115: goto SMUSD_INST; \
    case 116: goto SMLSD_INST; \
    case 117: goto SMLSLD_INST; \
    case 118: goto SMMLA_INST; \
    case 119: goto SMMLS_INST; \
    case 120: goto SMLALD_INST; \
    case 121: goto SMLAD_INST; \
    case 122: goto SMLAW_INST; \
    case 123: goto SMULW_INST; \
    case 124: goto PKHTB_INST; \
    case 125: goto PKHBT_INST; \
    case 126: goto SMUL_INST; \
    case 127: goto SMLALXY_INST; \
    case 128: goto SMLA_INST; \
    case 129: goto MCRR_INST; \
    case 130: goto MRRC_INST; \
    case 131: goto CMP_INST; \
    case 132: goto TST_INST; \
    case 133: goto TEQ_INST; \
    case 134: goto CMN_INST; \
    case 135: goto SMULL_INST; \
    case 136: goto UMULL_INST; \
    case 137: goto UMLAL_INST; \
    case 138: goto SMLAL_INST; \
    case 139: goto MUL_INST; \
    case 140: goto MLA_INST; \
    case 141: goto SSAT_INST; \
    case 142: goto USAT_INST; \
    case 143: goto MRS_INST; \
    case 144: goto MSR_INST; \
    case 145: goto AND_INST; \
    case 146: goto BIC_INST; \
    case 147: goto LDM_INST; \
    case 148: goto EOR_INST; \
    case 149: goto ADD_INST; \
    case 150: goto RSB_INST; \
    case 151: goto RSC_INST; \
    case 152: goto SBC_INST; \
    case 153: goto ADC_INST; \
    case 154: goto SUB_INST; \
    case 155: goto ORR_INST; \
    case 156: goto MVN_INST; \
    case 157: goto MOV_INST; \
    case 158: goto STM_INST; \
    case 159: goto LDM_INST; \
    case 160: goto LDRSH_INST; \
    case 161: goto STM_INST; \
    case 162: goto LDM_INST; \
    case 163: goto LDRSB_INST; \
    case 164: goto STRD_INST; \
    case 165: goto LDRH_INST; \
    case 166: goto STRH_INST; \
    case 167: goto LDRD_INST; \
    case 168: goto STRT_INST; \
    case 169: goto STRBT_INST; \
    case 170: goto LDRBT_INST; \
    case 171: goto LDRT_INST; \
    case 172: goto MRC_INST; \
    case 173: goto MCR_INST; \
    case 174: goto MSR_INST; \
    case 175: goto LDRB_INST; \
    case 176: goto STRB_INST; \
    case 177: goto LDR_INST; \
    case 178: goto LDRCOND_INST ; \
    case 179: goto STR_INST; \
    case 180: goto CDP_INST; \
    case 181: goto STC_INST; \
    case 182: goto LDC_INST; \
    case 183: goto SWI_INST; \
    case 184: goto BBL_INST; \
    case 185: goto LDREXD_INST; \
    case 186: goto STREXD_INST; \
    case 187: goto LDREXH_INST; \
    case 188: goto STREXH_INST; \
    case 189: goto B_2_THUMB ; \
    case 190: goto B_COND_THUMB ; \
    case 191: goto BL_1_THUMB ; \
    case 192: goto BL_2_THUMB ; \
    case 193: goto BLX_1_THUMB ; \
    case 194: goto DISPATCH; \
    case 195: goto INIT_INST_LENGTH; \
    case 196: goto END; \
    }
#endif

    #define UPDATE_NFLAG(dst)    (cpu->NFlag = BIT(dst, 31) ? 1 : 0)
    #define UPDATE_ZFLAG(dst)    (cpu->ZFlag = dst ? 0 : 1)
    #define UPDATE_CFLAG_WITH_SC (cpu->CFlag = cpu->shifter_carry_out)

    #define SAVE_NZCVT cpu->Cpsr = (cpu->Cpsr & 0x0fffffdf) | \
                      (cpu->NFlag << 31) | \
                      (cpu->ZFlag << 30) | \
                      (cpu->CFlag << 29) | \
                      (cpu->VFlag << 28) | \
                      (cpu->TFlag << 5)
    #define LOAD_NZCVT cpu->NFlag = (cpu->Cpsr >> 31);     \
                       cpu->ZFlag = (cpu->Cpsr >> 30) & 1; \
                       cpu->CFlag = (cpu->Cpsr >> 29) & 1; \
                       cpu->VFlag = (cpu->Cpsr >> 28) & 1; \
                       cpu->TFlag = (cpu->Cpsr >> 5) & 1;

    #define CurrentModeHasSPSR (cpu->Mode != SYSTEM32MODE) && (cpu->Mode != USER32MODE)
    #define PC (cpu->Reg[15])
    #define CHECK_EXT_INT if (!cpu->NirqSig && !(cpu->Cpsr & 0x80)) goto END;

    arm_processor *cpu = state;

    // GCC and Clang have a C++ extension to support a lookup table of labels. Otherwise, fallback
    // to a clunky switch statement.
#if defined __GNUC__ || defined __clang__
    void *InstLabel[] = {
        &&VMLA_INST, &&VMLS_INST, &&VNMLA_INST, &&VNMLA_INST, &&VNMLS_INST, &&VNMUL_INST, &&VMUL_INST, &&VADD_INST, &&VSUB_INST,
        &&VDIV_INST, &&VMOVI_INST, &&VMOVR_INST, &&VABS_INST, &&VNEG_INST, &&VSQRT_INST, &&VCMP_INST, &&VCMP2_INST, &&VCVTBDS_INST,
        &&VCVTBFF_INST, &&VCVTBFI_INST, &&VMOVBRS_INST, &&VMSR_INST, &&VMOVBRC_INST, &&VMRS_INST, &&VMOVBCR_INST, &&VMOVBRRSS_INST,
        &&VMOVBRRD_INST, &&VSTR_INST, &&VPUSH_INST, &&VSTM_INST, &&VPOP_INST, &&VLDR_INST, &&VLDM_INST,

        &&SRS_INST,&&RFE_INST,&&BKPT_INST,&&BLX_INST,&&CPS_INST,&&PLD_INST,&&SETEND_INST,&&CLREX_INST,&&REV16_INST,&&USAD8_INST,&&SXTB_INST,
        &&UXTB_INST,&&SXTH_INST,&&SXTB16_INST,&&UXTH_INST,&&UXTB16_INST,&&CPY_INST,&&UXTAB_INST,&&SSUB8_INST,&&SHSUB8_INST,&&SSUBADDX_INST,
        &&STREX_INST,&&STREXB_INST,&&SWP_INST,&&SWPB_INST,&&SSUB16_INST,&&SSAT16_INST,&&SHSUBADDX_INST,&&QSUBADDX_INST,&&SHADDSUBX_INST,
        &&SHADD8_INST,&&SHADD16_INST,&&SEL_INST,&&SADDSUBX_INST,&&SADD8_INST,&&SADD16_INST,&&SHSUB16_INST,&&UMAAL_INST,&&UXTAB16_INST,
        &&USUBADDX_INST,&&USUB8_INST,&&USUB16_INST,&&USAT16_INST,&&USADA8_INST,&&UQSUBADDX_INST,&&UQSUB8_INST,&&UQSUB16_INST,
        &&UQADDSUBX_INST,&&UQADD8_INST,&&UQADD16_INST,&&SXTAB_INST,&&UHSUBADDX_INST,&&UHSUB8_INST,&&UHSUB16_INST,&&UHADDSUBX_INST,&&UHADD8_INST,
        &&UHADD16_INST,&&UADDSUBX_INST,&&UADD8_INST,&&UADD16_INST,&&SXTAH_INST,&&SXTAB16_INST,&&QADD8_INST,&&BXJ_INST,&&CLZ_INST,&&UXTAH_INST,
        &&BX_INST,&&REV_INST,&&BLX_INST,&&REVSH_INST,&&QADD_INST,&&QADD16_INST,&&QADDSUBX_INST,&&LDREX_INST,&&QDADD_INST,&&QDSUB_INST,
        &&QSUB_INST,&&LDREXB_INST,&&QSUB8_INST,&&QSUB16_INST,&&SMUAD_INST,&&SMMUL_INST,&&SMUSD_INST,&&SMLSD_INST,&&SMLSLD_INST,&&SMMLA_INST,
        &&SMMLS_INST,&&SMLALD_INST,&&SMLAD_INST,&&SMLAW_INST,&&SMULW_INST,&&PKHTB_INST,&&PKHBT_INST,&&SMUL_INST,&&SMLALXY_INST,&&SMLA_INST,
        &&MCRR_INST,&&MRRC_INST,&&CMP_INST,&&TST_INST,&&TEQ_INST,&&CMN_INST,&&SMULL_INST,&&UMULL_INST,&&UMLAL_INST,&&SMLAL_INST,&&MUL_INST,
        &&MLA_INST,&&SSAT_INST,&&USAT_INST,&&MRS_INST,&&MSR_INST,&&AND_INST,&&BIC_INST,&&LDM_INST,&&EOR_INST,&&ADD_INST,&&RSB_INST,&&RSC_INST,
        &&SBC_INST,&&ADC_INST,&&SUB_INST,&&ORR_INST,&&MVN_INST,&&MOV_INST,&&STM_INST,&&LDM_INST,&&LDRSH_INST,&&STM_INST,&&LDM_INST,&&LDRSB_INST,
        &&STRD_INST,&&LDRH_INST,&&STRH_INST,&&LDRD_INST,&&STRT_INST,&&STRBT_INST,&&LDRBT_INST,&&LDRT_INST,&&MRC_INST,&&MCR_INST,&&MSR_INST,
        &&LDRB_INST,&&STRB_INST,&&LDR_INST,&&LDRCOND_INST, &&STR_INST,&&CDP_INST,&&STC_INST,&&LDC_INST,&&SWI_INST,&&BBL_INST,&&LDREXD_INST,
        &&STREXD_INST,&&LDREXH_INST,&&STREXH_INST,&&B_2_THUMB, &&B_COND_THUMB,&&BL_1_THUMB, &&BL_2_THUMB, &&BLX_1_THUMB, &&DISPATCH,
        &&INIT_INST_LENGTH,&&END
        };
#endif
    arm_inst* inst_base;
    unsigned int addr;
    unsigned int phys_addr;
    unsigned int num_instrs = 0;

    int ptr;

    LOAD_NZCVT;
    DISPATCH:
    {
        if (!cpu->NirqSig) {
            if (!(cpu->Cpsr & 0x80)) {
                goto END;
            }
        }

        if (cpu->TFlag)
            cpu->Reg[15] &= 0xfffffffe;
        else
            cpu->Reg[15] &= 0xfffffffc;

        phys_addr = cpu->Reg[15];

        if (find_bb(cpu->Reg[15], ptr) == -1)
            if (InterpreterTranslate(cpu, ptr, cpu->Reg[15]) == FETCH_EXCEPTION)
                goto END;

        inst_base = (arm_inst *)&inst_buf[ptr];
        GOTO_NEXT_INST;
    }
    ADC_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            adc_inst* const inst_cream = (adc_inst*)inst_base->component;

            bool carry;
            bool overflow;
            RD = AddWithCarry(RN, SHIFTER_OPERAND, cpu->CFlag, &carry, &overflow);

            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                cpu->CFlag = carry;
                cpu->VFlag = overflow;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(adc_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(adc_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    ADD_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            add_inst* const inst_cream = (add_inst*)inst_base->component;

            u32 rn_val = RN;
            if (inst_cream->Rn == 15)
                rn_val += 2 * GET_INST_SIZE(cpu);

            bool carry;
            bool overflow;
            RD = AddWithCarry(rn_val, SHIFTER_OPERAND, 0, &carry, &overflow);

            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Cpsr & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                cpu->CFlag = carry;
                cpu->VFlag = overflow;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(add_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(add_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    AND_INST:
    {
        and_inst *inst_cream = (and_inst *)inst_base->component;
        if ((inst_base->cond == 0xe) || CondPassed(cpu, inst_base->cond)) {
            u32 lop = RN;
            u32 rop = SHIFTER_OPERAND;
            RD = lop & rop;
            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Cpsr & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                UPDATE_CFLAG_WITH_SC;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(and_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(and_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    BBL_INST:
    {
        if ((inst_base->cond == 0xe) || CondPassed(cpu, inst_base->cond)) {
            bbl_inst *inst_cream = (bbl_inst *)inst_base->component;
            if (inst_cream->L) {
                LINK_RTN_ADDR;
            }
            SET_PC;
            INC_PC(sizeof(bbl_inst));
            goto DISPATCH;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(bbl_inst));
        goto DISPATCH;
    }
    BIC_INST:
    {
        bic_inst *inst_cream = (bic_inst *)inst_base->component;
        if ((inst_base->cond == 0xe) || CondPassed(cpu, inst_base->cond)) {
            u32 lop = RN;
            if (inst_cream->Rn == 15) {
                lop += 2 * GET_INST_SIZE(cpu);
            }
            u32 rop = SHIFTER_OPERAND;
            RD = lop & (~rop);
            if ((inst_cream->S) && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                UPDATE_CFLAG_WITH_SC;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(bic_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(bic_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    BKPT_INST:
    BLX_INST:
    {
        blx_inst *inst_cream = (blx_inst *)inst_base->component;
        if ((inst_base->cond == 0xe) || CondPassed(cpu, inst_base->cond)) {
            unsigned int inst = inst_cream->inst;
            if (BITS(inst, 20, 27) == 0x12 && BITS(inst, 4, 7) == 0x3) {
                cpu->Reg[14] = (cpu->Reg[15] + GET_INST_SIZE(cpu));
                if(cpu->TFlag)
                    cpu->Reg[14] |= 0x1;
                cpu->Reg[15] = cpu->Reg[inst_cream->val.Rm] & 0xfffffffe;
                cpu->TFlag = cpu->Reg[inst_cream->val.Rm] & 0x1;
            } else {
                cpu->Reg[14] = (cpu->Reg[15] + GET_INST_SIZE(cpu));
                cpu->TFlag = 0x1;
                int signed_int = inst_cream->val.signed_immed_24;
                signed_int = (signed_int & 0x800000) ? (0x3F000000 | signed_int) : signed_int;
                signed_int = signed_int << 2;
                cpu->Reg[15] = cpu->Reg[15] + 8 + signed_int + (BIT(inst, 24) << 1);
            }
            INC_PC(sizeof(blx_inst));
            goto DISPATCH;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(blx_inst));
        goto DISPATCH;
    }

    BX_INST:
    BXJ_INST:
    {
        // Note that only the 'fail' case of BXJ is emulated. This is because
        // the facilities for Jazelle emulation are not implemented.
        //
        // According to the ARM documentation on BXJ, if setting the J bit in the APSR
        // fails, then BXJ functions identically like a regular BX instruction.
        //
        // This is sufficient for citra, as the CPU for the 3DS does not implement Jazelle.

        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            bx_inst* const inst_cream = (bx_inst*)inst_base->component;

            if (inst_cream->Rm == 15)
                LOG_WARNING(Core_ARM11, "BX at pc %x: use of Rm = R15 is discouraged", cpu->Reg[15]);

            cpu->TFlag = cpu->Reg[inst_cream->Rm] & 0x1;
            cpu->Reg[15] = cpu->Reg[inst_cream->Rm] & 0xfffffffe;
            INC_PC(sizeof(bx_inst));
            goto DISPATCH;
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(bx_inst));
        goto DISPATCH;
    }

    CDP_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            // Undefined instruction here
            cpu->NumInstrsToExecute = 0;
            return num_instrs;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(cdp_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    CLREX_INST:
    {
        remove_exclusive(cpu, 0);
        cpu->exclusive_state = 0;

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(clrex_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    CLZ_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            clz_inst* inst_cream = (clz_inst*)inst_base->component;
            RD = clz(RM);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(clz_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    CMN_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            cmn_inst* const inst_cream = (cmn_inst*)inst_base->component;

            bool carry;
            bool overflow;
            u32 result = AddWithCarry(RN, SHIFTER_OPERAND, 0, &carry, &overflow);

            UPDATE_NFLAG(result);
            UPDATE_ZFLAG(result);
            cpu->CFlag = carry;
            cpu->VFlag = overflow;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(cmn_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    CMP_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            cmp_inst* const inst_cream = (cmp_inst*)inst_base->component;

            u32 rn_val = RN;
            if (inst_cream->Rn == 15)
                rn_val += 2 * GET_INST_SIZE(cpu);

            bool carry;
            bool overflow;
            u32 result = AddWithCarry(rn_val, ~SHIFTER_OPERAND, 1, &carry, &overflow);

            UPDATE_NFLAG(result);
            UPDATE_ZFLAG(result);
            cpu->CFlag = carry;
            cpu->VFlag = overflow;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(cmp_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    CPS_INST:
    {
        cps_inst *inst_cream = (cps_inst *)inst_base->component;
        uint32_t aif_val = 0;
        uint32_t aif_mask = 0;
        if (InAPrivilegedMode(cpu)) {
            if (inst_cream->imod1) {
                if (inst_cream->A) {
                    aif_val |= (inst_cream->imod0 << 8);
                    aif_mask |= 1 << 8;
                }
                if (inst_cream->I) {
                    aif_val |= (inst_cream->imod0 << 7);
                    aif_mask |= 1 << 7;
                }
                if (inst_cream->F) {
                    aif_val |= (inst_cream->imod0 << 6);
                    aif_mask |= 1 << 6;
                }
                aif_mask = ~aif_mask;
                cpu->Cpsr = (cpu->Cpsr & aif_mask) | aif_val;
            }
            if (inst_cream->mmod) {
                cpu->Cpsr = (cpu->Cpsr & 0xffffffe0) | inst_cream->mode;
                switch_mode(cpu, inst_cream->mode);
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(cps_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    CPY_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            mov_inst* inst_cream = (mov_inst*)inst_base->component;

            RD = SHIFTER_OPERAND;
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(mov_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(mov_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    EOR_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            eor_inst* inst_cream = (eor_inst*)inst_base->component;

            u32 lop = RN;
            if (inst_cream->Rn == 15) {
                lop += 2 * GET_INST_SIZE(cpu);
            }
            u32 rop = SHIFTER_OPERAND;
            RD = lop ^ rop;
            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                UPDATE_CFLAG_WITH_SC;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(eor_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(eor_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDC_INST:
    {
        // Instruction not implemented
        //LOG_CRITICAL(Core_ARM11, "unimplemented instruction");
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldc_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDM_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);

            unsigned int inst = inst_cream->inst;
            if (BIT(inst, 22) && !BIT(inst, 15)) {
                for (int i = 0; i < 13; i++) {
                    if(BIT(inst, i)) {
                        cpu->Reg[i] = Memory::Read32(addr);
                        addr += 4;
                    }
                }
                if (BIT(inst, 13)) {
                    if (cpu->Mode == USER32MODE) 
                        cpu->Reg[13] = Memory::Read32(addr);
                    else
                        cpu->Reg_usr[0] = Memory::Read32(addr);

                    addr += 4;
                }
                if (BIT(inst, 14)) {
                    if (cpu->Mode == USER32MODE) 
                        cpu->Reg[14] = Memory::Read32(addr);
                    else
                        cpu->Reg_usr[1] = Memory::Read32(addr);
                }
            } else if (!BIT(inst, 22)) {
                for(int i = 0; i < 16; i++ ){
                    if(BIT(inst, i)){
                        unsigned int ret = Memory::Read32(addr);

                        // For armv5t, should enter thumb when bits[0] is non-zero.
                        if(i == 15){
                            cpu->TFlag = ret & 0x1;
                            ret &= 0xFFFFFFFE;
                        }

                        cpu->Reg[i] = ret;
                        addr += 4;
                    }
                }
            } else if (BIT(inst, 22) && BIT(inst, 15)) {
                for(int i = 0; i < 15; i++ ){
                    if(BIT(inst, i)){
                        cpu->Reg[i] = Memory::Read32(addr);
                        addr += 4;
                     }
                 }

                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Cpsr & 0x1f);
                    LOAD_NZCVT;
                }

                cpu->Reg[15] = Memory::Read32(addr);
            }

            if (BIT(inst, 15)) {
                INC_PC(sizeof(ldst_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    SXTH_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            sxth_inst* inst_cream = (sxth_inst*)inst_base->component;

            unsigned int operand2 = ROTATE_RIGHT_32(RM, 8 * inst_cream->rotate);
            if (BIT(operand2, 15)) {
                operand2 |= 0xffff0000;
            } else {
                operand2 &= 0xffff;
            }
            RD = operand2;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(sxth_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDR_INST:
    {
        ldst_inst *inst_cream = (ldst_inst *)inst_base->component;
        //if ((inst_base->cond == 0xe) || CondPassed(cpu, inst_base->cond)) {
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);

            unsigned int value = Memory::Read32(addr);
            if (BIT(CP15_REG(CP15_CONTROL), 22) == 1)
                cpu->Reg[BITS(inst_cream->inst, 12, 15)] = value;
            else {
                value = ROTATE_RIGHT_32(value,(8*(addr&0x3)));
                cpu->Reg[BITS(inst_cream->inst, 12, 15)] = value;
            }

            if (BITS(inst_cream->inst, 12, 15) == 15) {
                // For armv5t, should enter thumb when bits[0] is non-zero.
                cpu->TFlag = value & 0x1;
                cpu->Reg[15] &= 0xFFFFFFFE;
                INC_PC(sizeof(ldst_inst));
                goto DISPATCH;
            }
        //}

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDRCOND_INST:
    {
        if (CondPassed(cpu, inst_base->cond)) {
            ldst_inst *inst_cream = (ldst_inst *)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);

            unsigned int value = Memory::Read32(addr);
            if (BIT(CP15_REG(CP15_CONTROL), 22) == 1)
                cpu->Reg[BITS(inst_cream->inst, 12, 15)] = value;
            else {
                value = ROTATE_RIGHT_32(value,(8*(addr&0x3)));
                cpu->Reg[BITS(inst_cream->inst, 12, 15)] = value;
            }

            if (BITS(inst_cream->inst, 12, 15) == 15) {
                // For armv5t, should enter thumb when bits[0] is non-zero.
                cpu->TFlag = value & 0x1;
                cpu->Reg[15] &= 0xFFFFFFFE;
                INC_PC(sizeof(ldst_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    UXTH_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            uxth_inst* inst_cream = (uxth_inst*)inst_base->component;
            RD = ROTATE_RIGHT_32(RM, 8 * inst_cream->rotate) & 0xffff;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(uxth_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    UXTAH_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            uxtah_inst* inst_cream = (uxtah_inst*)inst_base->component;
            unsigned int operand2 = ROTATE_RIGHT_32(RM, 8 * inst_cream->rotate) & 0xffff;

            RD = RN + operand2;
            if (inst_cream->Rn == 15 || inst_cream->Rm == 15) {
                LOG_ERROR(Core_ARM11, "invalid operands for UXTAH");
                CITRA_IGNORE_EXIT(-1);
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(uxtah_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDRB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);

            cpu->Reg[BITS(inst_cream->inst, 12, 15)] = Memory::Read8(addr);

            if (BITS(inst_cream->inst, 12, 15) == 15) {
                INC_PC(sizeof(ldst_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDRBT_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);

            cpu->Reg[BITS(inst_cream->inst, 12, 15)] = Memory::Read8(addr);

            if (BITS(inst_cream->inst, 12, 15) == 15) {
                INC_PC(sizeof(ldst_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDRD_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            // Should check if RD is even-numbered, Rd != 14, addr[0:1] == 0, (CP15_reg1_U == 1 || addr[2] == 0)
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);

            cpu->Reg[BITS(inst_cream->inst, 12, 15)] = Memory::Read32(addr);
            cpu->Reg[BITS(inst_cream->inst, 12, 15) + 1] = Memory::Read32(addr + 4);

            // No dispatch since this operation should not modify R15
        }
        cpu->Reg[15] += 4;
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    LDREX_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* inst_cream = (generic_arm_inst*)inst_base->component;
            unsigned int read_addr = RN;

            add_exclusive_addr(cpu, read_addr);
            cpu->exclusive_state = 1;

            RD = Memory::Read32(read_addr);
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(generic_arm_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDREXB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* inst_cream = (generic_arm_inst*)inst_base->component;
            unsigned int read_addr = RN;

            add_exclusive_addr(cpu, read_addr);
            cpu->exclusive_state = 1;

            RD = Memory::Read8(read_addr);
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(generic_arm_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDREXH_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* inst_cream = (generic_arm_inst*)inst_base->component;
            unsigned int read_addr = RN;

            add_exclusive_addr(cpu, read_addr);
            cpu->exclusive_state = 1;

            RD = Memory::Read16(read_addr);
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(generic_arm_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDREXD_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* inst_cream = (generic_arm_inst*)inst_base->component;
            unsigned int read_addr = RN;

            add_exclusive_addr(cpu, read_addr);
            cpu->exclusive_state = 1;

            RD = Memory::Read32(read_addr);
            RD2 = Memory::Read32(read_addr + 4);

            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(generic_arm_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDRH_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);
            cpu->Reg[BITS(inst_cream->inst, 12, 15)] = Memory::Read16(addr);
            if (BITS(inst_cream->inst, 12, 15) == 15) {
                INC_PC(sizeof(ldst_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDRSB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);
            unsigned int value = Memory::Read8(addr);
            if (BIT(value, 7)) {
                value |= 0xffffff00;
            }
            cpu->Reg[BITS(inst_cream->inst, 12, 15)] = value;
            if (BITS(inst_cream->inst, 12, 15) == 15) {
                INC_PC(sizeof(ldst_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDRSH_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);
            unsigned int value = Memory::Read16(addr);
            if (BIT(value, 15)) {
                value |= 0xffff0000;
            }
            cpu->Reg[BITS(inst_cream->inst, 12, 15)] = value;
            if (BITS(inst_cream->inst, 12, 15) == 15) {
                INC_PC(sizeof(ldst_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    LDRT_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 1);

            unsigned int value = Memory::Read32(addr);
            cpu->Reg[BITS(inst_cream->inst, 12, 15)] = value;

            if (BIT(CP15_REG(CP15_CONTROL), 22) == 1)
                cpu->Reg[BITS(inst_cream->inst, 12, 15)] = value;
            else
                cpu->Reg[BITS(inst_cream->inst, 12, 15)] = ROTATE_RIGHT_32(value,(8*(addr&0x3))) ;

            if (BITS(inst_cream->inst, 12, 15) == 15) {
                INC_PC(sizeof(ldst_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    MCR_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            mcr_inst* inst_cream = (mcr_inst*)inst_base->component;

            unsigned int inst = inst_cream->inst;
            if (inst_cream->Rd == 15) {
                DEBUG_MSG;
            } else {
                if (inst_cream->cp_num == 15) {
                    if(CRn == 0 && OPCODE_2 == 0 && CRm == 0) {
                        CP15_REG(CP15_MAIN_ID) = RD;
                    } else if (CRn == 1 && CRm == 0 && OPCODE_2 == 1) {
                        CP15_REG(CP15_AUXILIARY_CONTROL) = RD;
                    } else if (CRn == 1 && CRm == 0 && OPCODE_2 == 2) {
                        CP15_REG(CP15_COPROCESSOR_ACCESS_CONTROL) = RD;
                    } else if(CRn == 1 && CRm == 0 && OPCODE_2 == 0) {
                        CP15_REG(CP15_CONTROL) = RD;
                    } else if (CRn == 3 && CRm == 0 && OPCODE_2 == 0) {
                        CP15_REG(CP15_DOMAIN_ACCESS_CONTROL) = RD;
                    } else if (CRn == 2 && CRm == 0 && OPCODE_2 == 0) {
                        CP15_REG(CP15_TRANSLATION_BASE_TABLE_0) = RD;
                    } else if (CRn == 2 && CRm == 0 && OPCODE_2 == 1) {
                        CP15_REG(CP15_TRANSLATION_BASE_TABLE_1) = RD;
                    } else if (CRn == 2 && CRm == 0 && OPCODE_2 == 2) {
                        CP15_REG(CP15_TRANSLATION_BASE_CONTROL) = RD;
                    } else if(CRn == MMU_CACHE_OPS){
                        //LOG_WARNING(Core_ARM11, "cache operations have not implemented.");
                    } else if(CRn == MMU_TLB_OPS){
                        switch (CRm) {
                        case 5: // ITLB
                            switch(OPCODE_2) {
                            case 0: // Invalidate all
                                LOG_DEBUG(Core_ARM11, "{TLB} [INSN] invalidate all");
                                break;
                            case 1: // Invalidate by MVA
                                LOG_DEBUG(Core_ARM11, "{TLB} [INSN] invalidate by mva");
                                break;
                            case 2: // Invalidate by asid
                                LOG_DEBUG(Core_ARM11, "{TLB} [INSN] invalidate by asid");
                                break;
                            default:
                                break;
                            }

                            break;
                        case 6: // DTLB
                            switch(OPCODE_2){
                            case 0: // Invalidate all
                                LOG_DEBUG(Core_ARM11, "{TLB} [DATA] invalidate all");
                                break;
                            case 1: // Invalidate by MVA
                                LOG_DEBUG(Core_ARM11, "{TLB} [DATA] invalidate by mva");
                                break;
                            case 2: // Invalidate by asid
                                LOG_DEBUG(Core_ARM11, "{TLB} [DATA] invalidate by asid");
                                break;
                            default:
                                break;
                            }
                            break;
                        case 7: // UNIFILED TLB
                            switch(OPCODE_2){
                            case 0: // invalidate all
                                LOG_DEBUG(Core_ARM11, "{TLB} [UNIFILED] invalidate all");
                                break;
                            case 1: // Invalidate by MVA
                                LOG_DEBUG(Core_ARM11, "{TLB} [UNIFILED] invalidate by mva");
                                break;
                            case 2: // Invalidate by asid
                                LOG_DEBUG(Core_ARM11, "{TLB} [UNIFILED] invalidate by asid");
                                break;
                            default:
                                break;
                            }
                            break;
                        default:
                            break;
                        }
                    } else if(CRn == MMU_PID) {
                        if(OPCODE_2 == 0)
                            CP15_REG(CP15_PID) = RD;
                        else if(OPCODE_2 == 1)
                            CP15_REG(CP15_CONTEXT_ID) = RD;
                        else if(OPCODE_2 == 3) {
                            CP15_REG(CP15_THREAD_URO) = RD;
                        } else {
                            LOG_ERROR(Core_ARM11, "mmu_mcr wrote UNKNOWN - reg %d", CRn);
                        }
                    } else {
                        LOG_ERROR(Core_ARM11, "mcr CRn=%d, CRm=%d OP2=%d is not implemented", CRn, CRm, OPCODE_2);
                    }
                }
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(mcr_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    MCRR_INST:
    MLA_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            mla_inst* inst_cream = (mla_inst*)inst_base->component;

            uint64_t rm = RM;
            uint64_t rs = RS;
            uint64_t rn = RN;
            if (inst_cream->Rm == 15 || inst_cream->Rs == 15 || inst_cream->Rn == 15) {
                LOG_ERROR(Core_ARM11, "invalid operands for MLA");
                CITRA_IGNORE_EXIT(-1);
            }
            RD = static_cast<uint32_t>((rm * rs + rn) & 0xffffffff);
            if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(mla_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(mla_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    MOV_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            mov_inst* inst_cream = (mov_inst*)inst_base->component;

            RD = SHIFTER_OPERAND;
            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                UPDATE_CFLAG_WITH_SC;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(mov_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(mov_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    MRC_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            mrc_inst* inst_cream = (mrc_inst*)inst_base->component;

            unsigned int inst = inst_cream->inst;
            if (inst_cream->Rd == 15) {
                DEBUG_MSG;
            }
            if (inst_cream->inst == 0xeef04a10) {
                // Undefined instruction fmrx
                RD = 0x20000000;
                CITRA_IGNORE_EXIT(-1);
                goto END;
            } else {
                if (inst_cream->cp_num == 15) {
                    if(CRn == 0 && OPCODE_2 == 0 && CRm == 0) {
                        RD = cpu->CP15[CP15(CP15_MAIN_ID)];
                    } else if (CRn == 1 && CRm == 0 && OPCODE_2 == 0) {
                        RD = cpu->CP15[CP15(CP15_CONTROL)];
                    } else if (CRn == 1 && CRm == 0 && OPCODE_2 == 1) {
                        RD = cpu->CP15[CP15(CP15_AUXILIARY_CONTROL)];
                    } else if (CRn == 1 && CRm == 0 && OPCODE_2 == 2) {
                        RD = cpu->CP15[CP15(CP15_COPROCESSOR_ACCESS_CONTROL)];
                    } else if (CRn == 3 && CRm == 0 && OPCODE_2 == 0) {
                        RD = cpu->CP15[CP15(CP15_DOMAIN_ACCESS_CONTROL)];
                    } else if (CRn == 2 && CRm == 0 && OPCODE_2 == 0) {
                        RD = cpu->CP15[CP15(CP15_TRANSLATION_BASE_TABLE_0)];
                    } else if (CRn == 5 && CRm == 0 && OPCODE_2 == 0) {
                        RD = cpu->CP15[CP15(CP15_FAULT_STATUS)];
                    } else if (CRn == 6 && CRm == 0 && OPCODE_2 == 0) {
                        RD = cpu->CP15[CP15(CP15_FAULT_ADDRESS)];
                    } else if (CRn == 0 && CRm == 0 && OPCODE_2 == 1) {
                        RD = cpu->CP15[CP15(CP15_CACHE_TYPE)];
                    } else if (CRn == 5 && CRm == 0 && OPCODE_2 == 1) {
                        RD = cpu->CP15[CP15(CP15_INSTR_FAULT_STATUS)];
                    } else if (CRn == 13) {
                        if(OPCODE_2 == 0)
                            RD = CP15_REG(CP15_PID);
                        else if(OPCODE_2 == 1)
                            RD = CP15_REG(CP15_CONTEXT_ID);
                        else if(OPCODE_2 == 3) {
                            RD = Memory::KERNEL_MEMORY_VADDR;
                        } else {
                            LOG_ERROR(Core_ARM11, "mmu_mrr wrote UNKNOWN - reg %d", CRn);
                        }
                    } else {
                        LOG_ERROR(Core_ARM11, "mrc CRn=%d, CRm=%d, OP2=%d is not implemented", CRn, CRm, OPCODE_2);
                    }
                }
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(mrc_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    MRRC_INST:
    MRS_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            mrs_inst* inst_cream = (mrs_inst*)inst_base->component;

            if (inst_cream->R) {
                RD = cpu->Spsr_copy;
            } else {
                SAVE_NZCVT;
                RD = cpu->Cpsr;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(mrs_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    MSR_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            msr_inst* inst_cream = (msr_inst*)inst_base->component;
            const uint32_t UnallocMask = 0x06f0fc00, UserMask = 0xf80f0200, PrivMask = 0x000001df, StateMask = 0x01000020;
            unsigned int inst = inst_cream->inst;
            unsigned int operand;

            if (BIT(inst, 25)) {
                int rot_imm = BITS(inst, 8, 11) * 2;
                operand = ROTATE_RIGHT_32(BITS(inst, 0, 7), rot_imm);
            } else {
                operand = cpu->Reg[BITS(inst, 0, 3)];
            }
            uint32_t byte_mask = (BIT(inst, 16) ? 0xff : 0) | (BIT(inst, 17) ? 0xff00 : 0)
                        | (BIT(inst, 18) ? 0xff0000 : 0) | (BIT(inst, 19) ? 0xff000000 : 0);
            uint32_t mask = 0;
            if (!inst_cream->R) {
                if (InAPrivilegedMode(cpu)) {
                    if ((operand & StateMask) != 0) {
                        /// UNPREDICTABLE
                        DEBUG_MSG;
                    } else
                        mask = byte_mask & (UserMask | PrivMask);
                } else {
                    mask = byte_mask & UserMask;
                }
                SAVE_NZCVT;

                cpu->Cpsr = (cpu->Cpsr & ~mask) | (operand & mask);
                switch_mode(cpu, cpu->Cpsr & 0x1f);
                LOAD_NZCVT;
            } else {
                if (CurrentModeHasSPSR) {
                    mask = byte_mask & (UserMask | PrivMask | StateMask);
                    cpu->Spsr_copy = (cpu->Spsr_copy & ~mask) | (operand & mask);
                }
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(msr_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    MUL_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            mul_inst* inst_cream = (mul_inst*)inst_base->component;

            uint64_t rm = RM;
            uint64_t rs = RS;
            RD = static_cast<uint32_t>((rm * rs) & 0xffffffff);
            if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(mul_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(mul_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    MVN_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            mvn_inst* const inst_cream = (mvn_inst*)inst_base->component;

            RD = ~SHIFTER_OPERAND;

            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                UPDATE_CFLAG_WITH_SC;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(mvn_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(mvn_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    ORR_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            orr_inst* const inst_cream = (orr_inst*)inst_base->component;

            u32 lop = RN;
            u32 rop = SHIFTER_OPERAND;
            RD = lop | rop;

            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                UPDATE_CFLAG_WITH_SC;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(orr_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(orr_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    PKHBT_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            pkh_inst *inst_cream = (pkh_inst *)inst_base->component;
            RD = (RN & 0xFFFF) | ((RM << inst_cream->imm) & 0xFFFF0000);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(pkh_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    PKHTB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            pkh_inst *inst_cream = (pkh_inst *)inst_base->component;
            int shift_imm = inst_cream->imm ? inst_cream->imm : 31;
            RD = ((static_cast<s32>(RM) >> shift_imm) & 0xFFFF) | (RN & 0xFFFF0000);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(pkh_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    PLD_INST:
    {
        // Instruction not implemented
        //LOG_CRITICAL(Core_ARM11, "unimplemented instruction");
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(stc_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    QADD_INST:
    QDADD_INST:
    QDSUB_INST:
    QSUB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;
            const u8 op1 = inst_cream->op1;
            const u32 rm_val = RM;
            const u32 rn_val = RN;

            u32 result = 0;

            // QADD
            if (op1 == 0x00) {
                result = rm_val + rn_val;

                if (AddOverflow(rm_val, rn_val, result)) {
                    result = POS(result) ? 0x80000000 : 0x7FFFFFFF;
                    cpu->Cpsr |= (1 << 27);
                }
            }
            // QSUB
            else if (op1 == 0x01) {
                result = rm_val - rn_val;

                if (SubOverflow(rm_val, rn_val, result)) {
                    result = POS(result) ? 0x80000000 : 0x7FFFFFFF;
                    cpu->Cpsr |= (1 << 27);
                }
            }
            // QDADD
            else if (op1 == 0x02) {
                u32 mul = (rn_val * 2);

                if (AddOverflow(rn_val, rn_val, rn_val * 2)) {
                    mul = POS(mul) ? 0x80000000 : 0x7FFFFFFF;
                    cpu->Cpsr |= (1 << 27);
                }

                result = mul + rm_val;

                if (AddOverflow(rm_val, mul, result)) {
                    result = POS(result) ? 0x80000000 : 0x7FFFFFFF;
                    cpu->Cpsr |= (1 << 27);
                }
            }
            // QDSUB
            else if (op1 == 0x03) {
                u32 mul = (rn_val * 2);

                if (AddOverflow(rn_val, rn_val, mul)) {
                    mul = POS(mul) ? 0x80000000 : 0x7FFFFFFF;
                    cpu->Cpsr |= (1 << 27);
                }

                result = rm_val - mul;

                if (SubOverflow(rm_val, mul, result)) {
                    result = POS(result) ? 0x80000000 : 0x7FFFFFFF;
                    cpu->Cpsr |= (1 << 27);
                }
            }

            RD = result;
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    QADD8_INST:
    QADD16_INST:
    QADDSUBX_INST:
    QSUB8_INST:
    QSUB16_INST:
    QSUBADDX_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;
            const u16 rm_lo = (RM & 0xFFFF);
            const u16 rm_hi = ((RM >> 16) & 0xFFFF);
            const u16 rn_lo = (RN & 0xFFFF);
            const u16 rn_hi = ((RN >> 16) & 0xFFFF);
            const u8 op2    = inst_cream->op2;

            u16 lo_result = 0;
            u16 hi_result = 0;

            // QADD16
            if (op2 == 0x00) {
                lo_result = ARMul_SignedSaturatedAdd16(rn_lo, rm_lo);
                hi_result = ARMul_SignedSaturatedAdd16(rn_hi, rm_hi);
            }
            // QASX
            else if (op2 == 0x01) {
                lo_result = ARMul_SignedSaturatedSub16(rn_lo, rm_hi);
                hi_result = ARMul_SignedSaturatedAdd16(rn_hi, rm_lo);
            }
            // QSAX
            else if (op2 == 0x02) {
                lo_result = ARMul_SignedSaturatedAdd16(rn_lo, rm_hi);
                hi_result = ARMul_SignedSaturatedSub16(rn_hi, rm_lo);
            }
            // QSUB16
            else if (op2 == 0x03) {
                lo_result = ARMul_SignedSaturatedSub16(rn_lo, rm_lo);
                hi_result = ARMul_SignedSaturatedSub16(rn_hi, rm_hi);
            }
            // QADD8
            else if (op2 == 0x04) {
                lo_result = ARMul_SignedSaturatedAdd8(rn_lo & 0xFF, rm_lo & 0xFF) |
                            ARMul_SignedSaturatedAdd8(rn_lo >> 8, rm_lo >> 8) << 8;
                hi_result = ARMul_SignedSaturatedAdd8(rn_hi & 0xFF, rm_hi & 0xFF) |
                            ARMul_SignedSaturatedAdd8(rn_hi >> 8, rm_hi >> 8) << 8;
            }
            // QSUB8
            else if (op2 == 0x07) {
                lo_result = ARMul_SignedSaturatedSub8(rn_lo & 0xFF, rm_lo & 0xFF) |
                            ARMul_SignedSaturatedSub8(rn_lo >> 8, rm_lo >> 8) << 8;
                hi_result = ARMul_SignedSaturatedSub8(rn_hi & 0xFF, rm_hi & 0xFF) |
                            ARMul_SignedSaturatedSub8(rn_hi >> 8, rm_hi >> 8) << 8;
            }

            RD = (lo_result & 0xFFFF) | ((hi_result & 0xFFFF) << 16);
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    REV_INST:
    REV16_INST:
    REVSH_INST:
    {
        
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            rev_inst* const inst_cream = (rev_inst*)inst_base->component;

            const u8 op1 = inst_cream->op1;
            const u8 op2 = inst_cream->op2;

            // REV
            if (op1 == 0x03 && op2 == 0x01) {
                RD = ((RM & 0xFF) << 24) | (((RM >> 8) & 0xFF) << 16) | (((RM >> 16) & 0xFF) << 8) | ((RM >> 24) & 0xFF);
            }
            // REV16
            else if (op1 == 0x03 && op2 == 0x05) {
                RD = ((RM & 0xFF) << 8) | ((RM & 0xFF00) >> 8) | ((RM & 0xFF0000) << 8) | ((RM & 0xFF000000) >> 8);
            }
            // REVSH
            else if (op1 == 0x07 && op2 == 0x05) {
                RD = ((RM & 0xFF) << 8) | ((RM & 0xFF00) >> 8);
                if (RD & 0x8000)
                    RD |= 0xffff0000;
            }
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(rev_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    RFE_INST:
    RSB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            rsb_inst* const inst_cream = (rsb_inst*)inst_base->component;

            u32 rn_val = RN;
            if (inst_cream->Rn == 15)
                rn_val += 2 * GET_INST_SIZE(cpu);

            bool carry;
            bool overflow;
            RD = AddWithCarry(~rn_val, SHIFTER_OPERAND, 1, &carry, &overflow);

            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                cpu->CFlag = carry;
                cpu->VFlag = overflow;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(rsb_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(rsb_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    RSC_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            rsc_inst* const inst_cream = (rsc_inst*)inst_base->component;

            bool carry;
            bool overflow;
            RD = AddWithCarry(~RN, SHIFTER_OPERAND, cpu->CFlag, &carry, &overflow);

            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                cpu->CFlag = carry;
                cpu->VFlag = overflow;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(rsc_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(rsc_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SADD8_INST:
    SSUB8_INST:
    SADD16_INST:
    SADDSUBX_INST:
    SSUBADDX_INST:
    SSUB16_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;
            const u8 op2 = inst_cream->op2;

            if (op2 == 0x00 || op2 == 0x01 || op2 == 0x02 || op2 == 0x03) {
                const s16 rn_lo = (RN & 0xFFFF);
                const s16 rn_hi = ((RN >> 16) & 0xFFFF);
                const s16 rm_lo = (RM & 0xFFFF);
                const s16 rm_hi = ((RM >> 16) & 0xFFFF);

                s32 lo_result = 0;
                s32 hi_result = 0;

                // SADD16
                if (inst_cream->op2 == 0x00) {
                    lo_result = (rn_lo + rm_lo);
                    hi_result = (rn_hi + rm_hi);
                }
                // SASX
                else if (op2 == 0x01) {
                    lo_result = (rn_lo - rm_hi);
                    hi_result = (rn_hi + rm_lo);
                }
                // SSAX
                else if (op2 == 0x02) {
                    lo_result = (rn_lo + rm_hi);
                    hi_result = (rn_hi - rm_lo);
                }
                // SSUB16
                else if (op2 == 0x03) {
                    lo_result = (rn_lo - rm_lo);
                    hi_result = (rn_hi - rm_hi);
                }

                RD = (lo_result & 0xFFFF) | ((hi_result & 0xFFFF) << 16);

                if (lo_result >= 0) {
                    cpu->Cpsr |= (1 << 16);
                    cpu->Cpsr |= (1 << 17);
                } else {
                    cpu->Cpsr &= ~(1 << 16);
                    cpu->Cpsr &= ~(1 << 17);
                }

                if (hi_result >= 0) {
                    cpu->Cpsr |= (1 << 18);
                    cpu->Cpsr |= (1 << 19);
                } else {
                    cpu->Cpsr &= ~(1 << 18);
                    cpu->Cpsr &= ~(1 << 19);
                }
            }
            else if (op2 == 0x04 || op2 == 0x07) {
                s32 lo_val1, lo_val2;
                s32 hi_val1, hi_val2;

                // SADD8
                if (op2 == 0x04) {
                    lo_val1 = (s32)(s8)(RN & 0xFF) + (s32)(s8)(RM & 0xFF);
                    lo_val2 = (s32)(s8)((RN >> 8) & 0xFF)  + (s32)(s8)((RM >> 8) & 0xFF);
                    hi_val1 = (s32)(s8)((RN >> 16) & 0xFF) + (s32)(s8)((RM >> 16) & 0xFF);
                    hi_val2 = (s32)(s8)((RN >> 24) & 0xFF) + (s32)(s8)((RM >> 24) & 0xFF);
                }
                // SSUB8
                else {
                    lo_val1 = (s32)(s8)(RN & 0xFF) - (s32)(s8)(RM & 0xFF);
                    lo_val2 = (s32)(s8)((RN >> 8) & 0xFF) - (s32)(s8)((RM >> 8) & 0xFF);
                    hi_val1 = (s32)(s8)((RN >> 16) & 0xFF) - (s32)(s8)((RM >> 16) & 0xFF);
                    hi_val2 = (s32)(s8)((RN >> 24) & 0xFF) - (s32)(s8)((RM >> 24) & 0xFF);
                }

                RD =  ((lo_val1 & 0xFF) | ((lo_val2 & 0xFF) << 8) | ((hi_val1 & 0xFF) << 16) | ((hi_val2 & 0xFF) << 24));

                if (lo_val1 >= 0)
                    cpu->Cpsr |= (1 << 16);
                else
                    cpu->Cpsr &= ~(1 << 16);

                if (lo_val2 >= 0)
                    cpu->Cpsr |= (1 << 17);
                else
                    cpu->Cpsr &= ~(1 << 17);

                if (hi_val1 >= 0)
                    cpu->Cpsr |= (1 << 18);
                else
                    cpu->Cpsr &= ~(1 << 18);

                if (hi_val2 >= 0)
                    cpu->Cpsr |= (1 << 19);
                else
                    cpu->Cpsr &= ~(1 << 19);
            }
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SBC_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            sbc_inst* const inst_cream = (sbc_inst*)inst_base->component;

            bool carry;
            bool overflow;
            RD = AddWithCarry(RN, ~SHIFTER_OPERAND, cpu->CFlag, &carry, &overflow);

            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                cpu->CFlag = carry;
                cpu->VFlag = overflow;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(sbc_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(sbc_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SEL_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

            const u32 to = RM;
            const u32 from = RN;
            const u32 cpsr = cpu->Cpsr;

            u32 result;
            if (cpsr & (1 << 16))
                result = from & 0xff;
            else
                result = to & 0xff;

            if (cpsr & (1 << 17))
                result |= from & 0x0000ff00;
            else
                result |= to & 0x0000ff00;

            if (cpsr & (1 << 18))
                result |= from & 0x00ff0000;
            else
                result |= to & 0x00ff0000;

            if (cpsr & (1 << 19))
                result |= from & 0xff000000;
            else
                result |= to & 0xff000000;

            RD = result;
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SETEND_INST:

    SHADD8_INST:
    SHADD16_INST:
    SHADDSUBX_INST:
    SHSUB8_INST:
    SHSUB16_INST:
    SHSUBADDX_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

            const u8 op2 = inst_cream->op2;
            const u32 rm_val = RM;
            const u32 rn_val = RN;

            if (op2 == 0x00 || op2 == 0x01 || op2 == 0x02 || op2 == 0x03) {
                s32 lo_result = 0;
                s32 hi_result = 0;

                // SHADD16
                if (op2 == 0x00) {
                    lo_result = ((s16)(rn_val & 0xFFFF) + (s16)(rm_val & 0xFFFF)) >> 1;
                    hi_result = ((s16)((rn_val >> 16) & 0xFFFF) + (s16)((rm_val >> 16) & 0xFFFF)) >> 1;
                }
                // SHASX
                else if (op2 == 0x01) {
                    lo_result = ((s16)(rn_val & 0xFFFF) - (s16)((rm_val >> 16) & 0xFFFF)) >> 1;
                    hi_result = ((s16)((rn_val >> 16) & 0xFFFF) + (s16)(rm_val & 0xFFFF)) >> 1;
                }
                // SHSAX
                else if (op2 == 0x02) {
                    lo_result = ((s16)(rn_val & 0xFFFF) + (s16)((rm_val >> 16) & 0xFFFF)) >> 1;
                    hi_result = ((s16)((rn_val >> 16) & 0xFFFF) - (s16)(rm_val & 0xFFFF)) >> 1;
                }
                // SHSUB16
                else if (op2 == 0x03) {
                    lo_result = ((s16)(rn_val & 0xFFFF) - (s16)(rm_val & 0xFFFF)) >> 1;
                    hi_result = ((s16)((rn_val >> 16) & 0xFFFF) - (s16)((rm_val >> 16) & 0xFFFF)) >> 1;
                }

                RD = ((lo_result & 0xFFFF) | ((hi_result & 0xFFFF) << 16));
            }
            else if (op2 == 0x04 || op2 == 0x07) {
                s16 lo_val1, lo_val2;
                s16 hi_val1, hi_val2;

                // SHADD8
                if (op2 == 0x04) {
                    lo_val1 = ((s8)(rn_val & 0xFF) + (s8)(rm_val & 0xFF)) >> 1;
                    lo_val2 = ((s8)((rn_val >> 8) & 0xFF) + (s8)((rm_val >> 8) & 0xFF)) >> 1;

                    hi_val1 = ((s8)((rn_val >> 16) & 0xFF) + (s8)((rm_val >> 16) & 0xFF)) >> 1;
                    hi_val2 = ((s8)((rn_val >> 24) & 0xFF) + (s8)((rm_val >> 24) & 0xFF)) >> 1;
                }
                // SHSUB8
                else {
                    lo_val1 = ((s8)(rn_val & 0xFF) - (s8)(rm_val & 0xFF)) >> 1;
                    lo_val2 = ((s8)((rn_val >> 8) & 0xFF) - (s8)((rm_val >> 8) & 0xFF)) >> 1;

                    hi_val1 = ((s8)((rn_val >> 16) & 0xFF) - (s8)((rm_val >> 16) & 0xFF)) >> 1;
                    hi_val2 = ((s8)((rn_val >> 24) & 0xFF) - (s8)((rm_val >> 24) & 0xFF)) >> 1;
                }

                RD = (lo_val1 & 0xFF) | ((lo_val2 & 0xFF) << 8) | ((hi_val1 & 0xFF) << 16) | ((hi_val2 & 0xFF) << 24);
            }
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SMLA_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            smla_inst* inst_cream = (smla_inst*)inst_base->component;
            int32_t operand1, operand2;
            if (inst_cream->x == 0)
                operand1 = (BIT(RM, 15)) ? (BITS(RM, 0, 15) | 0xffff0000) : BITS(RM, 0, 15);
            else
                operand1 = (BIT(RM, 31)) ? (BITS(RM, 16, 31) | 0xffff0000) : BITS(RM, 16, 31);

            if (inst_cream->y == 0)
                operand2 = (BIT(RS, 15)) ? (BITS(RS, 0, 15) | 0xffff0000) : BITS(RS, 0, 15);
            else
                operand2 = (BIT(RS, 31)) ? (BITS(RS, 16, 31) | 0xffff0000) : BITS(RS, 16, 31);
            RD = operand1 * operand2 + RN;

            if (AddOverflow(operand1 * operand2, RN, RD))
                cpu->Cpsr |= (1 << 27);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(smla_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SMLAD_INST:
    SMLSD_INST:
    SMUAD_INST:
    SMUSD_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            smlad_inst* const inst_cream = (smlad_inst*)inst_base->component;
            const u8 op2 = inst_cream->op2;

            u32 rm_val = cpu->Reg[inst_cream->Rm];
            const u32 rn_val = cpu->Reg[inst_cream->Rn];

            if (inst_cream->m)
                rm_val = (((rm_val & 0xFFFF) << 16) | (rm_val >> 16));

            const s16 rm_lo = (rm_val & 0xFFFF);
            const s16 rm_hi = ((rm_val >> 16) & 0xFFFF);
            const s16 rn_lo = (rn_val & 0xFFFF);
            const s16 rn_hi = ((rn_val >> 16) & 0xFFFF);

            const u32 product1 = (rn_lo * rm_lo);
            const u32 product2 = (rn_hi * rm_hi);

            // SMUAD and SMLAD
            if (BIT(op2, 1) == 0) {
                RD = (product1 + product2);

                if (inst_cream->Ra != 15) {
                    RD += cpu->Reg[inst_cream->Ra];

                    if (ARMul_AddOverflowQ(product1 + product2, cpu->Reg[inst_cream->Ra]))
                        cpu->Cpsr |= (1 << 27);
                }

                if (ARMul_AddOverflowQ(product1, product2))
                    cpu->Cpsr |= (1 << 27);
            }
            // SMUSD and SMLSD
            else {
                RD = (product1 - product2);

                if (inst_cream->Ra != 15) {
                    RD += cpu->Reg[inst_cream->Ra];

                    if (ARMul_AddOverflowQ(product1 - product2, cpu->Reg[inst_cream->Ra]))
                        cpu->Cpsr |= (1 << 27);
                }
            }
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(smlad_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SMLAL_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            umlal_inst* inst_cream = (umlal_inst*)inst_base->component;
            long long int rm = RM;
            long long int rs = RS;
            if (BIT(rm, 31)) {
                rm |= 0xffffffff00000000LL;
            }
            if (BIT(rs, 31)) {
                rs |= 0xffffffff00000000LL;
            }
            long long int rst = rm * rs;
            long long int rdhi32 = RDHI;
            long long int hilo = (rdhi32 << 32) + RDLO;
            rst += hilo;
            RDLO = BITS(rst,  0, 31);
            RDHI = BITS(rst, 32, 63);
            if (inst_cream->S) {
                cpu->NFlag = BIT(RDHI, 31);
                cpu->ZFlag = (RDHI == 0 && RDLO == 0);
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(umlal_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SMLALXY_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            smlalxy_inst* const inst_cream = (smlalxy_inst*)inst_base->component;

            u64 operand1 = RN;
            u64 operand2 = RM;

            if (inst_cream->x != 0)
                operand1 >>= 16;
            if (inst_cream->y != 0)
                operand2 >>= 16;
            operand1 &= 0xFFFF;
            if (operand1 & 0x8000)
                operand1 -= 65536;
            operand2 &= 0xFFFF;
            if (operand2 & 0x8000)
                operand2 -= 65536;

            u64 dest = ((u64)RDHI << 32 | RDLO) + (operand1 * operand2);
            RDLO = (dest & 0xFFFFFFFF);
            RDHI = ((dest >> 32) & 0xFFFFFFFF);
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(smlalxy_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SMLAW_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            smlad_inst* const inst_cream = (smlad_inst*)inst_base->component;

            const u32 rm_val = RM;
            const u32 rn_val = RN;
            const u32 ra_val = cpu->Reg[inst_cream->Ra];
            const bool high = (inst_cream->m == 1);

            const s16 operand2 = (high) ? ((rm_val >> 16) & 0xFFFF) : (rm_val & 0xFFFF);
            const s64 result = (s64)(s32)rn_val * (s64)(s32)operand2 + ((s64)(s32)ra_val << 16);

            RD = (result & (0xFFFFFFFFFFFFFFFFLL >> 15)) >> 16;

            if ((result >> 16) != (s32)RD)
                cpu->Cpsr |= (1 << 27);
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(smlad_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SMLALD_INST:
    SMLSLD_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            smlald_inst* const inst_cream = (smlald_inst*)inst_base->component;

            const bool do_swap = (inst_cream->swap == 1);
            const u32 rdlo_val = RDLO;
            const u32 rdhi_val = RDHI;
            const u32 rn_val   = RN;
            u32 rm_val         = RM;

            if (do_swap)
                rm_val = (((rm_val & 0xFFFF) << 16) | (rm_val >> 16));
            
            const s32 product1 = (s16)(rn_val & 0xFFFF) * (s16)(rm_val & 0xFFFF);
            const s32 product2 = (s16)((rn_val >> 16) & 0xFFFF) * (s16)((rm_val >> 16) & 0xFFFF);
            s64 result;

            // SMLALD
            if (BIT(inst_cream->op2, 1) == 0) {
                result = (product1 + product2) + (s64)(rdlo_val | ((s64)rdhi_val << 32));
            }
            // SMLSLD
            else {
                result = (product1 - product2) + (s64)(rdlo_val | ((s64)rdhi_val << 32));
            }

            RDLO = (result & 0xFFFFFFFF);
            RDHI = ((result >> 32) & 0xFFFFFFFF);
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(smlald_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SMMLA_INST:
    SMMLS_INST:
    SMMUL_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            smlad_inst* const inst_cream = (smlad_inst*)inst_base->component;

            const u32 rm_val = RM;
            const u32 rn_val = RN;
            const bool do_round = (inst_cream->m == 1);

            // Assume SMMUL by default.
            s64 result = (s64)(s32)rn_val * (s64)(s32)rm_val;

            if (inst_cream->Ra != 15) {
                const u32 ra_val = cpu->Reg[inst_cream->Ra];

                // SMMLA, otherwise SMMLS
                if (BIT(inst_cream->op2, 1) == 0)
                    result += ((s64)ra_val << 32);
                else
                    result = ((s64)ra_val << 32) - result;
            }

            if (do_round)
                result += 0x80000000;

            RD = ((result >> 32) & 0xFFFFFFFF);
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(smlad_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SMUL_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            smul_inst* inst_cream = (smul_inst*)inst_base->component;
            uint32_t operand1, operand2;
            if (inst_cream->x == 0)
                operand1 = (BIT(RM, 15)) ? (BITS(RM, 0, 15) | 0xffff0000) : BITS(RM, 0, 15);
            else
                operand1 = (BIT(RM, 31)) ? (BITS(RM, 16, 31) | 0xffff0000) : BITS(RM, 16, 31);

            if (inst_cream->y == 0)
                operand2 = (BIT(RS, 15)) ? (BITS(RS, 0, 15) | 0xffff0000) : BITS(RS, 0, 15);
            else
                operand2 = (BIT(RS, 31)) ? (BITS(RS, 16, 31) | 0xffff0000) : BITS(RS, 16, 31);
            RD = operand1 * operand2;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(smul_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    SMULL_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            umull_inst* inst_cream = (umull_inst*)inst_base->component;
            int64_t rm = RM;
            int64_t rs = RS;
            if (BIT(rm, 31)) {
                rm |= 0xffffffff00000000LL;
            }
            if (BIT(rs, 31)) {
                rs |= 0xffffffff00000000LL;
            }
            int64_t rst = rm * rs;
            RDHI = BITS(rst, 32, 63);
            RDLO = BITS(rst,  0, 31);

            if (inst_cream->S) {
                cpu->NFlag = BIT(RDHI, 31);
                cpu->ZFlag = (RDHI == 0 && RDLO == 0);
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(umull_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SMULW_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            smlad_inst* const inst_cream = (smlad_inst*)inst_base->component;

            s16 rm = (inst_cream->m == 1) ? ((RM >> 16) & 0xFFFF) : (RM & 0xFFFF);

            s64 result = (s64)rm * (s64)(s32)RN;
            RD = BITS(result, 16, 47);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(smlad_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SRS_INST:

    SSAT_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ssat_inst* const inst_cream = (ssat_inst*)inst_base->component;

            u8 shift_type = inst_cream->shift_type;
            u8 shift_amount = inst_cream->imm5;
            u32 rn_val = RN;

            // 32-bit ASR is encoded as an amount of 0.
            if (shift_type == 1 && shift_amount == 0)
                shift_amount = 31;

            if (shift_type == 0)
                rn_val <<= shift_amount;
            else if (shift_type == 1)
                rn_val = ((s32)rn_val >> shift_amount);

            bool saturated = false;
            rn_val = ARMul_SignedSatQ(rn_val, inst_cream->sat_imm, &saturated);

            if (saturated)
                cpu->Cpsr |= (1 << 27);

            RD = rn_val;
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ssat_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SSAT16_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ssat_inst* const inst_cream = (ssat_inst*)inst_base->component;
            const u8 saturate_to = inst_cream->sat_imm;

            bool sat1 = false;
            bool sat2 = false;

            RD = (ARMul_SignedSatQ((s16)RN, saturate_to, &sat1) & 0xFFFF) |
                 ARMul_SignedSatQ((s32)RN >> 16, saturate_to, &sat2) << 16;

            if (sat1 || sat2)
                cpu->Cpsr |= (1 << 27);
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ssat_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    STC_INST:
    {
        // Instruction not implemented
        //LOG_CRITICAL(Core_ARM11, "unimplemented instruction");
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(stc_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STM_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            unsigned int inst = inst_cream->inst;

            int i;
            unsigned int Rn = BITS(inst, 16, 19);
            unsigned int old_RN = cpu->Reg[Rn];

            inst_cream->get_addr(cpu, inst_cream->inst, addr, 0);
            if (BIT(inst_cream->inst, 22) == 1) {
                for (i = 0; i < 13; i++) {
                    if(BIT(inst_cream->inst, i)) {
                        Memory::Write32(addr, cpu->Reg[i]);
                        addr += 4;
                    }
                }
                if (BIT(inst_cream->inst, 13)) {
                    if (cpu->Mode == USER32MODE) {
                        Memory::Write32(addr, cpu->Reg[i]);
                        addr += 4;
                    } else {
                        Memory::Write32(addr, cpu->Reg_usr[0]);
                        addr += 4;
                    }
                }
                if (BIT(inst_cream->inst, 14)) {
                    if (cpu->Mode == USER32MODE) {
                        Memory::Write32(addr, cpu->Reg[i]);
                        addr += 4;
                    } else {
                        Memory::Write32(addr, cpu->Reg_usr[1]);
                        addr += 4;
                    }
                }
                if (BIT(inst_cream->inst, 15)) {
                    Memory::Write32(addr, cpu->Reg_usr[1] + 8);
                }
            } else {
                for( i = 0; i < 15; i++ ) {
                    if(BIT(inst_cream->inst, i)) {
                        if(i == Rn)
                            Memory::Write32(addr, old_RN);
                        else
                            Memory::Write32(addr, cpu->Reg[i]);
                        addr += 4;
                    }
                }

                // Check PC reg
                if(BIT(inst_cream->inst, i)) {
                    Memory::Write32(addr, cpu->Reg_usr[1] + 8);
                }
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    SXTB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            sxtb_inst* inst_cream = (sxtb_inst*)inst_base->component;

            if (inst_cream->Rm == 15) {
                LOG_ERROR(Core_ARM11, "invalid operand for SXTB");
                CITRA_IGNORE_EXIT(-1);
            }
            unsigned int operand2 = ROTATE_RIGHT_32(RM, 8 * inst_cream->rotate);
            if (BIT(operand2, 7)) {
                operand2 |= 0xffffff00;
            } else
                operand2 &= 0xff;
            RD = operand2;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(sxtb_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STR_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 0);

            unsigned int value = cpu->Reg[BITS(inst_cream->inst, 12, 15)];
            Memory::Write32(addr, value);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    UXTB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            uxtb_inst* inst_cream = (uxtb_inst*)inst_base->component;
            RD = ROTATE_RIGHT_32(RM, 8 * inst_cream->rotate) & 0xff;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(uxtb_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    UXTAB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            uxtab_inst* inst_cream = (uxtab_inst*)inst_base->component;

            unsigned int operand2 = ROTATE_RIGHT_32(RM, 8 * inst_cream->rotate) & 0xff;
            RD = RN + operand2;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(uxtab_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STRB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 0);
            unsigned int value = cpu->Reg[BITS(inst_cream->inst, 12, 15)] & 0xff;
            Memory::Write8(addr, value);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STRBT_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 0);
            unsigned int value = cpu->Reg[BITS(inst_cream->inst, 12, 15)] & 0xff;
            Memory::Write8(addr, value);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STRD_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 0);

            unsigned int value = cpu->Reg[BITS(inst_cream->inst, 12, 15)];
            Memory::Write32(addr, value);
            value = cpu->Reg[BITS(inst_cream->inst, 12, 15) + 1];
            Memory::Write32(addr + 4, value);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STREX_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* inst_cream = (generic_arm_inst*)inst_base->component;
            unsigned int write_addr = cpu->Reg[inst_cream->Rn];

            if ((exclusive_detect(cpu, write_addr) == 0) && (cpu->exclusive_state == 1)) {
                remove_exclusive(cpu, write_addr);
                cpu->exclusive_state = 0;

                Memory::Write32(write_addr, cpu->Reg[inst_cream->Rm]);
                RD = 0;
            } else {
                // Failed to write due to mutex access
                RD = 1;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STREXB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* inst_cream = (generic_arm_inst*)inst_base->component;
            unsigned int write_addr = cpu->Reg[inst_cream->Rn];

            if ((exclusive_detect(cpu, write_addr) == 0) && (cpu->exclusive_state == 1)) {
                remove_exclusive(cpu, write_addr);
                cpu->exclusive_state = 0;

                Memory::Write8(write_addr, cpu->Reg[inst_cream->Rm]);
                RD = 0;
            } else {
                // Failed to write due to mutex access
                RD = 1;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STREXD_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* inst_cream = (generic_arm_inst*)inst_base->component;
            unsigned int write_addr = cpu->Reg[inst_cream->Rn];

            if ((exclusive_detect(cpu, write_addr) == 0) && (cpu->exclusive_state == 1)) {
                remove_exclusive(cpu, write_addr);
                cpu->exclusive_state = 0;

                Memory::Write32(write_addr, cpu->Reg[inst_cream->Rm]);
                Memory::Write32(write_addr + 4, cpu->Reg[inst_cream->Rm + 1]);
                RD = 0;
            }
            else {
                // Failed to write due to mutex access
                RD = 1;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STREXH_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* inst_cream = (generic_arm_inst*)inst_base->component;
            unsigned int write_addr = cpu->Reg[inst_cream->Rn];

            if ((exclusive_detect(cpu, write_addr) == 0) && (cpu->exclusive_state == 1)) {
                remove_exclusive(cpu, write_addr);
                cpu->exclusive_state = 0;

                Memory::Write16(write_addr, cpu->Reg[inst_cream->Rm]);
                RD = 0;
            } else {
                // Failed to write due to mutex access
                RD = 1;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STRH_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 0);

            unsigned int value = cpu->Reg[BITS(inst_cream->inst, 12, 15)] & 0xffff;
            Memory::Write16(addr, value);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    STRT_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ldst_inst* inst_cream = (ldst_inst*)inst_base->component;
            inst_cream->get_addr(cpu, inst_cream->inst, addr, 0);

            unsigned int value = cpu->Reg[BITS(inst_cream->inst, 12, 15)];
            Memory::Write32(addr, value);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ldst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    SUB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            sub_inst* const inst_cream = (sub_inst*)inst_base->component;

            u32 rn_val = RN;
            if (inst_cream->Rn == 15)
                rn_val += 8;

            bool carry;
            bool overflow;
            RD = AddWithCarry(rn_val, ~SHIFTER_OPERAND, 1, &carry, &overflow);

            if (inst_cream->S && (inst_cream->Rd == 15)) {
                if (CurrentModeHasSPSR) {
                    cpu->Cpsr = cpu->Spsr_copy;
                    switch_mode(cpu, cpu->Spsr_copy & 0x1f);
                    LOAD_NZCVT;
                }
            } else if (inst_cream->S) {
                UPDATE_NFLAG(RD);
                UPDATE_ZFLAG(RD);
                cpu->CFlag = carry;
                cpu->VFlag = overflow;
            }
            if (inst_cream->Rd == 15) {
                INC_PC(sizeof(sub_inst));
                goto DISPATCH;
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(sub_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    SWI_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            HLE::CallSVC(Memory::Read32(cpu->Reg[15]));
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(swi_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    SWP_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            swp_inst* inst_cream = (swp_inst*)inst_base->component;

            addr = RN;
            unsigned int value;
            value = Memory::Read32(addr);
            Memory::Write32(addr, RM);

            RD = value;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(swp_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    SWPB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            swp_inst* inst_cream = (swp_inst*)inst_base->component;
            addr = RN;
            unsigned int value = Memory::Read8(addr);
            Memory::Write8(addr, (RM & 0xFF));
            RD = value;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(swp_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    SXTAB_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            sxtab_inst* inst_cream = (sxtab_inst*)inst_base->component;

            // R15 should be check
            if(inst_cream->Rn == 15 || inst_cream->Rm == 15 || inst_cream->Rd ==15){
                CITRA_IGNORE_EXIT(-1);
            }
            unsigned int operand2 = ROTATE_RIGHT_32(RM, 8 * inst_cream->rotate) & 0xff;

            // Sign extend for byte
            operand2 = (0x80 & operand2)? (0xFFFFFF00 | operand2):operand2;
            RD = RN + operand2;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(uxtab_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SXTAB16_INST:
    SXTB16_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            sxtab_inst* const inst_cream = (sxtab_inst*)inst_base->component;

            const u8 rotation = inst_cream->rotate * 8;
            u32 rm_val = RM;
            u32 rn_val = RN;

            if (rotation)
                rm_val = ((rm_val << (32 - rotation)) | (rm_val >> rotation));

            // SXTB16
            if (inst_cream->Rn == 15) {
                u32 lo = (u32)(s8)rm_val;
                u32 hi = (u32)(s8)(rm_val >> 16);
                RD = (lo | (hi << 16));
            }
            // SXTAB16
            else {
                u32 lo = (rn_val & 0xFFFF) + (u32)(s8)(rm_val & 0xFF);
                u32 hi = ((rn_val >> 16) & 0xFFFF) + (u32)(s8)((rm_val >> 16) & 0xFF);
                RD = (lo | (hi << 16));
            }
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(sxtab_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    SXTAH_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            sxtah_inst* inst_cream = (sxtah_inst*)inst_base->component;

            // R15 should be check
            if(inst_cream->Rn == 15 || inst_cream->Rm == 15 || inst_cream->Rd ==15) {
                CITRA_IGNORE_EXIT(-1);
            }
            unsigned int operand2 = ROTATE_RIGHT_32(RM, 8 * inst_cream->rotate) & 0xffff;
            // Sign extend for half
            operand2 = (0x8000 & operand2) ? (0xFFFF0000 | operand2) : operand2;
            RD = RN + operand2;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(sxtah_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    TEQ_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            teq_inst* const inst_cream = (teq_inst*)inst_base->component;

            u32 lop = RN;
            u32 rop = SHIFTER_OPERAND;

            if (inst_cream->Rn == 15)
                lop += GET_INST_SIZE(cpu) * 2;

            u32 result = lop ^ rop;

            UPDATE_NFLAG(result);
            UPDATE_ZFLAG(result);
            UPDATE_CFLAG_WITH_SC;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(teq_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    TST_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            tst_inst* const inst_cream = (tst_inst*)inst_base->component;

            u32 lop = RN;
            u32 rop = SHIFTER_OPERAND;

            if (inst_cream->Rn == 15)
                lop += GET_INST_SIZE(cpu) * 2;

            u32 result = lop & rop;

            UPDATE_NFLAG(result);
            UPDATE_ZFLAG(result);
            UPDATE_CFLAG_WITH_SC;
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(tst_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    UADD8_INST:
    UADD16_INST:
    UADDSUBX_INST:
    USUB8_INST:
    USUB16_INST:
    USUBADDX_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

            const u8 op2 = inst_cream->op2;
            const u32 rm_val = RM;
            const u32 rn_val = RN;

            s32 lo_result = 0;
            s32 hi_result = 0;

            // UADD16
            if (op2 == 0x00) {
                lo_result = (rn_val & 0xFFFF) + (rm_val & 0xFFFF);
                hi_result = ((rn_val >> 16) & 0xFFFF) + ((rm_val >> 16) & 0xFFFF);

                if (lo_result & 0xFFFF0000) {
                    cpu->Cpsr |= (1 << 16);
                    cpu->Cpsr |= (1 << 17);
                } else {
                    cpu->Cpsr &= ~(1 << 16);
                    cpu->Cpsr &= ~(1 << 17);
                }

                if (hi_result & 0xFFFF0000) {
                    cpu->Cpsr |= (1 << 18);
                    cpu->Cpsr |= (1 << 19);
                } else {
                    cpu->Cpsr &= ~(1 << 18);
                    cpu->Cpsr &= ~(1 << 19);
                }
            }
            // UASX
            else if (op2 == 0x01) {
                lo_result = (rn_val & 0xFFFF) - ((rm_val >> 16) & 0xFFFF);
                hi_result = ((rn_val >> 16) & 0xFFFF) + (rm_val & 0xFFFF);

                if (lo_result >= 0) {
                    cpu->Cpsr |= (1 << 16);
                    cpu->Cpsr |= (1 << 17);
                } else {
                    cpu->Cpsr &= ~(1 << 16);
                    cpu->Cpsr &= ~(1 << 17);
                }

                if (hi_result >= 0x10000) {
                    cpu->Cpsr |= (1 << 18);
                    cpu->Cpsr |= (1 << 19);
                } else {
                    cpu->Cpsr &= ~(1 << 18);
                    cpu->Cpsr &= ~(1 << 19);
                }
            }
            // USAX
            else if (op2 == 0x02) {
                lo_result = (rn_val & 0xFFFF) + ((rm_val >> 16) & 0xFFFF);
                hi_result = ((rn_val >> 16) & 0xFFFF) - (rm_val & 0xFFFF);

                if (lo_result >= 0x10000) {
                    cpu->Cpsr |= (1 << 16);
                    cpu->Cpsr |= (1 << 17);
                } else {
                    cpu->Cpsr &= ~(1 << 16);
                    cpu->Cpsr &= ~(1 << 17);
                }

                if (hi_result >= 0) {
                    cpu->Cpsr |= (1 << 18);
                    cpu->Cpsr |= (1 << 19);
                } else {
                    cpu->Cpsr &= ~(1 << 18);
                    cpu->Cpsr &= ~(1 << 19);
                }
            }
            // USUB16
            else if (op2 == 0x03) {
                lo_result = (rn_val & 0xFFFF) - (rm_val & 0xFFFF);
                hi_result = ((rn_val >> 16) & 0xFFFF) - ((rm_val >> 16) & 0xFFFF);

                if ((lo_result & 0xFFFF0000) == 0) {
                    cpu->Cpsr |= (1 << 16);
                    cpu->Cpsr |= (1 << 17);
                } else {
                    cpu->Cpsr &= ~(1 << 16);
                    cpu->Cpsr &= ~(1 << 17);
                }

                if ((hi_result & 0xFFFF0000) == 0) {
                    cpu->Cpsr |= (1 << 18);
                    cpu->Cpsr |= (1 << 19);
                } else {
                    cpu->Cpsr &= ~(1 << 18);
                    cpu->Cpsr &= ~(1 << 19);
                }
            }
            // UADD8
            else if (op2 == 0x04) {
                s16 sum1 = (rn_val & 0xFF) + (rm_val & 0xFF);
                s16 sum2 = ((rn_val >> 8) & 0xFF) + ((rm_val >> 8) & 0xFF);
                s16 sum3 = ((rn_val >> 16) & 0xFF) + ((rm_val >> 16) & 0xFF);
                s16 sum4 = ((rn_val >> 24) & 0xFF) + ((rm_val >> 24) & 0xFF);

                if (sum1 >= 0x100)
                    state->Cpsr |= (1 << 16);
                else
                    state->Cpsr &= ~(1 << 16);

                if (sum2 >= 0x100)
                    state->Cpsr |= (1 << 17);
                else
                    state->Cpsr &= ~(1 << 17);

                if (sum3 >= 0x100)
                    state->Cpsr |= (1 << 18);
                else
                    state->Cpsr &= ~(1 << 18);

                if (sum4 >= 0x100)
                    state->Cpsr |= (1 << 19);
                else
                    state->Cpsr &= ~(1 << 19);

                lo_result = ((sum1 & 0xFF) | (sum2 & 0xFF) << 8);
                hi_result = ((sum3 & 0xFF) | (sum4 & 0xFF) << 8);
            }
            // USUB8
            else if (op2 == 0x07) {
                s16 diff1 = (rn_val & 0xFF) - (rm_val & 0xFF);
                s16 diff2 = ((rn_val >> 8) & 0xFF) - ((rm_val >> 8) & 0xFF);
                s16 diff3 = ((rn_val >> 16) & 0xFF) - ((rm_val >> 16) & 0xFF);
                s16 diff4 = ((rn_val >> 24) & 0xFF) - ((rm_val >> 24) & 0xFF);

                if (diff1 >= 0)
                    state->Cpsr |= (1 << 16);
                else
                    state->Cpsr &= ~(1 << 16);

                if (diff2 >= 0)
                    state->Cpsr |= (1 << 17);
                else
                    state->Cpsr &= ~(1 << 17);

                if (diff3 >= 0)
                    state->Cpsr |= (1 << 18);
                else
                    state->Cpsr &= ~(1 << 18);

                if (diff4 >= 0)
                    state->Cpsr |= (1 << 19);
                else
                    state->Cpsr &= ~(1 << 19);

                lo_result = (diff1 & 0xFF) | ((diff2 & 0xFF) << 8);
                hi_result = (diff3 & 0xFF) | ((diff4 & 0xFF) << 8);
            }

            RD = (lo_result & 0xFFFF) | ((hi_result & 0xFFFF) << 16);
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    UHADD8_INST:
    UHADD16_INST:
    UHADDSUBX_INST:
    UHSUBADDX_INST:
    UHSUB8_INST:
    UHSUB16_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;
            const u32 rm_val = RM;
            const u32 rn_val = RN;
            const u8 op2 = inst_cream->op2;

            if (op2 == 0x00 || op2 == 0x01 || op2 == 0x02 || op2 == 0x03)
            {
                u32 lo_val = 0;
                u32 hi_val = 0;
                
                // UHADD16
                if (op2 == 0x00) {
                    lo_val = (rn_val & 0xFFFF) + (rm_val & 0xFFFF);
                    hi_val = ((rn_val >> 16) & 0xFFFF) + ((rm_val >> 16) & 0xFFFF);
                }
                // UHASX
                else if (op2 == 0x01) {
                    lo_val = (rn_val & 0xFFFF) - ((rm_val >> 16) & 0xFFFF);
                    hi_val = ((rn_val >> 16) & 0xFFFF) + (rm_val & 0xFFFF);
                }
                // UHSAX
                else if (op2 == 0x02) {
                    lo_val = (rn_val & 0xFFFF) + ((rm_val >> 16) & 0xFFFF);
                    hi_val = ((rn_val >> 16) & 0xFFFF) - (rm_val & 0xFFFF);
                }
                // UHSUB16
                else if (op2 == 0x03) {
                    lo_val = (rn_val & 0xFFFF) - (rm_val & 0xFFFF);
                    hi_val = ((rn_val >> 16) & 0xFFFF) - ((rm_val >> 16) & 0xFFFF);
                }

                lo_val >>= 1;
                hi_val >>= 1;

                RD = (lo_val & 0xFFFF) | ((hi_val & 0xFFFF) << 16);
            }
            else if (op2 == 0x04 || op2 == 0x07) {
                u32 sum1;
                u32 sum2;
                u32 sum3;
                u32 sum4;

                // UHADD8
                if (op2 == 0x04) {
                    sum1 = (rn_val & 0xFF) + (rm_val & 0xFF);
                    sum2 = ((rn_val >> 8) & 0xFF) + ((rm_val >> 8) & 0xFF);
                    sum3 = ((rn_val >> 16) & 0xFF) + ((rm_val >> 16) & 0xFF);
                    sum4 = ((rn_val >> 24) & 0xFF) + ((rm_val >> 24) & 0xFF);
                }
                // UHSUB8
                else {
                    sum1 = (rn_val & 0xFF) - (rm_val & 0xFF);
                    sum2 = ((rn_val >> 8) & 0xFF) - ((rm_val >> 8) & 0xFF);
                    sum3 = ((rn_val >> 16) & 0xFF) - ((rm_val >> 16) & 0xFF);
                    sum4 = ((rn_val >> 24) & 0xFF) - ((rm_val >> 24) & 0xFF);
                }

                sum1 >>= 1;
                sum2 >>= 1;
                sum3 >>= 1;
                sum4 >>= 1;

                RD = (sum1 & 0xFF) | ((sum2 & 0xFF) << 8) | ((sum3 & 0xFF) << 16) | ((sum4 & 0xFF) << 24);
            }
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    UMAAL_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            umaal_inst* const inst_cream = (umaal_inst*)inst_base->component;
            const u64 rm = RM;
            const u64 rn = RN;
            const u64 rd_lo = RDLO;
            const u64 rd_hi = RDHI;
            const u64 result = (rm * rn) + rd_lo + rd_hi;

            RDLO = (result & 0xFFFFFFFF);
            RDHI = ((result >> 32) & 0xFFFFFFFF);
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(umaal_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    UMLAL_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            umlal_inst* inst_cream = (umlal_inst*)inst_base->component;
            unsigned long long int rm = RM;
            unsigned long long int rs = RS;
            unsigned long long int rst = rm * rs;
            unsigned long long int add = ((unsigned long long) RDHI)<<32;
            add += RDLO;
            rst += add;
            RDLO = BITS(rst,  0, 31);
            RDHI = BITS(rst, 32, 63);

            if (inst_cream->S) {
                cpu->NFlag = BIT(RDHI, 31);
                cpu->ZFlag = (RDHI == 0 && RDLO == 0);
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(umlal_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    UMULL_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            umull_inst* inst_cream = (umull_inst*)inst_base->component;
            unsigned long long int rm = RM;
            unsigned long long int rs = RS;
            unsigned long long int rst = rm * rs;
            RDHI = BITS(rst, 32, 63);
            RDLO = BITS(rst,  0, 31);

            if (inst_cream->S) {
                cpu->NFlag = BIT(RDHI, 31);
                cpu->ZFlag = (RDHI == 0 && RDLO == 0);
            }
        }
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(umull_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    B_2_THUMB:
    {
        b_2_thumb* inst_cream = (b_2_thumb*)inst_base->component;
        cpu->Reg[15] = cpu->Reg[15] + 4 + inst_cream->imm;
        INC_PC(sizeof(b_2_thumb));
        goto DISPATCH;
    }
    B_COND_THUMB:
    {
        b_cond_thumb* inst_cream = (b_cond_thumb*)inst_base->component;

        if(CondPassed(cpu, inst_cream->cond))
            cpu->Reg[15] = cpu->Reg[15] + 4 + inst_cream->imm;
        else
            cpu->Reg[15] += 2;

        INC_PC(sizeof(b_cond_thumb));
        goto DISPATCH;
    }
    BL_1_THUMB:
    {
        bl_1_thumb* inst_cream = (bl_1_thumb*)inst_base->component;
        cpu->Reg[14] = cpu->Reg[15] + 4 + inst_cream->imm;
        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(bl_1_thumb));
        FETCH_INST;
        GOTO_NEXT_INST;
    }
    BL_2_THUMB:
    {
        bl_2_thumb* inst_cream = (bl_2_thumb*)inst_base->component;
        int tmp = ((cpu->Reg[15] + 2) | 1);
        cpu->Reg[15] = (cpu->Reg[14] + inst_cream->imm);
        cpu->Reg[14] = tmp;
        INC_PC(sizeof(bl_2_thumb));
        goto DISPATCH;
    }
    BLX_1_THUMB:
    {
        // BLX 1 for armv5t and above
        u32 tmp = cpu->Reg[15];
        blx_1_thumb* inst_cream = (blx_1_thumb*)inst_base->component;
        cpu->Reg[15] = (cpu->Reg[14] + inst_cream->imm) & 0xFFFFFFFC;
        cpu->Reg[14] = ((tmp + 2) | 1);
        cpu->TFlag = 0;
        INC_PC(sizeof(blx_1_thumb));
        goto DISPATCH;
    }

    UQADD8_INST:
    UQADD16_INST:
    UQADDSUBX_INST:
    UQSUB8_INST:
    UQSUB16_INST:
    UQSUBADDX_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* const inst_cream = (generic_arm_inst*)inst_base->component;

            const u8 op2 = inst_cream->op2;
            const u32 rm_val = RM;
            const u32 rn_val = RN;

            u16 lo_val = 0;
            u16 hi_val = 0;
            
            // UQADD16
            if (op2 == 0x00) {
                lo_val = ARMul_UnsignedSaturatedAdd16(rn_val & 0xFFFF, rm_val & 0xFFFF);
                hi_val = ARMul_UnsignedSaturatedAdd16((rn_val >> 16) & 0xFFFF, (rm_val >> 16) & 0xFFFF);
            }
            // UQASX
            else if (op2 == 0x01) {
                lo_val = ARMul_UnsignedSaturatedSub16(rn_val & 0xFFFF, (rm_val >> 16) & 0xFFFF);
                hi_val = ARMul_UnsignedSaturatedAdd16((rn_val >> 16) & 0xFFFF, rm_val & 0xFFFF);
            }
            // UQSAX
            else if (op2 == 0x02) {
                lo_val = ARMul_UnsignedSaturatedAdd16(rn_val & 0xFFFF, (rm_val >> 16) & 0xFFFF);
                hi_val = ARMul_UnsignedSaturatedSub16((rn_val >> 16) & 0xFFFF, rm_val & 0xFFFF);
            }
            // UQSUB16
            else if (op2 == 0x03) {
                lo_val = ARMul_UnsignedSaturatedSub16(rn_val & 0xFFFF, rm_val & 0xFFFF);
                hi_val = ARMul_UnsignedSaturatedSub16((rn_val >> 16) & 0xFFFF, (rm_val >> 16) & 0xFFFF);
            }
            // UQADD8
            else if (op2 == 0x04) {
                lo_val = ARMul_UnsignedSaturatedAdd8(rn_val, rm_val) |
                         ARMul_UnsignedSaturatedAdd8(rn_val >> 8,  rm_val >> 8) << 8;
                hi_val = ARMul_UnsignedSaturatedAdd8(rn_val >> 16, rm_val >> 16) |
                         ARMul_UnsignedSaturatedAdd8(rn_val >> 24, rm_val >> 24) << 8;
            }
            // UQSUB8
            else {
                lo_val = ARMul_UnsignedSaturatedSub8(rn_val, rm_val) |
                         ARMul_UnsignedSaturatedSub8(rn_val >> 8,  rm_val >> 8) << 8;
                hi_val = ARMul_UnsignedSaturatedSub8(rn_val >> 16, rm_val >> 16) |
                         ARMul_UnsignedSaturatedSub8(rn_val >> 24, rm_val >> 24) << 8;
            }

            RD = ((lo_val & 0xFFFF) | hi_val << 16);
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    USAD8_INST:
    USADA8_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            generic_arm_inst* inst_cream = (generic_arm_inst*)inst_base->component;

            const u8 ra_idx = inst_cream->Ra;
            const u32 rm_val = RM;
            const u32 rn_val = RN;

            const u8 diff1 = ARMul_UnsignedAbsoluteDifference(rn_val & 0xFF, rm_val & 0xFF);
            const u8 diff2 = ARMul_UnsignedAbsoluteDifference((rn_val >> 8) & 0xFF, (rm_val >> 8) & 0xFF);
            const u8 diff3 = ARMul_UnsignedAbsoluteDifference((rn_val >> 16) & 0xFF, (rm_val >> 16) & 0xFF);
            const u8 diff4 = ARMul_UnsignedAbsoluteDifference((rn_val >> 24) & 0xFF, (rm_val >> 24) & 0xFF);

            u32 finalDif = (diff1 + diff2 + diff3 + diff4);

            // Op is USADA8 if true.
            if (ra_idx != 15)
                finalDif += cpu->Reg[ra_idx];

            RD = finalDif;
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(generic_arm_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    USAT_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ssat_inst* const inst_cream = (ssat_inst*)inst_base->component;

            u8 shift_type = inst_cream->shift_type;
            u8 shift_amount = inst_cream->imm5;
            u32 rn_val = RN;

            // 32-bit ASR is encoded as an amount of 0.
            if (shift_type == 1 && shift_amount == 0)
                shift_amount = 31;

            if (shift_type == 0)
                rn_val <<= shift_amount;
            else if (shift_type == 1)
                rn_val = ((s32)rn_val >> shift_amount);

            bool saturated = false;
            rn_val = ARMul_UnsignedSatQ(rn_val, inst_cream->sat_imm, &saturated);

            if (saturated)
                cpu->Cpsr |= (1 << 27);

            RD = rn_val;
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ssat_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    USAT16_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            ssat_inst* const inst_cream = (ssat_inst*)inst_base->component;
            const u8 saturate_to = inst_cream->sat_imm;

            bool sat1 = false;
            bool sat2 = false;

            RD = (ARMul_UnsignedSatQ((s16)RN, saturate_to, &sat1) & 0xFFFF) |
                 ARMul_UnsignedSatQ((s32)RN >> 16, saturate_to, &sat2) << 16;

            if (sat1 || sat2)
                cpu->Cpsr |= (1 << 27);
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(ssat_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    UXTAB16_INST:
    UXTB16_INST:
    {
        if (inst_base->cond == 0xE || CondPassed(cpu, inst_base->cond)) {
            uxtab_inst* const inst_cream = (uxtab_inst*)inst_base->component;

            const u8 rn_idx = inst_cream->Rn;
            const u32 rm_val = RM;
            const u32 rotation = inst_cream->rotate * 8;
            const u32 rotated_rm = ((rm_val << (32 - rotation)) | (rm_val >> rotation));

            // UXTB16, otherwise UXTAB16
            if (rn_idx == 15) {
                RD = rotated_rm & 0x00FF00FF;
            } else {
                const u32 rn_val = RN;
                const u8 lo_rotated = (rotated_rm & 0xFF);
                const u16 lo_result = (rn_val & 0xFFFF) + (u16)lo_rotated;
                const u8 hi_rotated = (rotated_rm >> 16) & 0xFF;
                const u16 hi_result = (rn_val >> 16) + (u16)hi_rotated;

                RD = ((hi_result << 16) | (lo_result & 0xFFFF));
            }
        }

        cpu->Reg[15] += GET_INST_SIZE(cpu);
        INC_PC(sizeof(uxtab_inst));
        FETCH_INST;
        GOTO_NEXT_INST;
    }

    #define VFP_INTERPRETER_IMPL
    #include "core/arm/skyeye_common/vfp/vfpinstr.cpp"
    #undef VFP_INTERPRETER_IMPL

    END:
    {
        SAVE_NZCVT;
        cpu->NumInstrsToExecute = 0;
        return num_instrs;
    }
    INIT_INST_LENGTH:
    {
        cpu->NumInstrsToExecute = 0;
        return num_instrs;
    }
}
