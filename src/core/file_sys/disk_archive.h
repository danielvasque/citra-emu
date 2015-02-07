// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "common/file_util.h"

#include "core/file_sys/archive_backend.h"
#include "core/loader/loader.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FileSys namespace

namespace FileSys {

/**
 * Helper which implements a backend accessing the host machine's filesystem.
 * This should be subclassed by concrete archive types, which will provide the
 * base directory on the host filesystem and override any required functionality.
 */
class DiskArchive : public ArchiveBackend {
public:
    DiskArchive(const std::string& mount_point_) : mount_point(mount_point_) {}

    virtual std::string GetName() const = 0;
    virtual ResultCode Format(const Path& path) const { return RESULT_SUCCESS; }
    std::unique_ptr<FileBackend> OpenFile(const Path& path, const Mode mode) const override;
    bool DeleteFile(const Path& path) const override;
    bool RenameFile(const Path& src_path, const Path& dest_path) const override;
    bool DeleteDirectory(const Path& path) const override;
    ResultCode CreateFile(const Path& path, u32 size) const override;
    bool CreateDirectory(const Path& path) const override;
    bool RenameDirectory(const Path& src_path, const Path& dest_path) const override;
    std::unique_ptr<DirectoryBackend> OpenDirectory(const Path& path) const override;

    virtual ResultCode Open(const Path& path) override {
        return RESULT_SUCCESS;
    }

    /**
     * Getter for the path used for this Archive
     * @return Mount point of that passthrough archive
     */
    virtual const std::string& GetMountPoint() const {
        return mount_point;
    }

protected:
    std::string mount_point;
};

class DiskFile : public FileBackend {
public:
    DiskFile();
    DiskFile(const DiskArchive* archive, const Path& path, const Mode mode);

    bool Open() override;
    size_t Read(const u64 offset, const u32 length, u8* buffer) const override;
    size_t Write(const u64 offset, const u32 length, const u32 flush, const u8* buffer) const override;
    size_t GetSize() const override;
    bool SetSize(const u64 size) const override;
    bool Close() const override;

    void Flush() const override {
        file->Flush();
    }

protected:
    const DiskArchive* archive;
    std::string path;
    Mode mode;
    std::unique_ptr<FileUtil::IOFile> file;
};

class DiskDirectory : public DirectoryBackend {
public:
    DiskDirectory();
    DiskDirectory(const DiskArchive* archive, const Path& path);

    ~DiskDirectory() override {
        Close();
    }

    bool Open() override;
    u32 Read(const u32 count, Entry* entries) override;

    bool Close() const override {
        return true;
    }

protected:
    const DiskArchive* archive;
    std::string path;
    u32 total_entries_in_directory;
    FileUtil::FSTEntry directory;

    // We need to remember the last entry we returned, so a subsequent call to Read will continue
    // from the next one.  This iterator will always point to the next unread entry.
    std::vector<FileUtil::FSTEntry>::iterator children_iterator;
};

} // namespace FileSys
