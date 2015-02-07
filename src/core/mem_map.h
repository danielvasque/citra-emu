// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common.h"
#include "common/common_types.h"

#include "core/hle/kernel/kernel.h"

namespace Memory {

////////////////////////////////////////////////////////////////////////////////////////////////////

enum : u32 {
    BOOTROM_SIZE                = 0x00010000,   ///< Bootrom (super secret code/data @ 0x8000) size
    BOOTROM_PADDR               = 0x00000000,   ///< Bootrom physical address
    BOOTROM_PADDR_END           = (BOOTROM_PADDR + BOOTROM_SIZE),

    BOOTROM_MIRROR_SIZE         = 0x00010000,   ///< Bootrom Mirror size
    BOOTROM_MIRROR_PADDR        = 0x00010000,   ///< Bootrom Mirror physical address
    BOOTROM_MIRROR_PADDR_END    = (BOOTROM_MIRROR_PADDR + BOOTROM_MIRROR_SIZE),
    
    MPCORE_PRIV_SIZE            = 0x00002000,   ///< MPCore private memory region size
    MPCORE_PRIV_PADDR           = 0x17E00000,   ///< MPCore private memory region physical address
    MPCORE_PRIV_PADDR_END       = (MPCORE_PRIV_PADDR + MPCORE_PRIV_SIZE),

    FCRAM_SIZE                  = 0x08000000,   ///< FCRAM size
    FCRAM_PADDR                 = 0x20000000,                       ///< FCRAM physical address
    FCRAM_PADDR_END             = (FCRAM_PADDR + FCRAM_SIZE),       ///< FCRAM end of physical space
    FCRAM_VADDR                 = 0x08000000,                       ///< FCRAM virtual address
    FCRAM_VADDR_END             = (FCRAM_VADDR + FCRAM_SIZE),       ///< FCRAM end of virtual space
    
    AXI_WRAM_SIZE               = 0x00080000,   ///< AXI WRAM size
    AXI_WRAM_PADDR              = 0x1FF80000,   ///< AXI WRAM physical address
    AXI_WRAM_PADDR_END          = (AXI_WRAM_PADDR + AXI_WRAM_SIZE),

    SHARED_MEMORY_SIZE          = 0x04000000,   ///< Shared memory size
    SHARED_MEMORY_VADDR         = 0x10000000,   ///< Shared memory
    SHARED_MEMORY_VADDR_END     = (SHARED_MEMORY_VADDR + SHARED_MEMORY_SIZE),

    DSP_MEMORY_SIZE             = 0x00080000,   ///< DSP memory size
    DSP_MEMORY_VADDR            = 0x1FF00000,   ///< DSP memory virtual address
    DSP_MEMORY_VADDR_END        = (DSP_MEMORY_VADDR + DSP_MEMORY_SIZE),

    CONFIG_MEMORY_SIZE          = 0x00001000,   ///< Configuration memory size
    CONFIG_MEMORY_VADDR         = 0x1FF80000,   ///< Configuration memory virtual address
    CONFIG_MEMORY_VADDR_END     = (CONFIG_MEMORY_VADDR + CONFIG_MEMORY_SIZE),

    SHARED_PAGE_SIZE            = 0x00001000,   ///< Shared page size
    SHARED_PAGE_VADDR           = 0x1FF81000,   ///< Shared page virtual address
    SHARED_PAGE_VADDR_END       = (SHARED_PAGE_VADDR + SHARED_PAGE_SIZE),

    KERNEL_MEMORY_SIZE          = 0x00001000,   ///< Kernel memory size
    KERNEL_MEMORY_VADDR         = 0xFFFF0000,   ///< Kernel memory where the kthread objects etc are
    KERNEL_MEMORY_VADDR_END     = (KERNEL_MEMORY_VADDR + KERNEL_MEMORY_SIZE),

    EXEFS_CODE_SIZE             = 0x03F00000,
    EXEFS_CODE_VADDR            = 0x00100000,   ///< ExeFS:/.code is loaded here
    EXEFS_CODE_VADDR_END        = (EXEFS_CODE_VADDR + EXEFS_CODE_SIZE),

    // Region of FCRAM used by system
    SYSTEM_MEMORY_SIZE          = 0x02C00000,   ///< 44MB
    SYSTEM_MEMORY_VADDR         = 0x04000000,
    SYSTEM_MEMORY_VADDR_END     = (SYSTEM_MEMORY_VADDR + SYSTEM_MEMORY_SIZE),

    HEAP_SIZE                   = FCRAM_SIZE,   ///< Application heap size
    //HEAP_PADDR                  = HEAP_GSP_SIZE,
    //HEAP_PADDR_END              = (HEAP_PADDR + HEAP_SIZE),
    HEAP_VADDR                  = 0x08000000,
    HEAP_VADDR_END              = (HEAP_VADDR + HEAP_SIZE),

