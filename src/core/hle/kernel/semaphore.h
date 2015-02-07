// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <queue>
#include <string>

#include "common/common_types.h"

#include "core/hle/kernel/kernel.h"

namespace Kernel {

class Semaphore final : public WaitObject {
public:
    /**
     * Creates a semaphore.
     * @param handle Pointer to the handle of the newly created object
     * @param initial_count Number of slots reserved for other threads
     * @param max_count Maximum number of slots the semaphore can have
     * @param name Optional name of semaphore
     * @return The created semaphore
     */
    static ResultVal<SharedPtr<Semaphore>> Create(s32 initial_count, s32 max_count,
            std::string name = "Unknown");

    std::string GetTypeName() const override { return "Semaphore"; }
    std::string GetName() const override { return name; }

    static const HandleType HANDLE_TYPE = HandleType::Semaphore;
    HandleType GetHandleType() const override { return HANDLE_TYPE; }

    s32 max_count;                              ///< Maximum number of simultaneous holders the semaphore can have
    s32 available_count;                        ///< Number of free slots left in the semaphore
    std::string name;                           ///< Name of semaphore (optional)

    bool ShouldWait() override;
    void Acquire() override;

    /**
     * Releases a certain number of slots from a semaphore.
     * @param release_count The number of slots to release
     * @return The number of free slots the semaphore had before this call
     */
    ResultVal<s32> Release(s32 release_count);

private:
    Semaphore();
    ~Semaphore() override;
};

} // namespace
