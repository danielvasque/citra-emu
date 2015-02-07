// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

#include "core/file_sys/archive_backend.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/result.h"

/// The unique system identifier hash, also known as ID0
extern const std::string SYSTEM_ID;
/// The scrambled SD card CID, also known as ID1
extern const std::string SDCARD_ID;

namespace Kernel {
    class Session;
}

namespace Service {
namespace FS {

/// Supported archive types
enum class ArchiveIdCode : u32 {
    RomFS               = 0x00000003,
    SaveData            = 0x00000004,
    ExtSaveData         = 0x00000006,
    SharedExtSaveData   = 0x00000007,
    SystemSaveData      = 0x00000008,
    SDMC                = 0x00000009,
    SDMCWriteOnly       = 0x0000000A,
    SaveDataCheck       = 0x2345678A,
};

typedef u64 ArchiveHandle;

/**
 * Opens an archive
 * @param id_code IdCode of the archive to open
 * @param archive_path Path to the archive, used with Binary paths
 * @return Handle to the opened archive
 */
ResultVal<ArchiveHandle> OpenArchive(ArchiveIdCode id_code, FileSys::Path& archive_path);

/**
 * Closes an archive
 * @param handle Handle to the archive to close
 */
ResultCode CloseArchive(ArchiveHandle handle);

/**
 * Creates an Archive
 * @param backend File system backend interface to the archive
 * @param id_code Id code used to access this type of archive
 */
ResultCode CreateArchive(std::unique_ptr<FileSys::ArchiveBackend>&& backend, ArchiveIdCode id_code);

/**
 * Open a File from an Archive
 * @param archive_handle Handle to an open Archive object
 * @param path Path to the File inside of the Archive
 * @param mode Mode under which to open the File
 * @return The opened File object as a Session
 */
ResultVal<Kernel::SharedPtr<Kernel::Session>> OpenFileFromArchive(ArchiveHandle archive_handle,
        const FileSys::Path& path, const FileSys::Mode mode);

/**
 * Delete a File from an Archive
 * @param archive_handle Handle to an open Archive object
 * @param path Path to the File inside of the Archive
 * @return Whether deletion succeeded
 */
ResultCode DeleteFileFromArchive(ArchiveHandle archive_handle, const FileSys::Path& path);

/**
 * Rename a File between two Archives
 * @param src_archive_handle Handle to the source Archive object
 * @param src_path Path to the File inside of the source Archive
 * @param dest_archive_handle Handle to the destination Archive object
 * @param dest_path Path to the File inside of the destination Archive
 * @return Whether rename succeeded
 */
ResultCode RenameFileBetweenArchives(ArchiveHandle src_archive_handle, const FileSys::Path& src_path,
                                     ArchiveHandle dest_archive_handle, const FileSys::Path& dest_path);

/**
 * Delete a Directory from an Archive
 * @param archive_handle Handle to an open Archive object
 * @param path Path to the Directory inside of the Archive
 * @return Whether deletion succeeded
 */
ResultCode DeleteDirectoryFromArchive(ArchiveHandle archive_handle, const FileSys::Path& path);

/**
 * Create a File in an Archive
 * @param archive_handle Handle to an open Archive object
 * @param path Path to the File inside of the Archive
 * @param file_size The size of the new file, filled with zeroes
 * @return File creation result code
 */
ResultCode CreateFileInArchive(ArchiveHandle archive_handle, const FileSys::Path& path, u32 file_size);

/**
 * Create a Directory from an Archive
 * @param archive_handle Handle to an open Archive object
 * @param path Path to the Directory inside of the Archive
 * @return Whether creation of directory succeeded
 */
ResultCode CreateDirectoryFromArchive(ArchiveHandle archive_handle, const FileSys::Path& path);

/**
 * Rename a Directory between two Archives
 * @param src_archive_handle Handle to the source Archive object
 * @param src_path Path to the Directory inside of the source Archive
 * @param dest_archive_handle Handle to the destination Archive object
 * @param dest_path Path to the Directory inside of the destination Archive
 * @return Whether rename succeeded
 */
ResultCode RenameDirectoryBetweenArchives(ArchiveHandle src_archive_handle, const FileSys::Path& src_path,
                                          ArchiveHandle dest_archive_handle, const FileSys::Path& dest_path);

/**
 * Open a Directory from an Archive
 * @param archive_handle Handle to an open Archive object
 * @param path Path to the Directory inside of the Archive
 * @return The opened Directory object as a Session
 */
ResultVal<Kernel::SharedPtr<Kernel::Session>> OpenDirectoryFromArchive(ArchiveHandle archive_handle,
        const FileSys::Path& path);

/**
 * Creates a blank SaveData archive.
 * @return ResultCode 0 on success or the corresponding code on error
 */
ResultCode FormatSaveData();

/**
 * Creates a blank SharedExtSaveData archive for the specified extdata ID
 * @param high The high word of the extdata id to create
 * @param low The low word of the extdata id to create
 * @return ResultCode 0 on success or the corresponding code on error
 */
ResultCode CreateExtSaveData(u32 high, u32 low);

/// Initialize archives
void ArchiveInit();

/// Shutdown archives
void ArchiveShutdown();

} // namespace FS
} // namespace Service
