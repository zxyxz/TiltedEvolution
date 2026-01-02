#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>

struct NotifyDroppedItemPhysicsDisabled final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyDroppedItemPhysicsDisabled;

    NotifyDroppedItemPhysicsDisabled()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyDroppedItemPhysicsDisabled& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode()
            && DropId == acRhs.DropId
            && HasLocation == acRhs.HasLocation
            && (!HasLocation || Location == acRhs.Location)
            && HasRotation == acRhs.HasRotation
            && (!HasRotation || Rotation == acRhs.Rotation)
            && CellId == acRhs.CellId
            && WorldSpaceId == acRhs.WorldSpaceId
            && ReferenceId == acRhs.ReferenceId;
    }

    uint64_t DropId{};
    bool HasLocation{false};
    Vector3_NetQuantize Location{};
    bool HasRotation{false};
    Vector3_NetQuantize Rotation{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
};
