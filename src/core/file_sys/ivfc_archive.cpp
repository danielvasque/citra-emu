// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>

#include "common/common_types.h"
#include "common/file_util.h"
#include "common/make_unique.h"

#include "core/file_sys/ivfc_archive.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FileSys namespace

namespace FileSys {

IVFCArchive::IVFCArchive() {
}

std::unique_ptr<FileBackend> IVFCArchive::OpenFile(const Path& path, const Mode mode) const {
    return Common::make_unique<IVFCFile>(this);
}

bool IVFCArchive::DeleteFile(const Path& path) const {
    LOG_CRITICAL(Service_FS, "Attempted to delete a file from an IVFC archive (%s).", GetName().c_str());
    return false;
}

bool IVFCArchive::RenameFile(const Path& src_path, const Path& dest_path) const {
    LOG_CRITICAL(Service_FS, "Attempted to rename a file within an IVFC archive (%s).", GetName().c_str());
    return false;
}

bool IVFCArchive::DeleteDirectory(const Path& path) const {
    LOG_CRITICAL(Service_FS, "Attempted to delete a directory from an IVFC archive (%s).", GetName().c_str());
    return false;
}

ResultCode IVFCArchive::CreateFile(const Path& path, u32 size) const {
    LOG_CRITICAL(Service_FS, "Attempted to create a file in an IVFC archive (%s).", GetName().c_str());
    // TODO: Verify error code
    return ResultCode(ErrorDescription::NotAuthorized, ErrorModule::FS, ErrorSummary::NotSupported, ErrorLevel::Permanent);
}

bool IVFCArchive::CreateDirectory(const Path& path) const {
    LOG_CRITICAL(Service_FS, "Attempted to create a directory in an IVFC archive (%s).", GetName().c_str());
    return false;
}

bool IVFCArchive::RenameDirectory(const Path& src_path, const Path& dest_path) const {
    LOG_CRITICAL(Service_FS, "Attempted to rename a file within an IVFC archive (%s).", GetName().c_str());
    return false;
}

std::unique_ptr<DirectoryBackend> IVFCArchive::OpenDirectory(const Path& path) const {
    return Common::make_unique<IVFCDirectory>();
}

ResultCode IVFCArchive::Format(const Path& path) const {
    LOG_CRITICAL(Service_FS, "Attempted to format an IVFC archive (%s).", GetName().c_str());
    // TODO: Verify error code
    return ResultCode(ErrorDescription::NotAuthorized, ErrorModule::FS, ErrorSummary::NotSupported, ErrorLevel::Permanent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

size_t IVFCFile::Read(const u64 offset, const u32 length, u8* buffer) const {
    LOG_TRACE(Service_FS, "called offset=%llu, length=%d", offset, length);
    memcpy(buffer, &archive->raw_data[(u32)offset], length);
    return length;
}

size_t IVFCFile::Write(const u64 offset, const u32 length, const u32 flush, const u8* buffer) const {
    LOG_CRITICAL(Service_FS, "Attempted to write to IVFC file in archive %s.", archive->GetName().c_str());
    return 0;
}

size_t IVFCFile::GetSize() const {
    return sizeof(u8) * archive->raw_data.size();
}

bool IVFCFile::SetSize(const u64 size) const {
    LOG_CRITICAL(Service_FS, "Attempted to set the size of an IVFC file in archive %s", archive->GetName().c_str());
    return false;
}

} // namespace FileSys
