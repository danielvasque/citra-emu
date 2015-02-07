/*
    vfp/vfp.h - ARM VFPv3 emulation unit - vfp interface
    Copyright (C) 2003 Skyeye Develop Group
    for help please send mail to <skyeye-developer@lists.gro.clinux.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#pragma once

#include "core/arm/skyeye_common/vfp/vfp_helper.h" /* for references to cdp SoftFloat functions */

#define VFP_DEBUG_UNIMPLEMENTED(x) LOG_ERROR(Core_ARM11, "in func %s, " #x " unimplemented\n", __FUNCTION__); exit(-1);
#define VFP_DEBUG_UNTESTED(x) LOG_TRACE(Core_ARM11, "in func %s, " #x " untested\n", __FUNCTION__);
#define CHECK_VFP_ENABLED
#define CHECK_VFP_CDP_RET vfp_raise_exceptions(cpu, ret, inst_cream->instr, cpu->VFP[VFP_OFFSET(VFP_FPSCR)]); //if (ret == -1) {printf("VFP CDP FAILURE %x\n", inst_cream->instr); exit(-1);}

unsigned VFPInit(ARMul_State* state);
unsigned VFPMRC(ARMul_State* state, unsigned type, ARMword instr, ARMword* value);
unsigned VFPMCR(ARMul_State* state, unsigned type, ARMword instr, ARMword value);
unsigned VFPMRRC(ARMul_State* state, unsigned type, ARMword instr, ARMword* value1, ARMword* value2);
unsigned VFPMCRR(ARMul_State* state, unsigned type, ARMword instr, ARMword value1, ARMword value2);
unsigned VFPSTC(ARMul_State* state, unsigned type, ARMword instr, ARMword* value);
unsigned VFPLDC(ARMul_State* state, unsigned type, ARMword instr, ARMword value);
unsigned VFPCDP(ARMul_State* state, unsigned type, ARMword instr);

s32 vfp_get_float(ARMul_State* state, u32 reg);
void vfp_put_float(ARMul_State* state, s32 val, u32 reg);
u64 vfp_get_double(ARMul_State* state, u32 reg);
void vfp_put_double(ARMul_State* state, u64 val, u32 reg);
void vfp_raise_exceptions(ARMul_State* state, u32 exceptions, u32 inst, u32 fpscr);
u32 vfp_single_cpdo(ARMul_State* state, u32 inst, u32 fpscr);
u32 vfp_double_cpdo(ARMul_State* state, u32 inst, u32 fpscr);

// MRC
void VMRS(ARMul_State* state, ARMword reg, ARMword Rt, ARMword* value);
void VMOVBRS(ARMul_State* state, ARMword to_arm, ARMword t, ARMword n, ARMword* value);
void VMOVBRRD(ARMul_State* state, ARMword to_arm, ARMword t, ARMword t2, ARMword n, ARMword* value1, ARMword* value2);
void VMOVBRRSS(ARMul_State* state, ARMword to_arm, ARMword t, ARMword t2, ARMword n, ARMword* value1, ARMword* value2);
void VMOVI(ARMul_State* state, ARMword single, ARMword d, ARMword imm);
void VMOVR(ARMul_State* state, ARMword single, ARMword d, ARMword imm);

// MCR
void VMSR(ARMul_State* state, ARMword reg, ARMword Rt);

// STC
int VSTM(ARMul_State* state, int type, ARMword instr, ARMword* value);
int VPUSH(ARMul_State* state, int type, ARMword instr, ARMword* value);
int VSTR(ARMul_State* state, int type, ARMword instr, ARMword* value);

// LDC
int VLDM(ARMul_State* state, int type, ARMword instr, ARMword value);
int VPOP(ARMul_State* state, int type, ARMword instr, ARMword value);
int VLDR(ARMul_State* state, int type, ARMword instr, ARMword value);
