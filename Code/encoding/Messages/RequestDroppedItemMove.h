#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>

struct RequestDroppedItemMove final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestDroppedItemMove;

    RequestDroppedItemMove()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestDroppedItemMove& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode()
            && ServerId == acRhs.ServerId
            && DropId == acRhs.DropId
            && HasLocation == acRhs.HasLocation
            && (!HasLocation || Location == acRhs.Location)
            && HasRotation == acRhs.HasRotation
            && (!HasRotation || Rotation == acRhs.Rotation)
            && HasVelocity == acRhs.HasVelocity
            && (!HasVelocity || Velocity == acRhs.Velocity)
            && HasAngularVelocity == acRhs.HasAngularVelocity
            && (!HasAngularVelocity || AngularVelocity == acRhs.AngularVelocity)
            && CellId == acRhs.CellId
            && WorldSpaceId == acRhs.WorldSpaceId
            && ReferenceId == acRhs.ReferenceId;
    }

    uint32_t ServerId{};
    uint64_t DropId{};
    bool HasLocation{false};
    Vector3_NetQuantize Location{};
    bool HasRotation{false};
    Vector3_NetQuantize Rotation{};
    bool HasVelocity{false};
    Vector3_NetQuantize Velocity{};
    bool HasAngularVelocity{false};
    Vector3_NetQuantize AngularVelocity{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
};
