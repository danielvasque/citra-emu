// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common.h"
#include <string>

namespace Common
{
class Timer
{
public:
    Timer();

    void Start();
    void Stop();
    void Update();

    // The time difference is always returned in milliseconds, regardless of alternative internal representation
    u64 GetTimeDifference();
    void AddTimeDifference();

    static void IncreaseResolution();
    static void RestoreResolution();
    static u64 GetTimeSinceJan1970();
    static u64 GetLocalTimeSinceJan1970();
    static double GetDoubleTime();

    static std::string GetTimeFormatted();
    std::string GetTimeElapsedFormatted() const;
    u64 GetTimeElapsed();

    static u32 GetTimeMs();

private:
    u64 m_LastTime;
    u64 m_StartTime;
    bool m_Running;
};

} // Namespace Common
