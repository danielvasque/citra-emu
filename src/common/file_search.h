// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>

class CFileSearch
{
public:
    typedef std::vector<std::string>XStringVector;

    CFileSearch(const XStringVector& _rSearchStrings, const XStringVector& _rDirectories);
    const XStringVector& GetFileNames() const;

private:

    void FindFiles(const std::string& _searchString, const std::string& _strPath);

    XStringVector m_FileNames;
};
