#pragma once

#include "ExtraData.h"

// Minimal map marker data used for fast travel discovery syncing.
struct MapMarkerData
{
    uint8_t pad0[0x10];
    uint8_t flags;
    uint8_t pad11;
    uint16_t type;
    uint32_t pad14;
};

static_assert(sizeof(MapMarkerData) == 0x18);

struct ExtraMapMarker : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::MapMarker;

    MapMarkerData* mapData;
};

static_assert(sizeof(ExtraMapMarker) == 0x18);
static_assert(offsetof(ExtraMapMarker, mapData) == 0x10);

