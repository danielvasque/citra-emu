// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

#include "core/hle/kernel/kernel.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FileSys namespace

namespace FileSys {

class FileBackend : NonCopyable {
public:
    FileBackend() { }
    virtual ~FileBackend() { }

    /**
     * Open the file
     * @return true if the file opened correctly
     */
    virtual bool Open() = 0;

    /**
     * Read data from the file
     * @param offset Offset in bytes to start reading data from
     * @param length Length in bytes of data to read from file
     * @param buffer Buffer to read data into
     * @return Number of bytes read
     */
    virtual size_t Read(const u64 offset, const u32 length, u8* buffer) const = 0;

    /**
     * Write data to the file
     * @param offset Offset in bytes to start writing data to
     * @param length Length in bytes of data to write to file
     * @param flush The flush parameters (0 == do not flush)
     * @param buffer Buffer to read data from
     * @return Number of bytes written
     */
    virtual size_t Write(const u64 offset, const u32 length, const u32 flush, const u8* buffer) const = 0;

    /**
     * Get the size of the file in bytes
     * @return Size of the file in bytes
     */
    virtual size_t GetSize() const = 0;

    /**
     * Set the size of the file in bytes
     * @param size New size of the file
     * @return true if successful
     */
    virtual bool SetSize(const u64 size) const = 0;

    /**
     * Close the file
     * @return true if the file closed correctly
     */
    virtual bool Close() const = 0;

    /**
     * Flushes the file
     */
    virtual void Flush() const = 0;
};

} // namespace FileSys
