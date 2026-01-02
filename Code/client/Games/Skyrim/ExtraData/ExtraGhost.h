#pragma once

#include "ExtraData.h"

struct ExtraGhost : BSExtraData
{
    inline static constexpr auto eExtraData = ExtraDataType::Ghost;

    ~ExtraGhost() override = default;

    ExtraDataType GetType() const noexcept override { return eExtraData; }

    // Matches CommonLibSSE ExtraGhost layout: bool at 0x10, total size 0x18
    bool ghost{true};
    uint8_t pad11{0};
    uint16_t pad12{0};
    uint32_t pad14{0};
};

static_assert(sizeof(ExtraGhost) == 0x18);
static_assert(offsetof(ExtraGhost, ghost) == 0x10);
