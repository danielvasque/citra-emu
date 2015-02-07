/*
    armmmu.c - Memory Management Unit emulation.
    ARMulator extensions for the ARM7100 family.
    Copyright (C) 1999  Ben Williamson

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

// Register numbers in the MMU
enum
{
	MMU_ID = 0,
	MMU_CONTROL = 1,
	MMU_TRANSLATION_TABLE_BASE = 2,
	MMU_DOMAIN_ACCESS_CONTROL = 3,
	MMU_FAULT_STATUS = 5,
	MMU_FAULT_ADDRESS = 6,
	MMU_CACHE_OPS = 7,
	MMU_TLB_OPS = 8,
	MMU_CACHE_LOCKDOWN = 9,
	MMU_TLB_LOCKDOWN = 10,
	MMU_PID = 13,

	// MMU_V4
	MMU_V4_CACHE_OPS = 7,
	MMU_V4_TLB_OPS = 8,

	// MMU_V3
	MMU_V3_FLUSH_TLB = 5,
	MMU_V3_FLUSH_TLB_ENTRY = 6,
	MMU_V3_FLUSH_CACHE = 7,

	// MMU Intel SA-1100
	MMU_SA_RB_OPS = 9,
	MMU_SA_DEBUG = 14,
	MMU_SA_CP15_R15 = 15,

	// Intel xscale CP15
	XSCALE_CP15_CACHE_TYPE = 0,
	XSCALE_CP15_AUX_CONTROL = 1,
	XSCALE_CP15_COPRO_ACCESS = 15,
};
