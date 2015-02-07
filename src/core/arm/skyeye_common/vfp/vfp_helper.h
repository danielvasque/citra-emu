/*
    vfp/vfp.h - ARM VFPv3 emulation unit - SoftFloat lib helper
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

/* 
 *  The following code is derivative from Linux Android kernel vfp
 *  floating point support.
 * 
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#pragma once

/* Custom edit */

#include <stdint.h>
#include <stdio.h>

#include "common/common_types.h"
#include "core/arm/skyeye_common/armdefs.h"

#define pr_info //printf
#define pr_debug //printf

#define do_div(n, base) {n/=base;}

/* From vfpinstr.h */

#define INST_CPRTDO(inst)	(((inst) & 0x0f000000) == 0x0e000000)
#define INST_CPRT(inst)		((inst) & (1 << 4))
#define INST_CPRT_L(inst)	((inst) & (1 << 20))
#define INST_CPRT_Rd(inst)	(((inst) & (15 << 12)) >> 12)
#define INST_CPRT_OP(inst)	(((inst) >> 21) & 7)
#define INST_CPNUM(inst)	((inst) & 0xf00)
#define CPNUM(cp)		((cp) << 8)

#define FOP_MASK	(0x00b00040)
#define FOP_FMAC	(0x00000000)
#define FOP_FNMAC	(0x00000040)
#define FOP_FMSC	(0x00100000)
#define FOP_FNMSC	(0x00100040)
#define FOP_FMUL	(0x00200000)
#define FOP_FNMUL	(0x00200040)
#define FOP_FADD	(0x00300000)
#define FOP_FSUB	(0x00300040)
#define FOP_FDIV	(0x00800000)
#define FOP_EXT		(0x00b00040)

#define FOP_TO_IDX(inst)	((inst & 0x00b00000) >> 20 | (inst & (1 << 6)) >> 4)

#define FEXT_MASK	(0x000f0080)
#define FEXT_FCPY	(0x00000000)
#define FEXT_FABS	(0x00000080)
#define FEXT_FNEG	(0x00010000)
#define FEXT_FSQRT	(0x00010080)
#define FEXT_FCMP	(0x00040000)
#define FEXT_FCMPE	(0x00040080)
#define FEXT_FCMPZ	(0x00050000)
#define FEXT_FCMPEZ	(0x00050080)
#define FEXT_FCVT	(0x00070080)
#define FEXT_FUITO	(0x00080000)
#define FEXT_FSITO	(0x00080080)
#define FEXT_FTOUI	(0x000c0000)
#define FEXT_FTOUIZ	(0x000c0080)
#define FEXT_FTOSI	(0x000d0000)
#define FEXT_FTOSIZ	(0x000d0080)

#define FEXT_TO_IDX(inst)	((inst & 0x000f0000) >> 15 | (inst & (1 << 7)) >> 7)

#define vfp_get_sd(inst)	((inst & 0x0000f000) >> 11 | (inst & (1 << 22)) >> 22)
#define vfp_get_dd(inst)	((inst & 0x0000f000) >> 12 | (inst & (1 << 22)) >> 18)
#define vfp_get_sm(inst)	((inst & 0x0000000f) << 1 | (inst & (1 << 5)) >> 5)
#define vfp_get_dm(inst)	((inst & 0x0000000f) | (inst & (1 << 5)) >> 1)
#define vfp_get_sn(inst)	((inst & 0x000f0000) >> 15 | (inst & (1 << 7)) >> 7)
#define vfp_get_dn(inst)	((inst & 0x000f0000) >> 16 | (inst & (1 << 7)) >> 3)

#define vfp_single(inst)	(((inst) & 0x0000f00) == 0xa00)

#define FPSCR_N	(1 << 31)
#define FPSCR_Z	(1 << 30)
#define FPSCR_C (1 << 29)
#define FPSCR_V	(1 << 28)

static inline u32 vfp_shiftright32jamming(u32 val, unsigned int shift)
{
	if (shift) {
		if (shift < 32)
			val = val >> shift | ((val << (32 - shift)) != 0);
		else
			val = val != 0;
	}
	return val;
}

