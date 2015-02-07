// Copyright 2012 Michael Kang, 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/arm/skyeye_common/arm_regformat.h"
#include "core/arm/skyeye_common/armdefs.h"
#include "core/arm/dyncom/arm_dyncom_dec.h"

const ISEITEM arm_instruction[] = {
    { "vmla", 4, ARMVFP2, 23, 27, 0x1C, 20, 21, 0x0, 9, 11, 0x5, 4, 4, 0 },
    { "vmls", 7, ARMVFP2, 28, 31, 0xF, 25, 27, 0x1, 23, 23, 1, 11, 11, 0, 8, 9, 0x2, 6, 6, 1, 4, 4, 0 },
    { "vnmla", 4, ARMVFP2, 23, 27, 0x1C, 20, 21, 0x1, 9, 11, 0x5, 4, 4, 0 },
    { "vnmla", 5, ARMVFP2, 23, 27, 0x1C, 20, 21, 0x2, 9, 11, 0x5, 6, 6, 1, 4, 4, 0 },
    { "vnmls", 5, ARMVFP2, 23, 27, 0x1C, 20, 21, 0x1, 9, 11, 0x5, 6, 6, 0, 4, 4, 0 },
    { "vnmul", 5, ARMVFP2, 23, 27, 0x1C, 20, 21, 0x2, 9, 11, 0x5, 6, 6, 1, 4, 4, 0 },
    { "vmul", 5, ARMVFP2, 23, 27, 0x1C, 20, 21, 0x2, 9, 11, 0x5, 6, 6, 0, 4, 4, 0 },
    { "vadd", 5, ARMVFP2, 23, 27, 0x1C, 20, 21, 0x3, 9, 11, 0x5, 6, 6, 0, 4, 4, 0 },
    { "vsub", 5, ARMVFP2, 23, 27, 0x1C, 20, 21, 0x3, 9, 11, 0x5, 6, 6, 1, 4, 4, 0 },
    { "vdiv", 5, ARMVFP2, 23, 27, 0x1D, 20, 21, 0x0, 9, 11, 0x5, 6, 6, 0, 4, 4, 0 },
    { "vmov(i)", 4, ARMVFP3, 23, 27, 0x1D, 20, 21, 0x3, 9, 11, 0x5, 4, 7, 0 },
    { "vmov(r)", 5, ARMVFP3, 23, 27, 0x1D, 16, 21, 0x30, 9, 11, 0x5, 6, 7, 1, 4, 4, 0 },
    { "vabs", 5, ARMVFP2, 23, 27, 0x1D, 16, 21, 0x30, 9, 11, 0x5, 6, 7, 3, 4, 4, 0 },
    { "vneg", 5, ARMVFP2, 23, 27, 0x1D, 17, 21, 0x18, 9, 11, 0x5, 6, 7, 1, 4, 4, 0 },
    { "vsqrt", 5, ARMVFP2, 23, 27, 0x1D, 16, 21, 0x31, 9, 11, 0x5, 6, 7, 3, 4, 4, 0 },
    { "vcmp", 5, ARMVFP2, 23, 27, 0x1D, 16, 21, 0x34, 9, 11, 0x5, 6, 6, 1, 4, 4, 0 },
    { "vcmp2", 5, ARMVFP2, 23, 27, 0x1D, 16, 21, 0x35, 9, 11, 0x5, 0, 6, 0x40 },
    { "vcvt(bds)", 5, ARMVFP2, 23, 27, 0x1D, 16, 21, 0x37, 9, 11, 0x5, 6, 7, 3, 4, 4, 0 },
    { "vcvt(bff)", 6, ARMVFP3, 23, 27, 0x1D, 19, 21, 0x7, 17, 17, 0x1, 9, 11, 5, 6, 6, 1 },
    { "vcvt(bfi)", 5, ARMVFP2, 23, 27, 0x1D, 19, 21, 0x7, 9, 11, 0x5, 6, 6, 1, 4, 4, 0 },
    { "vmovbrs", 3, ARMVFP2, 21, 27, 0x70, 8, 11, 0xA, 0, 6, 0x10 },
    { "vmsr", 2, ARMVFP2, 20, 27, 0xEE, 0, 11, 0xA10 },
    { "vmovbrc", 4, ARMVFP2, 23, 27, 0x1C, 20, 20, 0x0, 8, 11, 0xB, 0, 4, 0x10 },
    { "vmrs", 2, ARMVFP2, 20, 27, 0xEF, 0, 11, 0xA10 },
    { "vmovbcr", 4, ARMVFP2, 24, 27, 0xE, 20, 20, 1, 8, 11, 0xB, 0, 4, 0x10 },
    { "vmovbrrss", 3, ARMVFP2, 21, 27, 0x62, 8, 11, 0xA, 4, 4, 1 },
    { "vmovbrrd", 3, ARMVFP2, 21, 27, 0x62, 6, 11, 0x2C, 4, 4, 1 },
    { "vstr", 3, ARMVFP2, 24, 27, 0xD, 20, 21, 0, 9, 11, 5 },
    { "vpush", 3, ARMVFP2, 23, 27, 0x1A, 16, 21, 0x2D, 9, 11, 5 },
    { "vstm", 3, ARMVFP2, 25, 27, 0x6, 20, 20, 0, 9, 11, 5 },
    { "vpop", 3, ARMVFP2, 23, 27, 0x19, 16, 21, 0x3D, 9, 11, 5 },
    { "vldr", 3, ARMVFP2, 24, 27, 0xD, 20, 21, 1, 9, 11, 5 },
    { "vldm", 3, ARMVFP2, 25, 27, 0x6, 20, 20, 1, 9, 11, 5 },

    { "srs", 4, 6, 25, 31, 0x0000007c, 22, 22, 0x00000001, 16, 20, 0x0000000d, 8, 11, 0x00000005 },
    { "rfe", 4, 6, 25, 31, 0x0000007c, 22, 22, 0x00000000, 20, 20, 0x00000001, 8, 11, 0x0000000a },
    { "bkpt", 2, 3, 20, 31, 0x00000e12, 4, 7, 0x00000007 },
    { "blx", 1, 3, 25, 31, 0x0000007d },
    { "cps", 3, 6, 20, 31, 0x00000f10, 16, 16, 0x00000000, 5, 5, 0x00000000 },
    { "pld", 4, 4, 26, 31, 0x0000003d, 24, 24, 0x00000001, 20, 22, 0x00000005, 12, 15, 0x0000000f },
    { "setend", 2, 6, 16, 31, 0x0000f101, 4, 7, 0x00000000 },
    { "clrex", 1, 6, 0, 31, 0xf57ff01f },
    { "rev16", 2, 6, 16, 27, 0x000006bf, 4, 11, 0x000000fb },
    { "usad8", 3, 6, 20, 27, 0x00000078, 12, 15, 0x0000000f, 4, 7, 0x00000001 },
    { "sxtb", 2, 6, 16, 27, 0x000006af, 4, 7, 0x00000007 },
    { "uxtb", 2, 6, 16, 27, 0x000006ef, 4, 7, 0x00000007 },
    { "sxth", 2, 6, 16, 27, 0x000006bf, 4, 7, 0x00000007 },
    { "sxtb16", 2, 6, 16, 27, 0x0000068f, 4, 7, 0x00000007 },
    { "uxth", 2, 6, 16, 27, 0x000006ff, 4, 7, 0x00000007 },
    { "uxtb16", 2, 6, 16, 27, 0x000006cf, 4, 7, 0x00000007 },
    { "cpy", 2, 6, 20, 27, 0x0000001a, 4, 11, 0x00000000 },
    { "uxtab", 2, 6, 20, 27, 0x0000006e, 4, 9, 0x00000007 },
    { "ssub8", 2, 6, 20, 27, 0x00000061, 4, 7, 0x0000000f },
    { "shsub8", 2, 6, 20, 27, 0x00000063, 4, 7, 0x0000000f },
    { "ssubaddx", 2, 6, 20, 27, 0x00000061, 4, 7, 0x00000005 },
    { "strex", 2, 6, 20, 27, 0x00000018, 4, 7, 0x00000009 },
    { "strexb", 2, 7, 20, 27, 0x0000001c, 4, 7, 0x00000009 },
    { "swp", 2, 0, 20, 27, 0x00000010, 4, 7, 0x00000009 },
    { "swpb", 2, 0, 20, 27, 0x00000014, 4, 7, 0x00000009 },
    { "ssub16", 2, 6, 20, 27, 0x00000061, 4, 7, 0x00000007 },
    { "ssat16", 2, 6, 20, 27, 0x0000006a, 4, 7, 0x00000003 },
    { "shsubaddx", 2, 6, 20, 27, 0x00000063, 4, 7, 0x00000005 },
    { "qsubaddx", 2, 6, 20, 27, 0x00000062, 4, 7, 0x00000005 },
    { "shaddsubx", 2, 6, 20, 27, 0x00000063, 4, 7, 0x00000003 },
    { "shadd8", 2, 6, 20, 27, 0x00000063, 4, 7, 0x00000009 },
    { "shadd16", 2, 6, 20, 27, 0x00000063, 4, 7, 0x00000001 },
    { "sel", 2, 6, 20, 27, 0x00000068, 4, 7, 0x0000000b },
    { "saddsubx", 2, 6, 20, 27, 0x00000061, 4, 7, 0x00000003 },
    { "sadd8", 2, 6, 20, 27, 0x00000061, 4, 7, 0x00000009 },
    { "sadd16", 2, 6, 20, 27, 0x00000061, 4, 7, 0x00000001 },
    { "shsub16", 2, 6, 20, 27, 0x00000063, 4, 7, 0x00000007 },
    { "umaal", 2, 6, 20, 27, 0x00000004, 4, 7, 0x00000009 },
    { "uxtab16", 2, 6, 20, 27, 0x0000006c, 4, 7, 0x00000007 },
    { "usubaddx", 2, 6, 20, 27, 0x00000065, 4, 7, 0x00000005 },
    { "usub8", 2, 6, 20, 27, 0x00000065, 4, 7, 0x0000000f },
    { "usub16", 2, 6, 20, 27, 0x00000065, 4, 7, 0x00000007 },
    { "usat16", 2, 6, 20, 27, 0x0000006e, 4, 7, 0x00000003 },
    { "usada8", 2, 6, 20, 27, 0x00000078, 4, 7, 0x00000001 },
    { "uqsubaddx", 2, 6, 20, 27, 0x00000066, 4, 7, 0x00000005 },
    { "uqsub8", 2, 6, 20, 27, 0x00000066, 4, 7, 0x0000000f },
    { "uqsub16", 2, 6, 20, 27, 0x00000066, 4, 7, 0x00000007 },
    { "uqaddsubx", 2, 6, 20, 27, 0x00000066, 4, 7, 0x00000003 },
    { "uqadd8", 2, 6, 20, 27, 0x00000066, 4, 7, 0x00000009 },
    { "uqadd16", 2, 6, 20, 27, 0x00000066, 4, 7, 0x00000001 },
    { "sxtab", 2, 6, 20, 27, 0x0000006a, 4, 7, 0x00000007 },
    { "uhsubaddx", 2, 6, 20, 27, 0x00000067, 4, 7, 0x00000005 },
    { "uhsub8", 2, 6, 20, 27, 0x00000067, 4, 7, 0x0000000f },
    { "uhsub16", 2, 6, 20, 27, 0x00000067, 4, 7, 0x00000007 },
    { "uhaddsubx", 2, 6, 20, 27, 0x00000067, 4, 7, 0x00000003 },
    { "uhadd8", 2, 6, 20, 27, 0x00000067, 4, 7, 0x00000009 },
    { "uhadd16", 2, 6, 20, 27, 0x00000067, 4, 7, 0x00000001 },
    { "uaddsubx", 2, 6, 20, 27, 0x00000065, 4, 7, 0x00000003 },
    { "uadd8", 2, 6, 20, 27, 0x00000065, 4, 7, 0x00000009 },
    { "uadd16", 2, 6, 20, 27, 0x00000065, 4, 7, 0x00000001 },
    { "sxtah", 2, 6, 20, 27, 0x0000006b, 4, 7, 0x00000007 },
    { "sxtab16", 2, 6, 20, 27, 0x00000068, 4, 7, 0x00000007 },
    { "qadd8", 2, 6, 20, 27, 0x00000062, 4, 7, 0x00000009 },
    { "bxj", 2, 5, 20, 27, 0x00000012, 4, 7, 0x00000002 },
    { "clz", 2, 3, 20, 27, 0x00000016, 4, 7, 0x00000001 },
    { "uxtah", 2, 6, 20, 27, 0x0000006f, 4, 7, 0x00000007 },
    { "bx", 2, 2, 20, 27, 0x00000012, 4, 7, 0x00000001 },
    { "rev", 2, 6, 20, 27, 0x0000006b, 4, 7, 0x00000003 },
    { "blx", 2, 3, 20, 27, 0x00000012, 4, 7, 0x00000003 },
    { "revsh", 2, 6, 20, 27, 0x0000006f, 4, 7, 0x0000000b },
    { "qadd", 2, 4, 20, 27, 0x00000010, 4, 7, 0x00000005 },
    { "qadd16", 2, 6, 20, 27, 0x00000062, 4, 7, 0x00000001 },
    { "qaddsubx", 2, 6, 20, 27, 0x00000062, 4, 7, 0x00000003 },
    { "ldrex", 2, 0, 20, 27, 0x00000019, 4, 7, 0x00000009 },
    { "qdadd", 2, 4, 20, 27, 0x00000014, 4, 7, 0x00000005 },
    { "qdsub", 2, 4, 20, 27, 0x00000016, 4, 7, 0x00000005 },
    { "qsub", 2, 4, 20, 27, 0x00000012, 4, 7, 0x00000005 },
    { "ldrexb", 2, 7, 20, 27, 0x0000001d, 4, 7, 0x00000009 },
    { "qsub8", 2, 6, 20, 27, 0x00000062, 4, 7, 0x0000000f },
    { "qsub16", 2, 6, 20, 27, 0x00000062, 4, 7, 0x00000007 },
    { "smuad", 4, 6, 20, 27, 0x00000070, 12, 15, 0x0000000f, 6, 7, 0x00000000, 4, 4, 0x00000001 },
    { "smmul", 4, 6, 20, 27, 0x00000075, 12, 15, 0x0000000f, 6, 7, 0x00000000, 4, 4, 0x00000001 },
    { "smusd", 4, 6, 20, 27, 0x00000070, 12, 15, 0x0000000f, 6, 7, 0x00000001, 4, 4, 0x00000001 },
    { "smlsd", 3, 6, 20, 27, 0x00000070, 6, 7, 0x00000001, 4, 4, 0x00000001 },
    { "smlsld", 3, 6, 20, 27, 0x00000074, 6, 7, 0x00000001, 4, 4, 0x00000001 },
    { "smmla", 3, 6, 20, 27, 0x00000075, 6, 7, 0x00000000, 4, 4, 0x00000001 },
    { "smmls", 3, 6, 20, 27, 0x00000075, 6, 7, 0x00000003, 4, 4, 0x00000001 },
    { "smlald", 3, 6, 20, 27, 0x00000074, 6, 7, 0x00000000, 4, 4, 0x00000001 },
    { "smlad", 3, 6, 20, 27, 0x00000070, 6, 7, 0x00000000, 4, 4, 0x00000001 },
    { "smlaw", 3, 4, 20, 27, 0x00000012, 7, 7, 0x00000001, 4, 5, 0x00000000 },
    { "smulw", 3, 4, 20, 27, 0x00000012, 7, 7, 0x00000001, 4, 5, 0x00000002 },
    { "pkhtb", 2, 6, 20, 27, 0x00000068, 4, 6, 0x00000005 },
    { "pkhbt", 2, 6, 20, 27, 0x00000068, 4, 6, 0x00000001 },
    { "smul", 3, 4, 20, 27, 0x00000016, 7, 7, 0x00000001, 4, 4, 0x00000000 },
    { "smlalxy", 3, 4, 20, 27, 0x00000014, 7, 7, 0x00000001, 4, 4, 0x00000000 },
    { "smla", 3, 4, 20, 27, 0x00000010, 7, 7, 0x00000001, 4, 4, 0x00000000 },
    { "mcrr", 1, 6, 20, 27, 0x000000c4 },
    { "mrrc", 1, 6, 20, 27, 0x000000c5 },
    { "cmp", 2, 0, 26, 27, 0x00000000, 20, 24, 0x00000015 },
    { "tst", 2, 0, 26, 27, 0x00000000, 20, 24, 0x00000011 },
    { "teq", 2, 0, 26, 27, 0x00000000, 20, 24, 0x00000013 },
    { "cmn", 2, 0, 26, 27, 0x00000000, 20, 24, 0x00000017 },
    { "smull", 2, 0, 21, 27, 0x00000006, 4, 7, 0x00000009 },
    { "umull", 2, 0, 21, 27, 0x00000004, 4, 7, 0x00000009 },
    { "umlal", 2, 0, 21, 27, 0x00000005, 4, 7, 0x00000009 },
    { "smlal", 2, 0, 21, 27, 0x00000007, 4, 7, 0x00000009 },
    { "mul", 2, 0, 21, 27, 0x00000000, 4, 7, 0x00000009 },
    { "mla", 2, 0, 21, 27, 0x00000001, 4, 7, 0x00000009 },
    { "ssat", 2, 6, 21, 27, 0x00000035, 4, 5, 0x00000001 },
    { "usat", 2, 6, 21, 27, 0x00000037, 4, 5, 0x00000001 },
    { "mrs", 4, 0, 23, 27, 0x00000002, 20, 21, 0x00000000, 16, 19, 0x0000000f, 0, 11, 0x00000000 },
    { "msr", 3, 0, 23, 27, 0x00000002, 20, 21, 0x00000002, 4, 7, 0x00000000 },
    { "and", 2, 0, 26, 27, 0x00000000, 21, 24, 0x00000000 },
    { "bic", 2, 0, 26, 27, 0x00000000, 21, 24, 0x0000000e },
    { "ldm", 3, 0, 25, 27, 0x00000004, 20, 22, 0x00000005, 15, 15, 0x00000000 },
    { "eor", 2, 0, 26, 27, 0x00000000, 21, 24, 0x00000001 },
    { "add", 2, 0, 26, 27, 0x00000000, 21, 24, 0x00000004 },
    { "rsb", 2, 0, 26, 27, 0x00000000, 21, 24, 0x00000003 },
    { "rsc", 2, 0, 26, 27, 0x00000000, 21, 24, 0x00000007 },
    { "sbc", 2, 0, 26, 27, 0x00000000, 21, 24, 0x00000006 },
    { "adc", 2, 0, 26, 27, 0x00000000, 21, 24, 0x00000005 },
    { "sub", 2, 0, 26, 27, 0x00000000, 21, 24, 0x00000002 },
    { "orr", 2, 0, 26, 27, 0x00000000, 21, 24, 0x0000000c },
    { "mvn", 2, 0, 26, 27, 0x00000000, 21, 24, 0x0000000f },
    { "mov", 2, 0, 26, 27, 0x00000000, 21, 24, 0x0000000d },
    { "stm", 2, 0, 25, 27, 0x00000004, 20, 22, 0x00000004 },
    { "ldm", 4, 0, 25, 27, 0x00000004, 22, 22, 0x00000001, 20, 20, 0x00000001, 15, 15, 0x00000001 },
    { "ldrsh", 3, 2, 25, 27, 0x00000000, 20, 20, 0x00000001, 4, 7, 0x0000000f },
    { "stm", 3, 0, 25, 27, 0x00000004, 22, 22, 0x00000000, 20, 20, 0x00000000 },
    { "ldm", 3, 0, 25, 27, 0x00000004, 22, 22, 0x00000000, 20, 20, 0x00000001 },
    { "ldrsb", 3, 2, 25, 27, 0x00000000, 20, 20, 0x00000001, 4, 7, 0x0000000d },
    { "strd", 3, 4, 25, 27, 0x00000000, 20, 20, 0x00000000, 4, 7, 0x0000000f },
    { "ldrh", 3, 0, 25, 27, 0x00000000, 20, 20, 0x00000001, 4, 7, 0x0000000b },
    { "strh", 3, 0, 25, 27, 0x00000000, 20, 20, 0x00000000, 4, 7, 0x0000000b },
    { "ldrd", 3, 4, 25, 27, 0x00000000, 20, 20, 0x00000000, 4, 7, 0x0000000d },
    { "strt", 3, 0, 26, 27, 0x00000001, 24, 24, 0x00000000, 20, 22, 0x00000002 },
    { "strbt", 3, 0, 26, 27, 0x00000001, 24, 24, 0x00000000, 20, 22, 0x00000006 },
    { "ldrbt", 3, 0, 26, 27, 0x00000001, 24, 24, 0x00000000, 20, 22, 0x00000007 },
    { "ldrt", 3, 0, 26, 27, 0x00000001, 24, 24, 0x00000000, 20, 22, 0x00000003 },
    { "mrc", 3, 6, 24, 27, 0x0000000e, 20, 20, 0x00000001, 4, 4, 0x00000001 },
    { "mcr", 3, 0, 24, 27, 0x0000000e, 20, 20, 0x00000000, 4, 4, 0x00000001 },
    { "msr", 2, 0, 23, 27, 0x00000006, 20, 21, 0x00000002 },
    { "ldrb", 3, 0, 26, 27, 0x00000001, 22, 22, 0x00000001, 20, 20, 0x00000001 },
    { "strb", 3, 0, 26, 27, 0x00000001, 22, 22, 0x00000001, 20, 20, 0x00000000 },
    { "ldr", 4, 0, 28, 31, 0x0000000e, 26, 27, 0x00000001, 22, 22, 0x00000000, 20, 20, 0x00000001 },
    { "ldrcond", 3, 0, 26, 27, 0x00000001, 22, 22, 0x00000000, 20, 20, 0x00000001 },
    { "str", 3, 0, 26, 27, 0x00000001, 22, 22, 0x00000000, 20, 20, 0x00000000 },
    { "cdp", 2, 0, 24, 27, 0x0000000e, 4, 4, 0x00000000 },
    { "stc", 2, 0, 25, 27, 0x00000006, 20, 20, 0x00000000 },
    { "ldc", 2, 0, 25, 27, 0x00000006, 20, 20, 0x00000001 },
    { "swi", 1, 0, 24, 27, 0x0000000f },
    { "bbl", 1, 0, 25, 27, 0x00000005 },
    { "ldrexd", 2, ARMV6K, 20, 27, 0x0000001B, 4, 7, 0x00000009 },
    { "strexd", 2, ARMV6K, 20, 27, 0x0000001A, 4, 7, 0x00000009 },
    { "ldrexh", 2, ARMV6K, 20, 27, 0x0000001F, 4, 7, 0x00000009 },
    { "strexh", 2, ARMV6K, 20, 27, 0x0000001E, 4, 7, 0x00000009 },
};

