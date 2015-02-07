// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>

#include "common/make_unique.h"

#include "core/file_sys/archive_romfs.h"
#include "core/loader/3dsx.h"
#include "core/loader/elf.h"
#include "core/loader/ncch.h"
#include "core/hle/service/fs/archive.h"
#include "core/mem_map.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Loader {

/**
 * Identifies the type of a bootable file
 * @param file open file
 * @return FileType of file
 */
static FileType IdentifyFile(FileUtil::IOFile& file) {
    FileType type;

#define CHECK_TYPE(loader) \
    type = AppLoader_##loader::IdentifyType(file); \
    if (FileType::Error != type) \
        return type;

    CHECK_TYPE(THREEDSX)
    CHECK_TYPE(ELF)
    CHECK_TYPE(NCCH)

#undef CHECK_TYPE

    return FileType::Unknown;
}

/**
 * Guess the type of a bootable file from its extension
 * @param filename String filename of bootable file
 * @return FileType of file
 */
static FileType GuessFromFilename(const std::string& filename) {
    if (filename.size() == 0) {
        LOG_ERROR(Loader, "invalid filename %s", filename.c_str());
        return FileType::Error;
    }

    size_t extension_loc = filename.find_last_of('.');
    if (extension_loc == std::string::npos)
        return FileType::Unknown;
    std::string extension = Common::ToLower(filename.substr(extension_loc));

    if (extension == ".elf")
        return FileType::ELF;
    else if (extension == ".axf")
        return FileType::ELF;
    else if (extension == ".cxi")
        return FileType::CXI;
    else if (extension == ".cci")
        return FileType::CCI;
    else if (extension == ".bin")
        return FileType::BIN;
    else if (extension == ".3ds")
        return FileType::CCI;
    else if (extension == ".3dsx")
        return FileType::THREEDSX;
    return FileType::Unknown;
}

static const char* GetFileTypeString(FileType type) {
    switch (type) {
    case FileType::CCI:
        return "NCSD";
    case FileType::CXI:
        return "NCCH";
    case FileType::ELF:
        return "ELF";
    case FileType::THREEDSX:
        return "3DSX";
    case FileType::BIN:
        return "raw";
    case FileType::Error:
    case FileType::Unknown:
        return "unknown";
    }
}

ResultStatus LoadFile(const std::string& filename) {
    std::unique_ptr<FileUtil::IOFile> file(new FileUtil::IOFile(filename, "rb"));
    if (!file->IsOpen()) {
        LOG_ERROR(Loader, "Failed to load file %s", filename.c_str());
        return ResultStatus::Error;
    }

    FileType type = IdentifyFile(*file);
    FileType filename_type = GuessFromFilename(filename);

    if (type != filename_type) {
        LOG_WARNING(Loader, "File %s has a different type than its extension.", filename.c_str());
        if (FileType::Unknown == type)
            type = filename_type;
    }

    LOG_INFO(Loader, "Loading file %s as %s...", filename.c_str(), GetFileTypeString(type));

    switch (type) {

    //3DSX file format...
    case FileType::THREEDSX:
        return AppLoader_THREEDSX(std::move(file)).Load();

    // Standard ELF file format...
    case FileType::ELF:
        return AppLoader_ELF(std::move(file)).Load();

    // NCCH/NCSD container formats...
    case FileType::CXI:
    case FileType::CCI:
    {
        AppLoader_NCCH app_loader(std::move(file));

        // Load application and RomFS
        if (ResultStatus::Success == app_loader.Load()) {
            Kernel::g_program_id = app_loader.GetProgramId();
            Service::FS::CreateArchive(Common::make_unique<FileSys::Archive_RomFS>(app_loader), Service::FS::ArchiveIdCode::RomFS);
            return ResultStatus::Success;
        }
        break;
    }

    // Raw BIN file format...
    case FileType::BIN:
    {
        size_t size = (size_t)file->GetSize();
        if (file->ReadBytes(Memory::GetPointer(Memory::EXEFS_CODE_VADDR), size) != size)
            return ResultStatus::Error;

        Kernel::LoadExec(Memory::EXEFS_CODE_VADDR);
        return ResultStatus::Success;
    }

    // Error occurred durring IdentifyFile...
    case FileType::Error:

    // IdentifyFile could know identify file type...
    case FileType::Unknown:
    {
        LOG_CRITICAL(Loader, "File %s is of unknown type.", filename.c_str());
        return ResultStatus::ErrorInvalidFormat;
    }
    }
    return ResultStatus::Error;
}

} // namespace Loader