static inline u64 vfp_shiftright64jamming(u64 val, unsigned int shift)
{
	if (shift) {
		if (shift < 64)
			val = val >> shift | ((val << (64 - shift)) != 0);
		else
			val = val != 0;
	}
	return val;
}

static inline u32 vfp_hi64to32jamming(u64 val)
{
	u32 v;
	u32 highval = val >> 32;
	u32 lowval = val & 0xffffffff;

	if (lowval >= 1)
		v = highval | 1;
	else
		v = highval;

	return v;
}

static inline void add128(u64 *resh, u64 *resl, u64 nh, u64 nl, u64 mh, u64 ml)
{
	*resl = nl + ml;
	*resh = nh + mh;
	if (*resl < nl)
		*resh += 1;
}

static inline void sub128(u64 *resh, u64 *resl, u64 nh, u64 nl, u64 mh, u64 ml)
{
	*resl = nl - ml;
	*resh = nh - mh;
	if (*resl > nl)
		*resh -= 1;
}

static inline void mul64to128(u64 *resh, u64 *resl, u64 n, u64 m)
{
	u32 nh, nl, mh, ml;
	u64 rh, rma, rmb, rl;

	nl = n;
	ml = m;
	rl = (u64)nl * ml;

	nh = n >> 32;
	rma = (u64)nh * ml;

	mh = m >> 32;
	rmb = (u64)nl * mh;
	rma += rmb;

	rh = (u64)nh * mh;
	rh += ((u64)(rma < rmb) << 32) + (rma >> 32);

	rma <<= 32;
	rl += rma;
	rh += (rl < rma);

	*resl = rl;
	*resh = rh;
}

static inline void shift64left(u64 *resh, u64 *resl, u64 n)
{
	*resh = n >> 63;
	*resl = n << 1;
}

static inline u64 vfp_hi64multiply64(u64 n, u64 m)
{
	u64 rh, rl;
	mul64to128(&rh, &rl, n, m);
	return rh | (rl != 0);
}

static inline u64 vfp_estimate_div128to64(u64 nh, u64 nl, u64 m)
{
	u64 mh, ml, remh, reml, termh, terml, z;

	if (nh >= m)
		return ~0ULL;
	mh = m >> 32;
	if (mh << 32 <= nh) {
		z = 0xffffffff00000000ULL;
	} else {
		z = nh;
		do_div(z, mh);
		z <<= 32;
	}
	mul64to128(&termh, &terml, m, z);
	sub128(&remh, &reml, nh, nl, termh, terml);
	ml = m << 32;
	while ((s64)remh < 0) {
		z -= 0x100000000ULL;
		add128(&remh, &reml, remh, reml, mh, ml);
	}
	remh = (remh << 32) | (reml >> 32);
	if (mh << 32 <= remh) {
		z |= 0xffffffff;
	} else {
		do_div(remh, mh);
		z |= remh;
	}
	return z;
}

/*
 * Operations on unpacked elements
 */
#define vfp_sign_negate(sign)	(sign ^ 0x8000)

/*
 * Single-precision
 */
struct vfp_single {
	s16	exponent;
	u16	sign;
	u32	significand;
};

/*
 * VFP_SINGLE_MANTISSA_BITS - number of bits in the mantissa
 * VFP_SINGLE_EXPONENT_BITS - number of bits in the exponent
 * VFP_SINGLE_LOW_BITS - number of low bits in the unpacked significand
 *  which are not propagated to the float upon packing.
 */
#define VFP_SINGLE_MANTISSA_BITS	(23)
#define VFP_SINGLE_EXPONENT_BITS	(8)
#define VFP_SINGLE_LOW_BITS		(32 - VFP_SINGLE_MANTISSA_BITS - 2)
#define VFP_SINGLE_LOW_BITS_MASK	((1 << VFP_SINGLE_LOW_BITS) - 1)

/*
 * The bit in an unpacked float which indicates that it is a quiet NaN
 */
#define VFP_SINGLE_SIGNIFICAND_QNAN	(1 << (VFP_SINGLE_MANTISSA_BITS - 1 + VFP_SINGLE_LOW_BITS))