const ISEITEM arm_exclusion_code[] = {
    { "vmla", 0, ARMVFP2, 0 },
    { "vmls", 0, ARMVFP2, 0 },
    { "vnmla", 0, ARMVFP2, 0 },
    { "vnmla", 0, ARMVFP2, 0 },
    { "vnmls", 0, ARMVFP2, 0 },
    { "vnmul", 0, ARMVFP2, 0 },
    { "vmul", 0, ARMVFP2, 0 },
    { "vadd", 0, ARMVFP2, 0 },
    { "vsub", 0, ARMVFP2, 0 },
    { "vdiv", 0, ARMVFP2, 0 },
    { "vmov(i)", 0, ARMVFP3, 0 },
    { "vmov(r)", 0, ARMVFP3, 0 },
    { "vabs", 0, ARMVFP2, 0 },
    { "vneg", 0, ARMVFP2, 0 },
    { "vsqrt", 0, ARMVFP2, 0 },
    { "vcmp", 0, ARMVFP2, 0 },
    { "vcmp2", 0, ARMVFP2, 0 },
    { "vcvt(bff)", 0, ARMVFP3, 4, 4, 1 },
    { "vcvt(bds)", 0, ARMVFP2, 0 },
    { "vcvt(bfi)", 0, ARMVFP2, 0 },
    { "vmovbrs", 0, ARMVFP2, 0 },
    { "vmsr", 0, ARMVFP2, 0 },
    { "vmovbrc", 0, ARMVFP2, 0 },
    { "vmrs", 0, ARMVFP2, 0 },
    { "vmovbcr", 0, ARMVFP2, 0 },
    { "vmovbrrss", 0, ARMVFP2, 0 },
    { "vmovbrrd", 0, ARMVFP2, 0 },
    { "vstr", 0, ARMVFP2, 0 },
    { "vpush", 0, ARMVFP2, 0 },
    { "vstm", 0, ARMVFP2, 0 },
    { "vpop", 0, ARMVFP2, 0 },
    { "vldr", 0, ARMVFP2, 0 },
    { "vldm", 0, ARMVFP2, 0 },

    { "srs", 0, 6, 0 },
    { "rfe", 0, 6, 0 },
    { "bkpt", 0, 3, 0 },
    { "blx", 0, 3, 0 },
    { "cps", 0, 6, 0 },
    { "pld", 0, 4, 0 },
    { "setend", 0, 6, 0 },
    { "clrex", 0, 6, 0 },
    { "rev16", 0, 6, 0 },
    { "usad8", 0, 6, 0 },
    { "sxtb", 0, 6, 0 },
    { "uxtb", 0, 6, 0 },
    { "sxth", 0, 6, 0 },
    { "sxtb16", 0, 6, 0 },
    { "uxth", 0, 6, 0 },
    { "uxtb16", 0, 6, 0 },
    { "cpy", 0, 6, 0 },
    { "uxtab", 0, 6, 0 },
    { "ssub8", 0, 6, 0 },
    { "shsub8", 0, 6, 0 },
    { "ssubaddx", 0, 6, 0 },
    { "strex", 0, 6, 0 },
    { "strexb", 0, 7, 0 },
    { "swp", 0, 0, 0 },
    { "swpb", 0, 0, 0 },
    { "ssub16", 0, 6, 0 },
    { "ssat16", 0, 6, 0 },
    { "shsubaddx", 0, 6, 0 },
    { "qsubaddx", 0, 6, 0 },
    { "shaddsubx", 0, 6, 0 },
    { "shadd8", 0, 6, 0 },
    { "shadd16", 0, 6, 0 },
    { "sel", 0, 6, 0 },
    { "saddsubx", 0, 6, 0 },
    { "sadd8", 0, 6, 0 },
    { "sadd16", 0, 6, 0 },
    { "shsub16", 0, 6, 0 },
    { "umaal", 0, 6, 0 },
    { "uxtab16", 0, 6, 0 },
    { "usubaddx", 0, 6, 0 },
    { "usub8", 0, 6, 0 },
    { "usub16", 0, 6, 0 },
    { "usat16", 0, 6, 0 },
    { "usada8", 0, 6, 0 },
    { "uqsubaddx", 0, 6, 0 },
    { "uqsub8", 0, 6, 0 },
    { "uqsub16", 0, 6, 0 },
    { "uqaddsubx", 0, 6, 0 },
    { "uqadd8", 0, 6, 0 },
    { "uqadd16", 0, 6, 0 },
    { "sxtab", 0, 6, 0 },
    { "uhsubaddx", 0, 6, 0 },
    { "uhsub8", 0, 6, 0 },
    { "uhsub16", 0, 6, 0 },
    { "uhaddsubx", 0, 6, 0 },
    { "uhadd8", 0, 6, 0 },
    { "uhadd16", 0, 6, 0 },
    { "uaddsubx", 0, 6, 0 },
    { "uadd8", 0, 6, 0 },
    { "uadd16", 0, 6, 0 },
    { "sxtah", 0, 6, 0 },
    { "sxtab16", 0, 6, 0 },
    { "qadd8", 0, 6, 0 },
    { "bxj", 0, 5, 0 },
    { "clz", 0, 3, 0 },
    { "uxtah", 0, 6, 0 },
    { "bx", 0, 2, 0 },
    { "rev", 0, 6, 0 },
    { "blx", 0, 3, 0 },
    { "revsh", 0, 6, 0 },
    { "qadd", 0, 4, 0 },
    { "qadd16", 0, 6, 0 },
    { "qaddsubx", 0, 6, 0 },
    { "ldrex", 0, 0, 0 },
    { "qdadd", 0, 4, 0 },
    { "qdsub", 0, 4, 0 },
    { "qsub", 0, 4, 0 },
    { "ldrexb", 0, 7, 0 },
    { "qsub8", 0, 6, 0 },
    { "qsub16", 0, 6, 0 },
    { "smuad", 0, 6, 0 },
    { "smmul", 0, 6, 0 },
    { "smusd", 0, 6, 0 },
    { "smlsd", 0, 6, 0 },
    { "smlsld", 0, 6, 0 },
    { "smmla", 0, 6, 0 },
    { "smmls", 0, 6, 0 },
    { "smlald", 0, 6, 0 },
    { "smlad", 0, 6, 0 },
    { "smlaw", 0, 4, 0 },
    { "smulw", 0, 4, 0 },
    { "pkhtb", 0, 6, 0 },
    { "pkhbt", 0, 6, 0 },
    { "smul", 0, 4, 0 },
    { "smlal", 0, 4, 0 },
    { "smla", 0, 4, 0 },
    { "mcrr", 0, 6, 0 },
    { "mrrc", 0, 6, 0 },
    { "cmp", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "tst", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "teq", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "cmn", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "smull", 0, 0, 0 },
    { "umull", 0, 0, 0 },
    { "umlal", 0, 0, 0 },
    { "smlal", 0, 0, 0 },
    { "mul", 0, 0, 0 },
    { "mla", 0, 0, 0 },
    { "ssat", 0, 6, 0 },
    { "usat", 0, 6, 0 },
    { "mrs", 0, 0, 0 },
    { "msr", 0, 0, 0 },
    { "and", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "bic", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "ldm", 0, 0, 0 },
    { "eor", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "add", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "rsb", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "rsc", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "sbc", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "adc", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "sub", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "orr", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "mvn", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "mov", 3, 0, 4, 4, 0x00000001, 7, 7, 0x00000001, 25, 25, 0x00000000 },
    { "stm", 0, 0, 0 },
    { "ldm", 0, 0, 0 },
    { "ldrsh", 0, 2, 0 },
    { "stm", 0, 0, 0 },
    { "ldm", 0, 0, 0 },
    { "ldrsb", 0, 2, 0 },
    { "strd", 0, 4, 0 },
    { "ldrh", 0, 0, 0 },
    { "strh", 0, 0, 0 },
    { "ldrd", 0, 4, 0 },
    { "strt", 0, 0, 0 },
    { "strbt", 0, 0, 0 },
    { "ldrbt", 0, 0, 0 },
    { "ldrt", 0, 0, 0 },
    { "mrc", 0, 6, 0 },
    { "mcr", 0, 0, 0 },
    { "msr", 0, 0, 0 },
    { "ldrb", 0, 0, 0 },
    { "strb", 0, 0, 0 },
    { "ldr", 0, 0, 0 },
    { "ldrcond", 1, 0, 28, 31, 0x0000000e },
    { "str", 0, 0, 0 },
    { "cdp", 0, 0, 0 },
    { "stc", 0, 0, 0 },
    { "ldc", 0, 0, 0 },
    { "swi", 0, 0, 0 },
    { "bbl", 0, 0, 0 },
    { "ldrexd", 0, ARMV6K, 0 },
    { "strexd", 0, ARMV6K, 0 },
    { "ldrexh", 0, ARMV6K, 0 },
    { "strexh", 0, ARMV6K, 0 },

    { "bl_1_thumb", 0, INVALID, 0 },    // Should be table[-4]
    { "bl_2_thumb", 0, INVALID, 0 },    // Should be located at the end of the table[-3]
    { "blx_1_thumb", 0, INVALID, 0 },   // Should be located at table[-2]
    { "invalid", 0, INVALID, 0 }
};

