// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>

#include "core/loader/ncch.h"
#include "core/hle/kernel/kernel.h"
#include "core/mem_map.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Loader namespace

namespace Loader {

static const int kMaxSections = 8;        ///< Maximum number of sections (files) in an ExeFs
static const int kBlockSize   = 0x200;    ///< Size of ExeFS blocks (in bytes)

/**
 * Get the decompressed size of an LZSS compressed ExeFS file
 * @param buffer Buffer of compressed file
 * @param size Size of compressed buffer
 * @return Size of decompressed buffer
 */
static u32 LZSS_GetDecompressedSize(const u8* buffer, u32 size) {
    u32 offset_size = *(u32*)(buffer + size - 4);
    return offset_size + size;
}

/**
 * Decompress ExeFS file (compressed with LZSS)
 * @param compressed Compressed buffer
 * @param compressed_size Size of compressed buffer
 * @param decompressed Decompressed buffer
 * @param decompressed_size Size of decompressed buffer
 * @return True on success, otherwise false
 */
static bool LZSS_Decompress(const u8* compressed, u32 compressed_size, u8* decompressed, u32 decompressed_size) {
    const u8* footer = compressed + compressed_size - 8;
    u32 buffer_top_and_bottom = *reinterpret_cast<const u32*>(footer);
    u32 out = decompressed_size;
    u32 index = compressed_size - ((buffer_top_and_bottom >> 24) & 0xFF);
    u32 stop_index = compressed_size - (buffer_top_and_bottom & 0xFFFFFF);

    memset(decompressed, 0, decompressed_size);
    memcpy(decompressed, compressed, compressed_size);

    while (index > stop_index) {
       u8 control = compressed[--index];

        for (unsigned i = 0; i < 8; i++) {
            if (index <= stop_index)
                break;
            if (index <= 0)
                break;
            if (out <= 0)
                break;

            if (control & 0x80) {
                // Check if compression is out of bounds
                if (index < 2)
                    return false;
                index -= 2;

                u32 segment_offset = compressed[index] | (compressed[index + 1] << 8);
                u32 segment_size = ((segment_offset >> 12) & 15) + 3;
                segment_offset &= 0x0FFF;
                segment_offset += 2;

                // Check if compression is out of bounds
                if (out < segment_size)
                    return false;

                for (unsigned j = 0; j < segment_size; j++) {
                    // Check if compression is out of bounds
                    if (out + segment_offset >= decompressed_size)
                        return false;

                    u8 data = decompressed[out + segment_offset];
                    decompressed[--out] = data;
                }
            } else {
                // Check if compression is out of bounds
                if (out < 1)
                    return false;
                decompressed[--out] = compressed[--index];
            }
            control <<= 1;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// AppLoader_NCCH class

FileType AppLoader_NCCH::IdentifyType(FileUtil::IOFile& file) {
    u32 magic;
    file.Seek(0x100, SEEK_SET);
    if (1 != file.ReadArray<u32>(&magic, 1))
        return FileType::Error;

    if (MakeMagic('N', 'C', 'S', 'D') == magic)
        return FileType::CCI;

    if (MakeMagic('N', 'C', 'C', 'H') == magic)
        return FileType::CXI;

    return FileType::Error;
}

ResultStatus AppLoader_NCCH::LoadExec() const {
    if (!is_loaded)
        return ResultStatus::ErrorNotLoaded;

    std::vector<u8> code;
    if (ResultStatus::Success == ReadCode(code)) {
        Memory::WriteBlock(entry_point, &code[0], code.size());
        Kernel::LoadExec(entry_point);
        return ResultStatus::Success;
    }
    return ResultStatus::Error;
}

ResultStatus AppLoader_NCCH::LoadSectionExeFS(const char* name, std::vector<u8>& buffer) const {
    if (!file->IsOpen())
        return ResultStatus::Error;

    LOG_DEBUG(Loader, "%d sections:", kMaxSections);
    // Iterate through the ExeFs archive until we find the .code file...
    for (unsigned section_number = 0; section_number < kMaxSections; section_number++) {
        const auto& section = exefs_header.section[section_number];

        // Load the specified section...
        if (strcmp(section.name, name) == 0) {
            LOG_DEBUG(Loader, "%d - offset: 0x%08X, size: 0x%08X, name: %s", section_number,
                      section.offset, section.size, section.name);

            s64 section_offset = (section.offset + exefs_offset + sizeof(ExeFs_Header) + ncch_offset);
            file->Seek(section_offset, SEEK_SET);

            if (is_compressed) {
                // Section is compressed, read compressed .code section...
                std::unique_ptr<u8[]> temp_buffer;
                try {
                    temp_buffer.reset(new u8[section.size]);
                } catch (std::bad_alloc&) {
                    return ResultStatus::ErrorMemoryAllocationFailed;
                }

                if (file->ReadBytes(&temp_buffer[0], section.size) != section.size)
                    return ResultStatus::Error;

                // Decompress .code section...
                u32 decompressed_size = LZSS_GetDecompressedSize(&temp_buffer[0], section.size);
                buffer.resize(decompressed_size);
                if (!LZSS_Decompress(&temp_buffer[0], section.size, &buffer[0], decompressed_size))
                    return ResultStatus::ErrorInvalidFormat;
            } else {
                // Section is uncompressed...
                buffer.resize(section.size);
                if (file->ReadBytes(&buffer[0], section.size) != section.size)
                    return ResultStatus::Error;
            }
            return ResultStatus::Success;
        }
    }
    return ResultStatus::ErrorNotUsed;
}

ResultStatus AppLoader_NCCH::Load() {
    if (is_loaded)
        return ResultStatus::ErrorAlreadyLoaded;

    if (!file->IsOpen())
        return ResultStatus::Error;

    // Reset read pointer in case this file has been read before.
    file->Seek(0, SEEK_SET);

    if (file->ReadBytes(&ncch_header, sizeof(NCCH_Header)) != sizeof(NCCH_Header))
        return ResultStatus::Error;

    // Skip NCSD header and load first NCCH (NCSD is just a container of NCCH files)...
    if (MakeMagic('N', 'C', 'S', 'D') == ncch_header.magic) {
        LOG_WARNING(Loader, "Only loading the first (bootable) NCCH within the NCSD file!");
        ncch_offset = 0x4000;
        file->Seek(ncch_offset, SEEK_SET);
        file->ReadBytes(&ncch_header, sizeof(NCCH_Header));
    }

    // Verify we are loading the correct file type...
    if (MakeMagic('N', 'C', 'C', 'H') != ncch_header.magic)
        return ResultStatus::ErrorInvalidFormat;

    // Read ExHeader...

    if (file->ReadBytes(&exheader_header, sizeof(ExHeader_Header)) != sizeof(ExHeader_Header))
        return ResultStatus::Error;

    is_compressed = (exheader_header.codeset_info.flags.flag & 1) == 1;
    entry_point = exheader_header.codeset_info.text.address;

    LOG_INFO(Loader, "Name:            %s", exheader_header.codeset_info.name);
    LOG_DEBUG(Loader, "Code compressed: %s", is_compressed ? "yes" : "no");
    LOG_DEBUG(Loader, "Entry point:     0x%08X", entry_point);

    // Read ExeFS...

    exefs_offset = ncch_header.exefs_offset * kBlockSize;
    u32 exefs_size = ncch_header.exefs_size * kBlockSize;

    LOG_DEBUG(Loader, "ExeFS offset:    0x%08X", exefs_offset);
    LOG_DEBUG(Loader, "ExeFS size:      0x%08X", exefs_size);

    file->Seek(exefs_offset + ncch_offset, SEEK_SET);
    if (file->ReadBytes(&exefs_header, sizeof(ExeFs_Header)) != sizeof(ExeFs_Header))
        return ResultStatus::Error;

    is_loaded = true; // Set state to loaded

    return LoadExec(); // Load the executable into memory for booting
}

ResultStatus AppLoader_NCCH::ReadCode(std::vector<u8>& buffer) const {
    return LoadSectionExeFS(".code", buffer);
}

ResultStatus AppLoader_NCCH::ReadIcon(std::vector<u8>& buffer) const {
    return LoadSectionExeFS("icon", buffer);
}

ResultStatus AppLoader_NCCH::ReadBanner(std::vector<u8>& buffer) const {
    return LoadSectionExeFS("banner", buffer);
}

ResultStatus AppLoader_NCCH::ReadLogo(std::vector<u8>& buffer) const {
    return LoadSectionExeFS("logo", buffer);
}

ResultStatus AppLoader_NCCH::ReadRomFS(std::vector<u8>& buffer) const {
    if (!file->IsOpen())
        return ResultStatus::Error;

    // Check if the NCCH has a RomFS...
    if (ncch_header.romfs_offset != 0 && ncch_header.romfs_size != 0) {
        u32 romfs_offset = ncch_offset + (ncch_header.romfs_offset * kBlockSize) + 0x1000;
        u32 romfs_size = (ncch_header.romfs_size * kBlockSize) - 0x1000;

        LOG_DEBUG(Loader, "RomFS offset:    0x%08X", romfs_offset);
        LOG_DEBUG(Loader, "RomFS size:      0x%08X", romfs_size);

        buffer.resize(romfs_size);

        file->Seek(romfs_offset, SEEK_SET);
        if (file->ReadBytes(&buffer[0], romfs_size) != romfs_size)
            return ResultStatus::Error;

        return ResultStatus::Success;
    }
    LOG_DEBUG(Loader, "NCCH has no RomFS");
    return ResultStatus::ErrorNotUsed;
}

u64 AppLoader_NCCH::GetProgramId() const {
    return *reinterpret_cast<u64 const*>(&ncch_header.program_id[0]);
}

} // namespace Loader
