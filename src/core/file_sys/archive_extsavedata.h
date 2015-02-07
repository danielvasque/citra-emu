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

/// File system interface to the ExtSaveData archive
class Archive_ExtSaveData final : public DiskArchive {
public:
    Archive_ExtSaveData(const std::string& mount_point, bool shared);

    /**
     * Initialize the archive.
     * @return true if it initialized successfully
     */
    bool Initialize();

    ResultCode Open(const Path& path) override;
    ResultCode Format(const Path& path) const override;
    std::string GetName() const override { return "ExtSaveData"; }

    const std::string& GetMountPoint() const override {
        return concrete_mount_point;
    }

protected:
    /**
     * This holds the full directory path for this archive, it is only set after a successful call to Open, 
     * this is formed as <base extsavedatapath>/<type>/<high>/<low>. 
     * See GetExtSaveDataPath for the code that extracts this data from an archive path.
     */
    std::string concrete_mount_point;
};

/**
 * Constructs a path to the concrete ExtData archive in the host filesystem based on the 
 * input Path and base mount point.
 * @param mount_point The base mount point of the ExtSaveData archives.
 * @param path The path that identifies the requested concrete ExtSaveData archive.
 * @returns The complete path to the specified extdata archive in the host filesystem
 */
std::string GetExtSaveDataPath(const std::string& mount_point, const Path& path);

/**
 * Constructs a path to the base folder to hold concrete ExtSaveData archives in the host file system.
 * @param mount_point The base folder where this folder resides, ie. SDMC or NAND.
 * @param shared Whether this ExtSaveData container is for SharedExtSaveDatas or not.
 * @returns The path to the base ExtSaveData archives' folder in the host file system
 */
std::string GetExtDataContainerPath(const std::string& mount_point, bool shared);

} // namespace FileSys