    HEAP_LINEAR_SIZE            = 0x08000000,   ///< Linear heap size... TODO: Define correctly?
    HEAP_LINEAR_VADDR           = 0x14000000,
    HEAP_LINEAR_VADDR_END       = (HEAP_LINEAR_VADDR + HEAP_LINEAR_SIZE),
    HEAP_LINEAR_PADDR           = 0x00000000,
    HEAP_LINEAR_PADDR_END       = (HEAP_LINEAR_PADDR + HEAP_LINEAR_SIZE),

    HARDWARE_IO_SIZE            = 0x01000000,
    HARDWARE_IO_PADDR           = 0x10000000,                       ///< IO physical address start
    HARDWARE_IO_VADDR           = 0x1EC00000,                       ///< IO virtual address start
    HARDWARE_IO_PADDR_END       = (HARDWARE_IO_PADDR + HARDWARE_IO_SIZE),
    HARDWARE_IO_VADDR_END       = (HARDWARE_IO_VADDR + HARDWARE_IO_SIZE),

    VRAM_SIZE                   = 0x00600000,
    VRAM_PADDR                  = 0x18000000,
    VRAM_VADDR                  = 0x1F000000,
    VRAM_PADDR_END              = (VRAM_PADDR + VRAM_SIZE),
    VRAM_VADDR_END              = (VRAM_VADDR + VRAM_SIZE),

    SCRATCHPAD_SIZE             = 0x00004000,   ///< Typical stack size - TODO: Read from exheader
    SCRATCHPAD_VADDR_END        = 0x10000000,
    SCRATCHPAD_VADDR            = (SCRATCHPAD_VADDR_END - SCRATCHPAD_SIZE), ///< Stack space
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/// Represents a block of memory mapped by ControlMemory/MapMemoryBlock
struct MemoryBlock {
    MemoryBlock() : handle(0), base_address(0), address(0), size(0), operation(0), permissions(0) {
    }
    u32 handle;
    u32 base_address;
    u32 address;
    u32 size;
    u32 operation;
    u32 permissions;

    const u32 GetVirtualAddress() const{
        return base_address + address;
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////

// Base is a pointer to the base of the memory map. Yes, some MMU tricks
// are used to set up a full GC or Wii memory map in process memory.  on
// 32-bit, you have to mask your offsets with 0x3FFFFFFF. This means that
// some things are mirrored too many times, but eh... it works.

// In 64-bit, this might point to "high memory" (above the 32-bit limit),
// so be sure to load it into a 64-bit register.
extern u8 *g_base;

// These are guaranteed to point to "low memory" addresses (sub-32-bit).
// 64-bit: Pointers to low-mem (sub-0x10000000) mirror
// 32-bit: Same as the corresponding physical/virtual pointers.
extern u8* g_heap_linear;   ///< Linear heap (main memory)
extern u8* g_heap;          ///< Application heap (main memory)
extern u8* g_vram;          ///< Video memory (VRAM)
extern u8* g_shared_mem;    ///< Shared memory
extern u8* g_kernel_mem;    ///< Kernel memory
extern u8* g_dsp_mem;       ///< DSP memory
extern u8* g_system_mem;    ///< System memory
extern u8* g_exefs_code;    ///< ExeFS:/.code is loaded here

void Init();
void Shutdown();

template <typename T>
inline void Read(T &var, VAddr addr);

template <typename T>
inline void Write(VAddr addr, T data);

u8 Read8(VAddr addr);
u16 Read16(VAddr addr);
u32 Read32(VAddr addr);

u32 Read8_ZX(VAddr addr);
u32 Read16_ZX(VAddr addr);

void Write8(VAddr addr, u8 data);
void Write16(VAddr addr, u16 data);
void Write32(VAddr addr, u32 data);
void Write64(VAddr addr, u64 data);

void WriteBlock(VAddr addr, const u8* data, size_t size);

u8* GetPointer(VAddr virtual_address);

/**
 * Maps a block of memory on the heap
 * @param size Size of block in bytes
 * @param operation Memory map operation type
 * @param permissions Memory allocation permissions
 */
u32 MapBlock_Heap(u32 size, u32 operation, u32 permissions);

/**
 * Maps a block of memory on the GSP heap
 * @param size Size of block in bytes
 * @param operation Memory map operation type
 * @param permissions Control memory permissions
 */
u32 MapBlock_HeapLinear(u32 size, u32 operation, u32 permissions);

inline const char* GetCharPointer(const VAddr address) {
    return (const char *)GetPointer(address);
}

/// Converts a physical address to virtual address
VAddr PhysicalToVirtualAddress(PAddr addr);

/// Converts a virtual address to physical address
PAddr VirtualToPhysicalAddress(VAddr addr);

} // namespace
