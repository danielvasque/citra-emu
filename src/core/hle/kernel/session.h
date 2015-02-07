// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/kernel/kernel.h"
#include "core/mem_map.h"

namespace Kernel {

static const int kCommandHeaderOffset = 0x80; ///< Offset into command buffer of header

/**
 * Returns a pointer to the command buffer in kernel memory
 * @param offset Optional offset into command buffer
 * @return Pointer to command buffer
 */
inline static u32* GetCommandBuffer(const int offset=0) {
    return (u32*)Memory::GetPointer(Memory::KERNEL_MEMORY_VADDR + kCommandHeaderOffset + offset);
}

/**
 * Kernel object representing the client endpoint of an IPC session. Sessions are the basic CTR-OS
 * primitive for communication between different processes, and are used to implement service calls
 * to the various system services.
 *
 * To make a service call, the client must write the command header and parameters to the buffer
 * located at offset 0x80 of the TLS (Thread-Local Storage) area, then execute a SendSyncRequest
 * SVC call with its Session handle. The kernel will read the command header, using it to marshall
 * the parameters to the process at the server endpoint of the session. After the server replies to
 * the request, the response is marshalled back to the caller's TLS buffer and control is
 * transferred back to it.
 *
 * In Citra, only the client endpoint is currently implemented and only HLE calls, where the IPC
 * request is answered by C++ code in the emulator, are supported. When SendSyncRequest is called
 * with the session handle, this class's SyncRequest method is called, which should read the TLS
 * buffer and emulate the call accordingly. Since the code can directly read the emulated memory,
 * no parameter marshalling is done.
 *
 * In the long term, this should be turned into the full-fledged IPC mechanism implemented by
 * CTR-OS so that IPC calls can be optionally handled by the real implementations of processes, as
 * opposed to HLE simulations.
 */
class Session : public WaitObject {
public:
    Session();
    ~Session() override;

    std::string GetTypeName() const override { return "Session"; }

    static const HandleType HANDLE_TYPE = HandleType::Session;
    HandleType GetHandleType() const override { return HANDLE_TYPE; }

    /**
     * Handles a synchronous call to this session using HLE emulation. Emulated <-> emulated calls
     * aren't supported yet.
     */
    virtual ResultVal<bool> SyncRequest() = 0;

    // TODO(bunnei): These functions exist to satisfy a hardware test with a Session object
    // passed into WaitSynchronization. Figure out the meaning of them.

    bool ShouldWait() override {
        return true;
    }

    void Acquire() override {
        _assert_msg_(Kernel, !ShouldWait(), "object unavailable!");
    }
};

}
