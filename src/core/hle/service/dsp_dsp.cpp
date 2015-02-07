// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/log.h"
#include "core/hle/hle.h"
#include "core/hle/kernel/event.h"
#include "core/hle/service/dsp_dsp.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Namespace DSP_DSP

namespace DSP_DSP {

static u32 read_pipe_count    = 0;
static Kernel::SharedPtr<Kernel::Event> semaphore_event;
static Kernel::SharedPtr<Kernel::Event> interrupt_event;

void SignalInterrupt() {
    // TODO(bunnei): This is just a stub, it does not do anything other than signal to the emulated
    // application that a DSP interrupt occurred, without specifying which one. Since we do not
    // emulate the DSP yet (and how it works is largely unknown), this is a work around to get games
    // that check the DSP interrupt signal event to run. We should figure out the different types of
    // DSP interrupts, and trigger them at the appropriate times.

    if (interrupt_event != 0)
        interrupt_event->Signal();
}

/**
 * DSP_DSP::ConvertProcessAddressFromDspDram service function
 *  Inputs:
 *      1 : Address
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      2 : (inaddr << 1) + 0x1FF40000 (where 0x1FF00000 is the DSP RAM address)
 */
void ConvertProcessAddressFromDspDram(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 addr = cmd_buff[1];

    cmd_buff[1] = 0; // No error
    cmd_buff[2] = (addr << 1) + (Memory::DSP_MEMORY_VADDR + 0x40000);

    LOG_WARNING(Service_DSP, "(STUBBED) called with address %u", addr);
}

/**
 * DSP_DSP::LoadComponent service function
 *  Inputs:
 *      1 : Size
 *      2 : Unknown (observed only half word used)
 *      3 : Unknown (observed only half word used)
 *      4 : (size << 4) | 0xA
 *      5 : Buffer address
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      2 : Component loaded, 0 on not loaded, 1 on loaded
 */
void LoadComponent(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    cmd_buff[1] = 0; // No error
    cmd_buff[2] = 1; // Pretend that we actually loaded the DSP firmware

    // TODO(bunnei): Implement real DSP firmware loading

    LOG_WARNING(Service_DSP, "(STUBBED) called");
}

/**
 * DSP_DSP::GetSemaphoreEventHandle service function
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      3 : Semaphore event handle
 */
void GetSemaphoreEventHandle(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    cmd_buff[1] = RESULT_SUCCESS.raw; // No error
    cmd_buff[3] = Kernel::g_handle_table.Create(semaphore_event).MoveFrom(); // Event handle

    LOG_WARNING(Service_DSP, "(STUBBED) called");
}

/**
 * DSP_DSP::RegisterInterruptEvents service function
 *  Inputs:
 *      1 : Parameter 0 (purpose unknown)
 *      2 : Parameter 1 (purpose unknown)
 *      4 : Interrupt event handle
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 */
void RegisterInterruptEvents(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    auto evt = Kernel::g_handle_table.Get<Kernel::Event>(cmd_buff[4]);
    if (evt != nullptr) {
        interrupt_event = evt;
        cmd_buff[1] = 0; // No error
    } else {
        LOG_ERROR(Service_DSP, "called with invalid handle=%08X", cmd_buff[4]);

        // TODO(yuriks): An error should be returned from SendSyncRequest, not in the cmdbuf
        cmd_buff[1] = -1;
    }

    LOG_WARNING(Service_DSP, "(STUBBED) called");
}

/**
 * DSP_DSP::WriteReg0x10 service function
 *  Inputs:
 *      1 : Unknown (observed only half word used)
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 */
void WriteReg0x10(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    SignalInterrupt();

    cmd_buff[1] = 0; // No error

    LOG_WARNING(Service_DSP, "(STUBBED) called");
}

/**
 * DSP_DSP::ReadPipeIfPossible service function
 *  Inputs:
 *      1 : Unknown
 *      2 : Unknown
 *      3 : Size in bytes of read (observed only lower half word used)
 *      0x41 : Virtual address to read from DSP pipe to in memory
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      2 : Number of bytes read from pipe
 */
void ReadPipeIfPossible(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    u32 size = cmd_buff[3] & 0xFFFF;// Lower 16 bits are size
    VAddr addr = cmd_buff[0x41];

    // Canned DSP responses that games expect. These were taken from HW by 3dmoo team.
    // TODO: Remove this hack :)
    static const std::array<u16, 16> canned_read_pipe = {
        0x000F, 0xBFFF, 0x9E8E, 0x8680, 0xA78E, 0x9430, 0x8400, 0x8540,
        0x948E, 0x8710, 0x8410, 0xA90E, 0xAA0E, 0xAACE, 0xAC4E, 0xAC58
    };

    u32 initial_size = read_pipe_count;

    for (unsigned offset = 0; offset < size; offset += sizeof(u16)) {
        if (read_pipe_count < canned_read_pipe.size()) {
            Memory::Write16(addr + offset, canned_read_pipe[read_pipe_count]);
            read_pipe_count++;
        } else {
            LOG_ERROR(Service_DSP, "canned read pipe log exceeded!");
            break;
        }
    }

    cmd_buff[1] = 0; // No error
    cmd_buff[2] = (read_pipe_count - initial_size) * sizeof(u16);

    LOG_WARNING(Service_DSP, "(STUBBED) called size=0x%08X, buffer=0x%08X", size, addr);
}

const Interface::FunctionInfo FunctionTable[] = {
    {0x00010040, nullptr,                          "RecvData"},
    {0x00020040, nullptr,                          "RecvDataIsReady"},
    {0x00030080, nullptr,                          "SendData"},
    {0x00040040, nullptr,                          "SendDataIsEmpty"},
    {0x00070040, WriteReg0x10,                     "WriteReg0x10"},
    {0x00080000, nullptr,                          "GetSemaphore"},
    {0x00090040, nullptr,                          "ClearSemaphore"},
    {0x000B0000, nullptr,                          "CheckSemaphoreRequest"},
    {0x000C0040, ConvertProcessAddressFromDspDram, "ConvertProcessAddressFromDspDram"},
    {0x000D0082, nullptr,                          "WriteProcessPipe"},
    {0x001000C0, ReadPipeIfPossible,               "ReadPipeIfPossible"},
    {0x001100C2, LoadComponent,                    "LoadComponent"},
    {0x00120000, nullptr,                          "UnloadComponent"},
    {0x00130082, nullptr,                          "FlushDataCache"},
    {0x00140082, nullptr,                          "InvalidateDCache"},
    {0x00150082, RegisterInterruptEvents,          "RegisterInterruptEvents"},
    {0x00160000, GetSemaphoreEventHandle,          "GetSemaphoreEventHandle"},
    {0x00170040, nullptr,                          "SetSemaphoreMask"},
    {0x00180040, nullptr,                          "GetPhysicalAddress"},
    {0x00190040, nullptr,                          "GetVirtualAddress"},
    {0x001A0042, nullptr,                          "SetIirFilterI2S1_cmd1"},
    {0x001B0042, nullptr,                          "SetIirFilterI2S1_cmd2"},
    {0x001C0082, nullptr,                          "SetIirFilterEQ"},
    {0x001F0000, nullptr,                          "GetHeadphoneStatus"},
    {0x00210000, nullptr,                          "GetIsDspOccupied"},
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Interface class

Interface::Interface() {
    semaphore_event = Kernel::Event::Create(RESETTYPE_ONESHOT, "DSP_DSP::semaphore_event");
    interrupt_event = nullptr;
    read_pipe_count = 0;

    Register(FunctionTable);
}

} // namespace