/*
 * Operations on packed single-precision numbers
 */
#define vfp_single_packed_sign(v)	((v) & 0x80000000)
#define vfp_single_packed_negate(v)	((v) ^ 0x80000000)
#define vfp_single_packed_abs(v)	((v) & ~0x80000000)
#define vfp_single_packed_exponent(v)	(((v) >> VFP_SINGLE_MANTISSA_BITS) & ((1 << VFP_SINGLE_EXPONENT_BITS) - 1))
#define vfp_single_packed_mantissa(v)	((v) & ((1 << VFP_SINGLE_MANTISSA_BITS) - 1))

/*
 * Unpack a single-precision float.  Note that this returns the magnitude
 * of the single-precision float mantissa with the 1. if necessary,
 * aligned to bit 30.
 */
static inline void vfp_single_unpack(struct vfp_single *s, s32 val)
{
	u32 significand;

	s->sign = vfp_single_packed_sign(val) >> 16,
	s->exponent = vfp_single_packed_exponent(val);

	significand = (u32) val;
	significand = (significand << (32 - VFP_SINGLE_MANTISSA_BITS)) >> 2;
	if (s->exponent && s->exponent != 255)
		significand |= 0x40000000;
	s->significand = significand;
}

/*
 * Re-pack a single-precision float.  This assumes that the float is
 * already normalised such that the MSB is bit 30, _not_ bit 31.
 */
static inline s32 vfp_single_pack(struct vfp_single *s)
{
	u32 val;
	val = (s->sign << 16) +
	      (s->exponent << VFP_SINGLE_MANTISSA_BITS) +
	      (s->significand >> VFP_SINGLE_LOW_BITS);
	return (s32)val;
}

#define VFP_NUMBER		(1<<0)
#define VFP_ZERO		(1<<1)
#define VFP_DENORMAL		(1<<2)
#define VFP_INFINITY		(1<<3)
#define VFP_NAN			(1<<4)
#define VFP_NAN_SIGNAL		(1<<5)

#define VFP_QNAN		(VFP_NAN)
#define VFP_SNAN		(VFP_NAN|VFP_NAN_SIGNAL)

static inline int vfp_single_type(struct vfp_single *s)
{
	int type = VFP_NUMBER;
	if (s->exponent == 255) {
		if (s->significand == 0)
			type = VFP_INFINITY;
		else if (s->significand & VFP_SINGLE_SIGNIFICAND_QNAN)
			type = VFP_QNAN;
		else
			type = VFP_SNAN;
	} else if (s->exponent == 0) {
		if (s->significand == 0)
			type |= VFP_ZERO;
		else
			type |= VFP_DENORMAL;
	}
	return type;
}


u32 vfp_single_normaliseround(ARMul_State* state, int sd, struct vfp_single *vs, u32 fpscr, u32 exceptions, const char *func);

/*
 * Double-precision
 */
struct vfp_double {
	s16	exponent;
	u16	sign;
	u64	significand;
};

/*
 * VFP_REG_ZERO is a special register number for vfp_get_double
 * which returns (double)0.0.  This is useful for the compare with
 * zero instructions.
 */
#ifdef CONFIG_VFPv3
#define VFP_REG_ZERO	32
#else
#define VFP_REG_ZERO	16
#endif

#define VFP_DOUBLE_MANTISSA_BITS	(52)
#define VFP_DOUBLE_EXPONENT_BITS	(11)
#define VFP_DOUBLE_LOW_BITS		(64 - VFP_DOUBLE_MANTISSA_BITS - 2)
#define VFP_DOUBLE_LOW_BITS_MASK	((1 << VFP_DOUBLE_LOW_BITS) - 1)

/*
 * The bit in an unpacked double which indicates that it is a quiet NaN
 */
#define VFP_DOUBLE_SIGNIFICAND_QNAN	(1ULL << (VFP_DOUBLE_MANTISSA_BITS - 1 + VFP_DOUBLE_LOW_BITS))

/*
 * Operations on packed single-precision numbers
 */
