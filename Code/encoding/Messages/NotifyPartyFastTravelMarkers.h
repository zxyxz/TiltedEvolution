#pragma once

#include "Message.h"

#include <Structs/GameId.h>

// Server -> Client: announce party fast travel (map) markers that should be discovered locally.
struct NotifyPartyFastTravelMarkers final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyFastTravelMarkers;

    NotifyPartyFastTravelMarkers()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyFastTravelMarkers& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Markers == acRhs.Markers;
    }

    TiltedPhoques::Vector<GameId> Markers{};
};

