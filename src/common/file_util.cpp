// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.


#include "common/common.h"
#include "common/file_util.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>        // for SHGetFolderPath
#include <shellapi.h>
#include <commdlg.h>    // for GetSaveFileName
#include <io.h>
#include <direct.h>        // getcwd
#include <tchar.h>
#else
#include <sys/param.h>
#include <dirent.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFURL.h>
#include <CoreFoundation/CFBundle.h>
#endif

#include <algorithm>
#include <sys/stat.h>

#ifndef S_ISDIR
#define S_ISDIR(m)  (((m)&S_IFMT) == S_IFDIR)
#endif

#ifdef BSD4_4
#define stat64 stat
#define fstat64 fstat
#endif

// This namespace has various generic functions related to files and paths.
// The code still needs a ton of cleanup.
// REMEMBER: strdup considered harmful!
namespace FileUtil
{

// Remove any ending forward slashes from directory paths
// Modifies argument.
static void StripTailDirSlashes(std::string &fname)
{
    if (fname.length() > 1)
    {
        size_t i = fname.length() - 1;
        while (fname[i] == DIR_SEP_CHR)
            fname[i--] = '\0';
    }
    return;
}

// Returns true if file filename exists
bool Exists(const std::string &filename)
{
    struct stat64 file_info;

    std::string copy(filename);
    StripTailDirSlashes(copy);

#ifdef _WIN32
    int result = _tstat64(Common::UTF8ToTStr(copy).c_str(), &file_info);
#else
    int result = stat64(copy.c_str(), &file_info);
#endif

    return (result == 0);
}

// Returns true if filename is a directory
bool IsDirectory(const std::string &filename)
{
    struct stat64 file_info;

    std::string copy(filename);
    StripTailDirSlashes(copy);

#ifdef _WIN32
    int result = _tstat64(Common::UTF8ToTStr(copy).c_str(), &file_info);
#else
    int result = stat64(copy.c_str(), &file_info);
#endif

    if (result < 0) {
        LOG_WARNING(Common_Filesystem, "stat failed on %s: %s",
                 filename.c_str(), GetLastErrorMsg());
        return false;
    }

    return S_ISDIR(file_info.st_mode);
}

// Deletes a given filename, return true on success
// Doesn't supports deleting a directory
bool Delete(const std::string &filename)
{
    LOG_INFO(Common_Filesystem, "file %s", filename.c_str());

    // Return true because we care about the file no
    // being there, not the actual delete.
    if (!Exists(filename))
    {
        LOG_WARNING(Common_Filesystem, "%s does not exist", filename.c_str());
        return true;
    }

    // We can't delete a directory
    if (IsDirectory(filename))
    {
        LOG_ERROR(Common_Filesystem, "Failed: %s is a directory", filename.c_str());
        return false;
    }

#ifdef _WIN32
    if (!DeleteFile(Common::UTF8ToTStr(filename).c_str()))
    {
        LOG_ERROR(Common_Filesystem, "DeleteFile failed on %s: %s",
                 filename.c_str(), GetLastErrorMsg());
        return false;
    }
#else
    if (unlink(filename.c_str()) == -1) {
        LOG_ERROR(Common_Filesystem, "unlink failed on %s: %s",
                 filename.c_str(), GetLastErrorMsg());
        return false;
    }
#endif

    return true;
}

// Returns true if successful, or path already exists.
bool CreateDir(const std::string &path)
{
    LOG_TRACE(Common_Filesystem, "directory %s", path.c_str());
#ifdef _WIN32
    if (::CreateDirectory(Common::UTF8ToTStr(path).c_str(), nullptr))
        return true;
    DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS)
    {
        LOG_WARNING(Common_Filesystem, "CreateDirectory failed on %s: already exists", path.c_str());
        return true;
    }
    LOG_ERROR(Common_Filesystem, "CreateDirectory failed on %s: %i", path.c_str(), error);
    return false;
#else
    if (mkdir(path.c_str(), 0755) == 0)
        return true;

    int err = errno;

    if (err == EEXIST)
    {
        LOG_WARNING(Common_Filesystem, "mkdir failed on %s: already exists", path.c_str());
        return true;
    }

    LOG_ERROR(Common_Filesystem, "mkdir failed on %s: %s", path.c_str(), strerror(err));
    return false;
#endif
}

