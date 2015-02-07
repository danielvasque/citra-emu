// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>

#include "common/common_types.h"
#include "common/bit_field.h"

namespace GPU {

// Returns index corresponding to the Regs member labeled by field_name
// TODO: Due to Visual studio bug 209229, offsetof does not return constant expressions
//       when used with array elements (e.g. GPU_REG_INDEX(memory_fill_config[0])).
//       For details cf. https://connect.microsoft.com/VisualStudio/feedback/details/209229/offsetof-does-not-produce-a-constant-expression-for-array-members
//       Hopefully, this will be fixed sometime in the future.
//       For lack of better alternatives, we currently hardcode the offsets when constant
//       expressions are needed via GPU_REG_INDEX_WORKAROUND (on sane compilers, static_asserts
//       will then make sure the offsets indeed match the automatically calculated ones).
#define GPU_REG_INDEX(field_name) (offsetof(GPU::Regs, field_name) / sizeof(u32))
#if defined(_MSC_VER)
#define GPU_REG_INDEX_WORKAROUND(field_name, backup_workaround_index) (backup_workaround_index)
#else
// NOTE: Yeah, hacking in a static_assert here just to workaround the lacking MSVC compiler
//       really is this annoying. This macro just forwards its first argument to GPU_REG_INDEX
//       and then performs a (no-op) cast to size_t iff the second argument matches the expected
//       field offset. Otherwise, the compiler will fail to compile this code.
#define GPU_REG_INDEX_WORKAROUND(field_name, backup_workaround_index) \
    ((typename std::enable_if<backup_workaround_index == GPU_REG_INDEX(field_name), size_t>::type)GPU_REG_INDEX(field_name))
#endif

// MMIO region 0x1EFxxxxx
struct Regs {

// helper macro to properly align structure members.
// Calling INSERT_PADDING_WORDS will add a new member variable with a name like "pad121",
// depending on the current source line to make sure variable names are unique.
#define INSERT_PADDING_WORDS_HELPER1(x, y) x ## y
#define INSERT_PADDING_WORDS_HELPER2(x, y) INSERT_PADDING_WORDS_HELPER1(x, y)
#define INSERT_PADDING_WORDS(num_words) u32 INSERT_PADDING_WORDS_HELPER2(pad, __LINE__)[(num_words)]

// helper macro to make sure the defined structures are of the expected size.
#if defined(_MSC_VER)
// TODO: MSVC does not support using sizeof() on non-static data members even though this
//       is technically allowed since C++11. This macro should be enabled once MSVC adds
//       support for that.
#define ASSERT_MEMBER_SIZE(name, size_in_bytes)
#else
#define ASSERT_MEMBER_SIZE(name, size_in_bytes)  \
    static_assert(sizeof(name) == size_in_bytes, \
                  "Structure size and register block length don't match")
#endif

    enum class PixelFormat : u32 {
        RGBA8  = 0,
        RGB8   = 1,
        RGB565 = 2,
        RGB5A1 = 3,
        RGBA4  = 4,
    };

    INSERT_PADDING_WORDS(0x4);

    struct {
        u32 address_start;
        u32 address_end; // ?
        u32 size;
        u32 value; // ?

        inline u32 GetStartAddress() const {
            return DecodeAddressRegister(address_start);
        }

        inline u32 GetEndAddress() const {
            return DecodeAddressRegister(address_end);
        }
    } memory_fill_config[2];
    ASSERT_MEMBER_SIZE(memory_fill_config[0], 0x10);

    INSERT_PADDING_WORDS(0x10b);

    struct FramebufferConfig {
        union {
            u32 size;

            BitField< 0, 16, u32> width;
            BitField<16, 16, u32> height;
        };

        INSERT_PADDING_WORDS(0x2);

        u32 address_left1;
        u32 address_left2;

        union {
            u32 format;

            BitField< 0, 3, PixelFormat> color_format;
        };

        INSERT_PADDING_WORDS(0x1);

        union {
            u32 active_fb;

            // 0: Use parameters ending with "1"
            // 1: Use parameters ending with "2"
            BitField<0, 1, u32> second_fb_active;
        };

        INSERT_PADDING_WORDS(0x5);

        // Distance between two pixel rows, in bytes
        u32 stride;

