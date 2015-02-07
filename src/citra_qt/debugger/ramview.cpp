// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "ramview.h"

#include "common/common.h"
#include "core/mem_map.h"
GRamView::GRamView(QWidget* parent) : QHexEdit(parent)
{
}

void GRamView::OnCPUStepped()
{
    // TODO: QHexEdit doesn't show vertical scroll bars for > 10MB data streams...
    //setData(QByteArray((const char*)Mem_RAM,sizeof(Mem_RAM)/8));
}