// Creates the full path of fullPath returns true on success
bool CreateFullPath(const std::string &fullPath)
{
    int panicCounter = 100;
    LOG_TRACE(Common_Filesystem, "path %s", fullPath.c_str());

    if (FileUtil::Exists(fullPath))
    {
        LOG_WARNING(Common_Filesystem, "path exists %s", fullPath.c_str());
        return true;
    }

    size_t position = 0;
    while (true)
    {
        // Find next sub path
        position = fullPath.find(DIR_SEP_CHR, position);

        // we're done, yay!
        if (position == fullPath.npos)
            return true;

        // Include the '/' so the first call is CreateDir("/") rather than CreateDir("")
        std::string const subPath(fullPath.substr(0, position + 1));
        if (!FileUtil::IsDirectory(subPath) && !FileUtil::CreateDir(subPath)) {
            LOG_ERROR(Common, "CreateFullPath: directory creation failed");
            return false;
        }

        // A safety check
        panicCounter--;
        if (panicCounter <= 0)
        {
            LOG_ERROR(Common, "CreateFullPath: directory structure is too deep");
            return false;
        }
        position++;
    }
}


// Deletes a directory filename, returns true on success
bool DeleteDir(const std::string &filename)
{
    LOG_INFO(Common_Filesystem, "directory %s", filename.c_str());

    // check if a directory
    if (!FileUtil::IsDirectory(filename))
    {
        LOG_ERROR(Common_Filesystem, "Not a directory %s", filename.c_str());
        return false;
    }

#ifdef _WIN32
    if (::RemoveDirectory(Common::UTF8ToTStr(filename).c_str()))
        return true;
#else
    if (rmdir(filename.c_str()) == 0)
        return true;
#endif
    LOG_ERROR(Common_Filesystem, "failed %s: %s", filename.c_str(), GetLastErrorMsg());

    return false;
}

