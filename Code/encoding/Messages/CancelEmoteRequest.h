#pragma once

#include "Message.h"

struct CancelEmoteRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kCancelEmoteRequest;

    CancelEmoteRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const CancelEmoteRequest& acRhs) const noexcept { return ServerId == acRhs.ServerId && GetOpcode() == acRhs.GetOpcode(); }

    uint32_t ServerId{};
};