        u32 address_right1;
        u32 address_right2;

        INSERT_PADDING_WORDS(0x30);
    } framebuffer_config[2];
    ASSERT_MEMBER_SIZE(framebuffer_config[0], 0x100);

    INSERT_PADDING_WORDS(0x169);

    struct {
        u32 input_address;
        u32 output_address;

        inline u32 GetPhysicalInputAddress() const {
            return DecodeAddressRegister(input_address);
        }

        inline u32 GetPhysicalOutputAddress() const {
            return DecodeAddressRegister(output_address);
        }

        union {
            u32 output_size;

            BitField< 0, 16, u32> output_width;
            BitField<16, 16, u32> output_height;
        };

        union {
            u32 input_size;

            BitField< 0, 16, u32> input_width;
            BitField<16, 16, u32> input_height;
        };

        union {
            u32 flags;

            BitField< 0, 1, u32> flip_data;        // flips input data horizontally (TODO) if true
            BitField< 8, 3, PixelFormat> input_format;
            BitField<12, 3, PixelFormat> output_format;
            BitField<16, 1, u32> output_tiled;     // stores output in a tiled format

            // TODO: Not really sure if this actually scales, or even resizes at all.
            BitField<24, 1, u32> scale_horizontally;
        };

        INSERT_PADDING_WORDS(0x1);

        // it seems that writing to this field triggers the display transfer
        u32 trigger;
    } display_transfer_config;
    ASSERT_MEMBER_SIZE(display_transfer_config, 0x1c);

    INSERT_PADDING_WORDS(0x331);

    struct {
        // command list size (in bytes)
        u32 size;

        INSERT_PADDING_WORDS(0x1);

        // command list address
        u32 address;

        INSERT_PADDING_WORDS(0x1);

        // it seems that writing to this field triggers command list processing
        u32 trigger;

        inline u32 GetPhysicalAddress() const {
            return DecodeAddressRegister(address);
        }
    } command_processor_config;
    ASSERT_MEMBER_SIZE(command_processor_config, 0x14);

    INSERT_PADDING_WORDS(0x9c3);

#undef INSERT_PADDING_WORDS_HELPER1
#undef INSERT_PADDING_WORDS_HELPER2
#undef INSERT_PADDING_WORDS

    static inline size_t NumIds() {
        return sizeof(Regs) / sizeof(u32);
    }

    u32& operator [] (int index) const {
        u32* content = (u32*)this;
        return content[index];
    }

    u32& operator [] (int index) {
        u32* content = (u32*)this;
        return content[index];
    }

private:
    /*
     * Most physical addresses which GPU registers refer to are 8-byte aligned.
     * This function should be used to get the address from a raw register value.
     */
    static inline u32 DecodeAddressRegister(u32 register_value) {
        return register_value * 8;
    }
};
static_assert(std::is_standard_layout<Regs>::value, "Structure does not use standard layout");

// TODO: MSVC does not support using offsetof() on non-static data members even though this
//       is technically allowed since C++11. This macro should be enabled once MSVC adds
//       support for that.
#ifndef _MSC_VER
#define ASSERT_REG_POSITION(field_name, position)             \
    static_assert(offsetof(Regs, field_name) == position * 4, \
                  "Field "#field_name" has invalid position")

ASSERT_REG_POSITION(memory_fill_config[0],    0x00004);
ASSERT_REG_POSITION(memory_fill_config[1],    0x00008);
ASSERT_REG_POSITION(framebuffer_config[0],    0x00117);
ASSERT_REG_POSITION(framebuffer_config[1],    0x00157);
ASSERT_REG_POSITION(display_transfer_config,  0x00300);
ASSERT_REG_POSITION(command_processor_config, 0x00638);

#undef ASSERT_REG_POSITION
#endif // !defined(_MSC_VER)

// The total number of registers is chosen arbitrarily, but let's make sure it's not some odd value anyway.
static_assert(sizeof(Regs) == 0x1000 * sizeof(u32), "Invalid total size of register set");

extern Regs g_regs;
extern bool g_skip_frame;

template <typename T>
void Read(T &var, const u32 addr);

template <typename T>
void Write(u32 addr, const T data);

/// Initialize hardware
void Init();

/// Shutdown hardware
void Shutdown();


} // namespace