// renames file srcFilename to destFilename, returns true on success
bool Rename(const std::string &srcFilename, const std::string &destFilename)
{
    LOG_TRACE(Common_Filesystem, "%s --> %s",
            srcFilename.c_str(), destFilename.c_str());
    if (rename(srcFilename.c_str(), destFilename.c_str()) == 0)
        return true;
    LOG_ERROR(Common_Filesystem, "failed %s --> %s: %s",
              srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
    return false;
}

// copies file srcFilename to destFilename, returns true on success
bool Copy(const std::string &srcFilename, const std::string &destFilename)
{
    LOG_TRACE(Common_Filesystem, "%s --> %s",
            srcFilename.c_str(), destFilename.c_str());
#ifdef _WIN32
    if (CopyFile(Common::UTF8ToTStr(srcFilename).c_str(), Common::UTF8ToTStr(destFilename).c_str(), FALSE))
        return true;

    LOG_ERROR(Common_Filesystem, "failed %s --> %s: %s",
            srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
    return false;
#else

    // buffer size
#define BSIZE 1024

    char buffer[BSIZE];

    // Open input file
    FILE *input = fopen(srcFilename.c_str(), "rb");
    if (!input)
    {
        LOG_ERROR(Common_Filesystem, "opening input failed %s --> %s: %s",
                srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
        return false;
    }

    // open output file
    FILE *output = fopen(destFilename.c_str(), "wb");
    if (!output)
    {
        fclose(input);
        LOG_ERROR(Common_Filesystem, "opening output failed %s --> %s: %s",
                srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
        return false;
    }

    // copy loop
    while (!feof(input))
    {
        // read input
        int rnum = fread(buffer, sizeof(char), BSIZE, input);
        if (rnum != BSIZE)
        {
            if (ferror(input) != 0)
            {
                LOG_ERROR(Common_Filesystem,
                        "failed reading from source, %s --> %s: %s",
                        srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
                goto bail;
            }
        }

        // write output
        int wnum = fwrite(buffer, sizeof(char), rnum, output);
        if (wnum != rnum)
        {
            LOG_ERROR(Common_Filesystem,
                    "failed writing to output, %s --> %s: %s",
                    srcFilename.c_str(), destFilename.c_str(), GetLastErrorMsg());
            goto bail;
        }
    }
    // close files
    fclose(input);
    fclose(output);
    return true;
bail:
    if (input)
        fclose(input);
    if (output)
        fclose(output);
    return false;
#endif
}

// Returns the size of filename (64bit)
u64 GetSize(const std::string &filename)
{
    if (!Exists(filename))
    {
        LOG_ERROR(Common_Filesystem, "failed %s: No such file", filename.c_str());
        return 0;
    }

    if (IsDirectory(filename))
    {
        LOG_ERROR(Common_Filesystem, "failed %s: is a directory", filename.c_str());
        return 0;
    }

    struct stat64 buf;
#ifdef _WIN32
    if (_tstat64(Common::UTF8ToTStr(filename).c_str(), &buf) == 0)
#else
    if (stat64(filename.c_str(), &buf) == 0)
#endif
    {
        LOG_TRACE(Common_Filesystem, "%s: %lld",
                filename.c_str(), (long long)buf.st_size);
        return buf.st_size;
    }

    LOG_ERROR(Common_Filesystem, "Stat failed %s: %s",
            filename.c_str(), GetLastErrorMsg());
    return 0;
}

// Overloaded GetSize, accepts file descriptor
u64 GetSize(const int fd)
{
    struct stat64 buf;
    if (fstat64(fd, &buf) != 0) {
        LOG_ERROR(Common_Filesystem, "GetSize: stat failed %i: %s",
            fd, GetLastErrorMsg());
        return 0;
    }
    return buf.st_size;
}

// Overloaded GetSize, accepts FILE*
u64 GetSize(FILE *f)
{
    // can't use off_t here because it can be 32-bit
    u64 pos = ftello(f);
    if (fseeko(f, 0, SEEK_END) != 0) {
        LOG_ERROR(Common_Filesystem, "GetSize: seek failed %p: %s",
              f, GetLastErrorMsg());
        return 0;
    }
    u64 size = ftello(f);
    if ((size != pos) && (fseeko(f, pos, SEEK_SET) != 0)) {
        LOG_ERROR(Common_Filesystem, "GetSize: seek failed %p: %s",
              f, GetLastErrorMsg());
        return 0;
    }
    return size;
}

// creates an empty file filename, returns true on success
bool CreateEmptyFile(const std::string &filename)
{
    LOG_TRACE(Common_Filesystem, "%s", filename.c_str());

    if (!FileUtil::IOFile(filename, "wb"))
    {
        LOG_ERROR(Common_Filesystem, "failed %s: %s",
                  filename.c_str(), GetLastErrorMsg());
        return false;
    }

    return true;
}


// Scans the directory tree gets, starting from _Directory and adds the
// results into parentEntry. Returns the number of files+directories found
u32 ScanDirectoryTree(const std::string &directory, FSTEntry& parentEntry)
{
    LOG_TRACE(Common_Filesystem, "directory %s", directory.c_str());
    // How many files + directories we found
    u32 foundEntries = 0;
#ifdef _WIN32
    // Find the first file in the directory.
    WIN32_FIND_DATA ffd;

    HANDLE hFind = FindFirstFile(Common::UTF8ToTStr(directory + "\\*").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        FindClose(hFind);
        return foundEntries;
    }
    // windows loop
    do
    {
        FSTEntry entry;
        const std::string virtualName(Common::TStrToUTF8(ffd.cFileName));
#else
    struct dirent dirent, *result = nullptr;

    DIR *dirp = opendir(directory.c_str());
    if (!dirp)
        return 0;

    // non windows loop
    while (!readdir_r(dirp, &dirent, &result) && result)
    {
        FSTEntry entry;
        const std::string virtualName(result->d_name);
#endif
        // check for "." and ".."
        if (((virtualName[0] == '.') && (virtualName[1] == '\0')) ||
                ((virtualName[0] == '.') && (virtualName[1] == '.') &&
                 (virtualName[2] == '\0')))
            continue;
        entry.virtualName = virtualName;
        entry.physicalName = directory;
        entry.physicalName += DIR_SEP + entry.virtualName;

        if (IsDirectory(entry.physicalName.c_str()))
        {
            entry.isDirectory = true;
            // is a directory, lets go inside
            entry.size = ScanDirectoryTree(entry.physicalName, entry);
            foundEntries += (u32)entry.size;
        }
        else
        { // is a file
            entry.isDirectory = false;
            entry.size = GetSize(entry.physicalName.c_str());
        }
        ++foundEntries;
        // Push into the tree
        parentEntry.children.push_back(entry);
#ifdef _WIN32
    } while (FindNextFile(hFind, &ffd) != 0);
    FindClose(hFind);
#else
    }
    closedir(dirp);
#endif
    // Return number of entries found.
    return foundEntries;
}


// Deletes the given directory and anything under it. Returns true on success.
bool DeleteDirRecursively(const std::string &directory)
{
    LOG_TRACE(Common_Filesystem, "%s", directory.c_str());
#ifdef _WIN32
    // Find the first file in the directory.
    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile(Common::UTF8ToTStr(directory + "\\*").c_str(), &ffd);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        FindClose(hFind);
        return false;
    }

    // windows loop
    do
    {
        const std::string virtualName(Common::TStrToUTF8(ffd.cFileName));
#else
    struct dirent dirent, *result = nullptr;
    DIR *dirp = opendir(directory.c_str());
    if (!dirp)
        return false;

    // non windows loop
    while (!readdir_r(dirp, &dirent, &result) && result)
    {
        const std::string virtualName = result->d_name;
#endif

        // check for "." and ".."
        if (((virtualName[0] == '.') && (virtualName[1] == '\0')) ||
            ((virtualName[0] == '.') && (virtualName[1] == '.') &&
             (virtualName[2] == '\0')))
            continue;

        std::string newPath = directory + DIR_SEP_CHR + virtualName;
        if (IsDirectory(newPath))
        {
            if (!DeleteDirRecursively(newPath))
            {
                #ifndef _WIN32
                closedir(dirp);
                #endif

                return false;
            }
        }
        else
        {
            if (!FileUtil::Delete(newPath))
            {
                #ifndef _WIN32
                closedir(dirp);
                #endif

                return false;
            }
        }

#ifdef _WIN32
    } while (FindNextFile(hFind, &ffd) != 0);
    FindClose(hFind);
#else
    }
    closedir(dirp);
#endif
    FileUtil::DeleteDir(directory);

    return true;
}

// Create directory and copy contents (does not overwrite existing files)
void CopyDir(const std::string &source_path, const std::string &dest_path)
{
#ifndef _WIN32
    if (source_path == dest_path) return;
    if (!FileUtil::Exists(source_path)) return;
    if (!FileUtil::Exists(dest_path)) FileUtil::CreateFullPath(dest_path);

    struct dirent dirent, *result = nullptr;
    DIR *dirp = opendir(source_path.c_str());
    if (!dirp) return;

    while (!readdir_r(dirp, &dirent, &result) && result)
    {
        const std::string virtualName(result->d_name);
        // check for "." and ".."
        if (((virtualName[0] == '.') && (virtualName[1] == '\0')) ||
            ((virtualName[0] == '.') && (virtualName[1] == '.') &&
            (virtualName[2] == '\0')))
            continue;

        std::string source, dest;
        source = source_path + virtualName;
        dest = dest_path + virtualName;
        if (IsDirectory(source))
        {
            source += '/';
            dest += '/';
            if (!FileUtil::Exists(dest)) FileUtil::CreateFullPath(dest);
            CopyDir(source, dest);
        }
        else if (!FileUtil::Exists(dest)) FileUtil::Copy(source, dest);
    }
    closedir(dirp);
#endif
}

// Returns the current directory
std::string GetCurrentDir()
{
    char *dir;
    // Get the current working directory (getcwd uses malloc)
    if (!(dir = __getcwd(nullptr, 0))) {

        LOG_ERROR(Common_Filesystem, "GetCurrentDirectory failed: %s",
                GetLastErrorMsg());
        return nullptr;
    }
    std::string strDir = dir;
    free(dir);
    return strDir;
}

// Sets the current directory to the given directory
bool SetCurrentDir(const std::string &directory)
{
    return __chdir(directory.c_str()) == 0;
}

#if defined(__APPLE__)
std::string GetBundleDirectory()
{
    CFURLRef BundleRef;
    char AppBundlePath[MAXPATHLEN];
    // Get the main bundle for the app
    BundleRef = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    CFStringRef BundlePath = CFURLCopyFileSystemPath(BundleRef, kCFURLPOSIXPathStyle);
    CFStringGetFileSystemRepresentation(BundlePath, AppBundlePath, sizeof(AppBundlePath));
    CFRelease(BundleRef);
    CFRelease(BundlePath);

    return AppBundlePath;
}
#endif

#ifdef _WIN32
std::string& GetExeDirectory()
{
    static std::string DolphinPath;
    if (DolphinPath.empty())
    {
        TCHAR Dolphin_exe_Path[2048];
        GetModuleFileName(nullptr, Dolphin_exe_Path, 2048);
        DolphinPath = Common::TStrToUTF8(Dolphin_exe_Path);
        DolphinPath = DolphinPath.substr(0, DolphinPath.find_last_of('\\'));
    }
    return DolphinPath;
}
#endif

std::string GetSysDirectory()
{
    std::string sysDir;

#if defined (__APPLE__)
    sysDir = GetBundleDirectory();
    sysDir += DIR_SEP;
    sysDir += SYSDATA_DIR;
#else
    sysDir = SYSDATA_DIR;
#endif
    sysDir += DIR_SEP;

    LOG_DEBUG(Common_Filesystem, "Setting to %s:", sysDir.c_str());
    return sysDir;
}

// Returns a string with a Citra data dir or file in the user's home
// directory. To be used in "multi-user" mode (that is, installed).
const std::string& GetUserPath(const unsigned int DirIDX, const std::string &newPath)
{
    static std::string paths[NUM_PATH_INDICES];

    // Set up all paths and files on the first run
    if (paths[D_USER_IDX].empty())
    {
#ifdef _WIN32
        paths[D_USER_IDX] = GetExeDirectory() + DIR_SEP USERDATA_DIR DIR_SEP;
#else
        if (FileUtil::Exists(ROOT_DIR DIR_SEP USERDATA_DIR))
            paths[D_USER_IDX] = ROOT_DIR DIR_SEP USERDATA_DIR DIR_SEP;
        else
            paths[D_USER_IDX] = std::string(getenv("HOME") ?
                getenv("HOME") : getenv("PWD") ?
                getenv("PWD") : "") + DIR_SEP EMU_DATA_DIR DIR_SEP;
#endif

        paths[D_CONFIG_IDX]         = paths[D_USER_IDX] + CONFIG_DIR DIR_SEP;
        paths[D_GAMECONFIG_IDX]     = paths[D_USER_IDX] + GAMECONFIG_DIR DIR_SEP;
        paths[D_MAPS_IDX]           = paths[D_USER_IDX] + MAPS_DIR DIR_SEP;
        paths[D_CACHE_IDX]          = paths[D_USER_IDX] + CACHE_DIR DIR_SEP;
        paths[D_SDMC_IDX]           = paths[D_USER_IDX] + SDMC_DIR DIR_SEP;
        paths[D_NAND_IDX]           = paths[D_USER_IDX] + NAND_DIR DIR_SEP;
        paths[D_SYSDATA_IDX]        = paths[D_USER_IDX] + SYSDATA_DIR DIR_SEP;
        paths[D_SHADERCACHE_IDX]    = paths[D_USER_IDX] + SHADERCACHE_DIR DIR_SEP;
        paths[D_SHADERS_IDX]        = paths[D_USER_IDX] + SHADERS_DIR DIR_SEP;
        paths[D_STATESAVES_IDX]     = paths[D_USER_IDX] + STATESAVES_DIR DIR_SEP;
        paths[D_SCREENSHOTS_IDX]    = paths[D_USER_IDX] + SCREENSHOTS_DIR DIR_SEP;
        paths[D_DUMP_IDX]           = paths[D_USER_IDX] + DUMP_DIR DIR_SEP;
        paths[D_DUMPFRAMES_IDX]     = paths[D_DUMP_IDX] + DUMP_FRAMES_DIR DIR_SEP;
        paths[D_DUMPAUDIO_IDX]      = paths[D_DUMP_IDX] + DUMP_AUDIO_DIR DIR_SEP;
        paths[D_DUMPTEXTURES_IDX]   = paths[D_DUMP_IDX] + DUMP_TEXTURES_DIR DIR_SEP;
        paths[D_LOGS_IDX]           = paths[D_USER_IDX] + LOGS_DIR DIR_SEP;
        paths[F_DEBUGGERCONFIG_IDX] = paths[D_CONFIG_IDX] + DEBUGGER_CONFIG;
        paths[F_LOGGERCONFIG_IDX]   = paths[D_CONFIG_IDX] + LOGGER_CONFIG;
        paths[F_MAINLOG_IDX]        = paths[D_LOGS_IDX] + MAIN_LOG;
    }

    if (!newPath.empty())
    {
        if (!FileUtil::IsDirectory(newPath))
        {
            LOG_ERROR(Common_Filesystem, "Invalid path specified %s", newPath.c_str());
            return paths[DirIDX];
        }
        else
        {
            paths[DirIDX] = newPath;
        }

        switch (DirIDX)
        {
        case D_ROOT_IDX:
            paths[D_USER_IDX]           = paths[D_ROOT_IDX] + DIR_SEP;
            paths[D_SYSCONF_IDX]        = paths[D_USER_IDX] + SYSCONF_DIR + DIR_SEP;
            paths[F_SYSCONF_IDX]        = paths[D_SYSCONF_IDX] + SYSCONF;
            break;

        case D_USER_IDX:
            paths[D_USER_IDX]           = paths[D_ROOT_IDX] + DIR_SEP;
            paths[D_CONFIG_IDX]         = paths[D_USER_IDX] + CONFIG_DIR DIR_SEP;
            paths[D_GAMECONFIG_IDX]     = paths[D_USER_IDX] + GAMECONFIG_DIR DIR_SEP;
            paths[D_MAPS_IDX]           = paths[D_USER_IDX] + MAPS_DIR DIR_SEP;
            paths[D_CACHE_IDX]          = paths[D_USER_IDX] + CACHE_DIR DIR_SEP;
            paths[D_SDMC_IDX]           = paths[D_USER_IDX] + SDMC_DIR DIR_SEP;
            paths[D_NAND_IDX]           = paths[D_USER_IDX] + NAND_DIR DIR_SEP;
            paths[D_SHADERCACHE_IDX]    = paths[D_USER_IDX] + SHADERCACHE_DIR DIR_SEP;
            paths[D_SHADERS_IDX]        = paths[D_USER_IDX] + SHADERS_DIR DIR_SEP;
            paths[D_STATESAVES_IDX]     = paths[D_USER_IDX] + STATESAVES_DIR DIR_SEP;
            paths[D_SCREENSHOTS_IDX]    = paths[D_USER_IDX] + SCREENSHOTS_DIR DIR_SEP;
            paths[D_DUMP_IDX]           = paths[D_USER_IDX] + DUMP_DIR DIR_SEP;
            paths[D_DUMPFRAMES_IDX]     = paths[D_DUMP_IDX] + DUMP_FRAMES_DIR DIR_SEP;
            paths[D_DUMPAUDIO_IDX]      = paths[D_DUMP_IDX] + DUMP_AUDIO_DIR DIR_SEP;
            paths[D_DUMPTEXTURES_IDX]   = paths[D_DUMP_IDX] + DUMP_TEXTURES_DIR DIR_SEP;
            paths[D_LOGS_IDX]           = paths[D_USER_IDX] + LOGS_DIR DIR_SEP;
            paths[D_SYSCONF_IDX]        = paths[D_USER_IDX] + SYSCONF_DIR DIR_SEP;
            paths[F_EMUCONFIG_IDX]      = paths[D_CONFIG_IDX] + EMU_CONFIG;
            paths[F_DEBUGGERCONFIG_IDX] = paths[D_CONFIG_IDX] + DEBUGGER_CONFIG;
            paths[F_LOGGERCONFIG_IDX]   = paths[D_CONFIG_IDX] + LOGGER_CONFIG;
            paths[F_MAINLOG_IDX]        = paths[D_LOGS_IDX] + MAIN_LOG;
            break;

        case D_CONFIG_IDX:
            paths[F_EMUCONFIG_IDX]      = paths[D_CONFIG_IDX] + EMU_CONFIG;
            paths[F_DEBUGGERCONFIG_IDX] = paths[D_CONFIG_IDX] + DEBUGGER_CONFIG;
            paths[F_LOGGERCONFIG_IDX]   = paths[D_CONFIG_IDX] + LOGGER_CONFIG;
            break;

        case D_DUMP_IDX:
            paths[D_DUMPFRAMES_IDX]     = paths[D_DUMP_IDX] + DUMP_FRAMES_DIR DIR_SEP;
            paths[D_DUMPAUDIO_IDX]      = paths[D_DUMP_IDX] + DUMP_AUDIO_DIR DIR_SEP;
            paths[D_DUMPTEXTURES_IDX]   = paths[D_DUMP_IDX] + DUMP_TEXTURES_DIR DIR_SEP;
            break;

        case D_LOGS_IDX:
            paths[F_MAINLOG_IDX]        = paths[D_LOGS_IDX] + MAIN_LOG;
        }
    }

    return paths[DirIDX];
}

size_t WriteStringToFile(bool text_file, const std::string &str, const char *filename)
{
    return FileUtil::IOFile(filename, text_file ? "w" : "wb").WriteBytes(str.data(), str.size());
}

size_t ReadFileToString(bool text_file, const char *filename, std::string &str)
{
    FileUtil::IOFile file(filename, text_file ? "r" : "rb");
    auto const f = file.GetHandle();

    if (!f)
        return false;

    str.resize(static_cast<u32>(GetSize(f)));
    return file.ReadArray(&str[0], str.size());
}

/**
 * Splits the filename into 8.3 format
 * Loosely implemented following https://en.wikipedia.org/wiki/8.3_filename
 * @param filename The normal filename to use
 * @param short_name A 9-char array in which the short name will be written
 * @param extension A 4-char array in which the extension will be written
 */
void SplitFilename83(const std::string& filename, std::array<char, 9>& short_name,
                     std::array<char, 4>& extension) {
    const std::string forbidden_characters = ".\"/\\[]:;=, ";

    // On a FAT32 partition, 8.3 names are stored as a 11 bytes array, filled with spaces.
    short_name = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'};
    extension = {' ', ' ', ' ', '\0'};

    std::string::size_type point = filename.rfind('.');
    if (point == filename.size() - 1)
        point = filename.rfind('.', point);

    // Get short name.
    int j = 0;
    for (char letter : filename.substr(0, point)) {
        if (forbidden_characters.find(letter, 0) != std::string::npos)
            continue;
        if (j == 8) {
            // TODO(Link Mauve): also do that for filenames containing a space.
            // TODO(Link Mauve): handle multiple files having the same short name.
            short_name[6] = '~';
            short_name[7] = '1';
            break;
        }
        short_name[j++] = toupper(letter);
    }

    // Get extension.
    if (point != std::string::npos) {
        j = 0;
        for (char letter : filename.substr(point + 1, 3))
            extension[j++] = toupper(letter);
    }
}

IOFile::IOFile()
    : m_file(nullptr), m_good(true)
{}

IOFile::IOFile(std::FILE* file)
    : m_file(file), m_good(true)
{}

IOFile::IOFile(const std::string& filename, const char openmode[])
    : m_file(nullptr), m_good(true)
{
    Open(filename, openmode);
}

IOFile::~IOFile()
{
    Close();
}

IOFile::IOFile(IOFile&& other)
    : m_file(nullptr), m_good(true)
{
    Swap(other);
}

IOFile& IOFile::operator=(IOFile&& other)
{
    Swap(other);
    return *this;
}

void IOFile::Swap(IOFile& other)
{
    std::swap(m_file, other.m_file);
    std::swap(m_good, other.m_good);
}

bool IOFile::Open(const std::string& filename, const char openmode[])
{
    Close();
#ifdef _WIN32
    _tfopen_s(&m_file, Common::UTF8ToTStr(filename).c_str(), Common::UTF8ToTStr(openmode).c_str());
#else
    m_file = fopen(filename.c_str(), openmode);
#endif

    m_good = IsOpen();
    return m_good;
}

bool IOFile::Close()
{
    if (!IsOpen() || 0 != std::fclose(m_file))
        m_good = false;

    m_file = nullptr;
    return m_good;
}

std::FILE* IOFile::ReleaseHandle()
{
    std::FILE* const ret = m_file;
    m_file = nullptr;
    return ret;
}

void IOFile::SetHandle(std::FILE* file)
{
    Close();
    Clear();
    m_file = file;
}

u64 IOFile::GetSize()
{
    if (IsOpen())
        return FileUtil::GetSize(m_file);
    else
        return 0;
}

bool IOFile::Seek(s64 off, int origin)
{
    if (!IsOpen() || 0 != fseeko(m_file, off, origin))
        m_good = false;

    return m_good;
}

u64 IOFile::Tell()
{
    if (IsOpen())
        return ftello(m_file);
    else
        return -1;
}

bool IOFile::Flush()
{
    if (!IsOpen() || 0 != std::fflush(m_file))
        m_good = false;

    return m_good;
}

bool IOFile::Resize(u64 size)
{
    if (!IsOpen() || 0 !=
#ifdef _WIN32
        // ector: _chsize sucks, not 64-bit safe
        // F|RES: changed to _chsize_s. i think it is 64-bit safe
        _chsize_s(_fileno(m_file), size)
#else
        // TODO: handle 64bit and growing
        ftruncate(fileno(m_file), size)
#endif
    )
        m_good = false;

    return m_good;
}

} // namespace