int decode_arm_instr(uint32_t instr, int32_t *idx) {
    int n = 0;
    int base = 0;
    int ret = DECODE_FAILURE;
    int i = 0;
    int instr_slots = sizeof(arm_instruction) / sizeof(ISEITEM);

    for (i = 0; i < instr_slots; i++) {
        n = arm_instruction[i].attribute_value;
        base = 0;

        while (n) {
            if (arm_instruction[i].content[base + 1] == 31 && arm_instruction[i].content[base] == 0) {
                // clrex
                if (instr != arm_instruction[i].content[base + 2]) {
                    break;
                }
            } else if (BITS(arm_instruction[i].content[base], arm_instruction[i].content[base + 1]) != arm_instruction[i].content[base + 2]) {
                break;
            }
            base += 3;
            n--;
        }

        // All conditions is satisfied.
        if (n == 0)
            ret = DECODE_SUCCESS;

        if (ret == DECODE_SUCCESS) {
            n = arm_exclusion_code[i].attribute_value;
            if (n != 0) {
                base = 0;
                while (n) {
                    if (BITS(arm_exclusion_code[i].content[base], arm_exclusion_code[i].content[base + 1]) != arm_exclusion_code[i].content[base + 2]) {
                        break;
                    }
                    base += 3;
                    n--;
                }

                // All conditions is satisfied.
                if (n == 0)
                    ret = DECODE_FAILURE;
            }
        }

        if (ret == DECODE_SUCCESS) {
            *idx = i;
            return ret;
        }
    }
    return ret;
}
