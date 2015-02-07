// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/common.h"

#include "core/core_timing.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/timer.h"
#include "core/hle/kernel/thread.h"

namespace Kernel {

/// The event type of the generic timer callback event
static int timer_callback_event_type = -1;
// TODO(yuriks): This can be removed if Timer objects are explicitly pooled in the future, allowing
//               us to simply use a pool index or similar.
static Kernel::HandleTable timer_callback_handle_table;

Timer::Timer() {}
Timer::~Timer() {}

SharedPtr<Timer> Timer::Create(ResetType reset_type, std::string name) {
    SharedPtr<Timer> timer(new Timer);

    timer->reset_type = reset_type;
    timer->signaled = false;
    timer->name = std::move(name);
    timer->initial_delay = 0;
    timer->interval_delay = 0;
    timer->callback_handle = timer_callback_handle_table.Create(timer).MoveFrom();

    return timer;
}

bool Timer::ShouldWait() {
    return !signaled;
}

void Timer::Acquire() {
    _assert_msg_(Kernel, !ShouldWait(), "object unavailable!");
}

void Timer::Set(s64 initial, s64 interval) {
    // Ensure we get rid of any previous scheduled event
    Cancel();

    initial_delay = initial;
    interval_delay = interval;

    u64 initial_microseconds = initial / 1000;
    CoreTiming::ScheduleEvent(usToCycles(initial_microseconds),
            timer_callback_event_type, callback_handle);
}

void Timer::Cancel() {
    CoreTiming::UnscheduleEvent(timer_callback_event_type, callback_handle);
}

void Timer::Clear() {
    signaled = false;
}

/// The timer callback event, called when a timer is fired
static void TimerCallback(u64 timer_handle, int cycles_late) {
    SharedPtr<Timer> timer = timer_callback_handle_table.Get<Timer>(timer_handle);

    if (timer == nullptr) {
        LOG_CRITICAL(Kernel, "Callback fired for invalid timer %08X", timer_handle);
        return;
    }

    LOG_TRACE(Kernel, "Timer %u fired", timer_handle);

    timer->signaled = true;

    // Resume all waiting threads
    timer->WakeupAllWaitingThreads();

    if (timer->reset_type == RESETTYPE_ONESHOT)
        timer->signaled = false;

    if (timer->interval_delay != 0) {
        // Reschedule the timer with the interval delay
        u64 interval_microseconds = timer->interval_delay / 1000;
        CoreTiming::ScheduleEvent(usToCycles(interval_microseconds) - cycles_late, 
                timer_callback_event_type, timer_handle);
    }
}

void TimersInit() {
    timer_callback_event_type = CoreTiming::RegisterEvent("TimerCallback", TimerCallback);
}

void TimersShutdown() {
}

} // namespace
