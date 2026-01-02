#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>

// Client -> Server: send the local player's current location for party tracking
struct PartyPositionUpdateRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPartyPositionUpdateRequest;

    PartyPositionUpdateRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    Vector3_NetQuantize Position;
    GameId WorldSpaceId; // 0/empty for interiors
    GameId CellId;
};