#define vfp_double_packed_sign(v)	((v) & (1ULL << 63))
#define vfp_double_packed_negate(v)	((v) ^ (1ULL << 63))
#define vfp_double_packed_abs(v)	((v) & ~(1ULL << 63))
#define vfp_double_packed_exponent(v)	(((v) >> VFP_DOUBLE_MANTISSA_BITS) & ((1 << VFP_DOUBLE_EXPONENT_BITS) - 1))
#define vfp_double_packed_mantissa(v)	((v) & ((1ULL << VFP_DOUBLE_MANTISSA_BITS) - 1))

/*
 * Unpack a double-precision float.  Note that this returns the magnitude
 * of the double-precision float mantissa with the 1. if necessary,
 * aligned to bit 62.
 */
static inline void vfp_double_unpack(struct vfp_double *s, s64 val)
{
	u64 significand;

	s->sign = vfp_double_packed_sign(val) >> 48;
	s->exponent = vfp_double_packed_exponent(val);

	significand = (u64) val;
	significand = (significand << (64 - VFP_DOUBLE_MANTISSA_BITS)) >> 2;
	if (s->exponent && s->exponent != 2047)
		significand |= (1ULL << 62);
	s->significand = significand;
}

/*
 * Re-pack a double-precision float.  This assumes that the float is
 * already normalised such that the MSB is bit 30, _not_ bit 31.
 */
static inline s64 vfp_double_pack(struct vfp_double *s)
{
	u64 val;
	val = ((u64)s->sign << 48) +
	      ((u64)s->exponent << VFP_DOUBLE_MANTISSA_BITS) +
	      (s->significand >> VFP_DOUBLE_LOW_BITS);
	return (s64)val;
}

static inline int vfp_double_type(struct vfp_double *s)
{
	int type = VFP_NUMBER;
	if (s->exponent == 2047) {
		if (s->significand == 0)
			type = VFP_INFINITY;
		else if (s->significand & VFP_DOUBLE_SIGNIFICAND_QNAN)
			type = VFP_QNAN;
		else
			type = VFP_SNAN;
	} else if (s->exponent == 0) {
		if (s->significand == 0)
			type |= VFP_ZERO;
		else
			type |= VFP_DENORMAL;
	}
	return type;
}

u32 vfp_double_normaliseround(ARMul_State* state, int dd, struct vfp_double *vd, u32 fpscr, u32 exceptions, const char *func);

u32 vfp_estimate_sqrt_significand(u32 exponent, u32 significand);

/*
 * A special flag to tell the normalisation code not to normalise.
 */
#define VFP_NAN_FLAG	0x100

/*
 * A bit pattern used to indicate the initial (unset) value of the
 * exception mask, in case nothing handles an instruction.  This
 * doesn't include the NAN flag, which get masked out before
 * we check for an error.
 */
#define VFP_EXCEPTION_ERROR	((u32)-1 & ~VFP_NAN_FLAG)

/*
 * A flag to tell vfp instruction type.
 *  OP_SCALAR - this operation always operates in scalar mode
 *  OP_SD - the instruction exceptionally writes to a single precision result.
 *  OP_DD - the instruction exceptionally writes to a double precision result.
 *  OP_SM - the instruction exceptionally reads from a single precision operand.
 */
#define OP_SCALAR	(1 << 0)
#define OP_SD		(1 << 1)
#define OP_DD		(1 << 1)
#define OP_SM		(1 << 2)

struct op {
	u32 (* const fn)(ARMul_State* state, int dd, int dn, int dm, u32 fpscr);
	u32 flags;
};

static inline u32 fls(ARMword x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;

}

u32 vfp_double_normaliseroundintern(ARMul_State* state, struct vfp_double *vd, u32 fpscr, u32 exceptions, const char *func);
u32 vfp_double_multiply(struct vfp_double *vdd, struct vfp_double *vdn, struct vfp_double *vdm, u32 fpscr);
u32 vfp_double_add(struct vfp_double *vdd, struct vfp_double *vdn, struct vfp_double *vdm, u32 fpscr);
u32 vfp_double_fcvtsinterncutting(ARMul_State* state, int sd, struct vfp_double* dm, u32 fpscr);
