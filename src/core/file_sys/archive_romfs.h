// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>

#include "common/common_types.h"

#include "core/file_sys/ivfc_archive.h"
#include "core/loader/loader.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FileSys namespace

namespace FileSys {

/// File system interface to the RomFS archive
class Archive_RomFS final : public IVFCArchive {
public:
    Archive_RomFS(const Loader::AppLoader& app_loader);

    std::string GetName() const override { return "RomFS"; }
    ResultCode Open(const Path& path) override { return RESULT_SUCCESS; }
};

} // namespace FileSys
