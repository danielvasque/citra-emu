// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

#include "core/file_sys/disk_archive.h"
#include "core/loader/loader.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FileSys namespace

namespace FileSys {

/// File system interface to the SaveData archive
class Archive_SaveData final : public DiskArchive {
public:
    Archive_SaveData(const std::string& mount_point);

    std::string GetName() const override { return "SaveData"; }

    ResultCode Open(const Path& path) override;

    ResultCode Format(const Path& path) const override;

    const std::string& GetMountPoint() const override {
        return concrete_mount_point;
    }

protected:
    std::string concrete_mount_point;
};

} // namespace FileSys
