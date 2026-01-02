#pragma once

#include "Message.h"

// Client -> Server: request a full snapshot of party positions.
struct PartyPositionsRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPartyPositionsRequest;

    PartyPositionsRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
};
