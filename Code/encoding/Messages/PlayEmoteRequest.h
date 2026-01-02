#pragma once

#include "Message.h"

using TiltedPhoques::String;

struct PlayEmoteRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPlayEmoteRequest;

    PlayEmoteRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayEmoteRequest& acRhs) const noexcept { return ServerId == acRhs.ServerId && EventName == acRhs.EventName && GetOpcode() == acRhs.GetOpcode(); }

    uint32_t ServerId{};
    String EventName{};
};
