#pragma once

#include "Message.h"

#include <Structs/SyncMode.h>

struct RequestSetSyncMode final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestSetSyncMode;

    RequestSetSyncMode()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestSetSyncMode& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Mode == acRhs.Mode; }

    SyncMode Mode{SyncMode::Normal};
};
