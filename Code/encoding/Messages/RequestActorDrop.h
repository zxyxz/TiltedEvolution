#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Guid.h>
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>

struct RequestActorDrop final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestActorDrop;

    RequestActorDrop()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestActorDrop& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && ServerId == acRhs.ServerId && ActorFormId == acRhs.ActorFormId && Item == acRhs.Item && ClientDropId == acRhs.ClientDropId && HasLocation == acRhs.HasLocation &&
               (!HasLocation || Location == acRhs.Location) && HasRotation == acRhs.HasRotation && (!HasRotation || Rotation == acRhs.Rotation) && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId &&
               ReferenceId == acRhs.ReferenceId;
    }

    uint32_t ServerId{};
    uint32_t ActorFormId{};
    Inventory::Entry Item{};
    Guid ClientDropId{};
    bool HasLocation = false;
    Vector3_NetQuantize Location{};
    bool HasRotation = false;
    Vector3_NetQuantize Rotation{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
};
