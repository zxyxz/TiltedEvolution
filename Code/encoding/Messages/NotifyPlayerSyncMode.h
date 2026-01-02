#pragma once

#include "Message.h"

#include <Structs/SyncMode.h>

struct NotifyPlayerSyncMode final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlayerSyncMode;

    NotifyPlayerSyncMode()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerSyncMode& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Mode == acRhs.Mode; }

    uint32_t PlayerId{};
    SyncMode Mode{SyncMode::Normal};
};
