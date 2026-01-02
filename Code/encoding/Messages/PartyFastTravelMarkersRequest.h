#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// Client -> Server: send newly discovered fast travel (map) markers within the party.
struct PartyFastTravelMarkersRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPartyFastTravelMarkersRequest;

    PartyFastTravelMarkersRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    // When true, Markers represents the sender's complete marker set and should replace the cached set server-side.
    bool FullSync{false};
    TiltedPhoques::Vector<GameId> Markers{};
};
