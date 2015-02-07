// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/log.h"
#include "core/hle/hle.h"
#include "core/hle/service/ac_u.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Namespace AC_U

namespace AC_U {

/**
 * AC_U::GetWifiStatus service function
 *  Outputs:
 *      1 : Result of function, 0 on success, otherwise error code
 *      2 : Output connection type, 0 = none, 1 = Old3DS Internet, 2 = New3DS Internet.
 */
void GetWifiStatus(Service::Interface* self) {
    u32* cmd_buff = Kernel::GetCommandBuffer();

    // TODO(purpasmart96): This function is only a stub,
    // it returns a valid result without implementing full functionality.

    cmd_buff[1] = 0; // No error
    cmd_buff[2] = 0; // Connection type set to none

    LOG_WARNING(Service_AC, "(STUBBED) called");
}

const Interface::FunctionInfo FunctionTable[] = {
    {0x00010000, nullptr,               "CreateDefaultConfig"},
    {0x00040006, nullptr,               "ConnectAsync"},
    {0x00050002, nullptr,               "GetConnectResult"},
    {0x00080004, nullptr,               "CloseAsync"},
    {0x00090002, nullptr,               "GetCloseResult"},
    {0x000A0000, nullptr,               "GetLastErrorCode"},
    {0x000D0000, GetWifiStatus,         "GetWifiStatus"},
    {0x000E0042, nullptr,               "GetCurrentAPInfo"},
    {0x00100042, nullptr,               "GetCurrentNZoneInfo"},
    {0x00110042, nullptr,               "GetNZoneApNumService"},
    {0x00240042, nullptr,               "AddDenyApType"},
    {0x00270002, nullptr,               "GetInfraPriority"},
    {0x002D0082, nullptr,               "SetRequestEulaVersion"},
    {0x00300004, nullptr,               "RegisterDisconnectEvent"},
    {0x003C0042, nullptr,               "GetAPSSIDList"},
    {0x003E0042, nullptr,               "IsConnected"},
    {0x00400042, nullptr,               "SetClientVersion"},
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// Interface class

Interface::Interface() {
    Register(FunctionTable);
}

} // namespace
