#pragma once

#include "Message.h"

struct TeleportResponse final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kTeleportResponse;

    TeleportResponse()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const TeleportResponse& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && RequesterId == acRhs.RequesterId && Accepted == acRhs.Accepted;
    }

    uint16_t RequesterId{};
    bool Accepted{};
};
