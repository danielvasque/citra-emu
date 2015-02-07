// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>

#include "core/hle/kernel/kernel.h"
#include "common/bit_field.h"

namespace Kernel {
    class SharedMemory;
    class Event;
}

namespace Service {
namespace HID {

// Handle to shared memory region designated to HID_User service
extern Kernel::SharedPtr<Kernel::SharedMemory> g_shared_mem;

// Event handles
extern Kernel::SharedPtr<Kernel::Event> g_event_pad_or_touch_1;
extern Kernel::SharedPtr<Kernel::Event> g_event_pad_or_touch_2;
extern Kernel::SharedPtr<Kernel::Event> g_event_accelerometer;
extern Kernel::SharedPtr<Kernel::Event> g_event_gyroscope;
extern Kernel::SharedPtr<Kernel::Event> g_event_debug_pad;

/**
 * Structure of a Pad controller state.
 */
struct PadState {
    union {
        u32 hex;

        BitField<0, 1, u32> a;
        BitField<1, 1, u32> b;
        BitField<2, 1, u32> select;
        BitField<3, 1, u32> start;
        BitField<4, 1, u32> right;
        BitField<5, 1, u32> left;
        BitField<6, 1, u32> up;
        BitField<7, 1, u32> down;
        BitField<8, 1, u32> r;
        BitField<9, 1, u32> l;
        BitField<10, 1, u32> x;
        BitField<11, 1, u32> y;

        BitField<28, 1, u32> circle_right;
        BitField<29, 1, u32> circle_left;
        BitField<30, 1, u32> circle_up;
        BitField<31, 1, u32> circle_down;
    };
};

/**
 * Structure of a single entry in the PadData's Pad state history array.
 */
struct PadDataEntry {
    PadState current_state;
    PadState delta_additions;
    PadState delta_removals;

    s16 circle_pad_x;
    s16 circle_pad_y;
};

/**
 * Structure of all data related to the 3DS Pad.
 */
struct PadData {
    s64 index_reset_ticks;
    s64 index_reset_ticks_previous;
    u32 index; // the index of the last updated Pad state history element

    u32 pad1;
    u32 pad2;

    PadState current_state; // same as entries[index].current_state
    u32 raw_circle_pad_data;

    u32 pad3;

    std::array<PadDataEntry, 8> entries; // Pad state history
};

// Pre-defined PadStates for single button presses
const PadState PAD_NONE         = {{0}};
const PadState PAD_A            = {{1u << 0}};
const PadState PAD_B            = {{1u << 1}};
const PadState PAD_SELECT       = {{1u << 2}};
const PadState PAD_START        = {{1u << 3}};
const PadState PAD_RIGHT        = {{1u << 4}};
const PadState PAD_LEFT         = {{1u << 5}};
const PadState PAD_UP           = {{1u << 6}};
const PadState PAD_DOWN         = {{1u << 7}};
const PadState PAD_R            = {{1u << 8}};
const PadState PAD_L            = {{1u << 9}};
const PadState PAD_X            = {{1u << 10}};
const PadState PAD_Y            = {{1u << 11}};
const PadState PAD_CIRCLE_RIGHT = {{1u << 28}};
const PadState PAD_CIRCLE_LEFT  = {{1u << 29}};
const PadState PAD_CIRCLE_UP    = {{1u << 30}};
const PadState PAD_CIRCLE_DOWN  = {{1u << 31}};

// Methods for updating the HID module's state
void PadButtonPress(const PadState& pad_state);
void PadButtonRelease(const PadState& pad_state);
void PadUpdateComplete();

void HIDInit();
void HIDShutdown();

}
}
