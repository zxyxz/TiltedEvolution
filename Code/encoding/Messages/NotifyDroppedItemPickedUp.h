#pragma once

#include "Message.h"
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>

struct NotifyDroppedItemPickedUp final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyDroppedItemPickedUp;

    NotifyDroppedItemPickedUp()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyDroppedItemPickedUp& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode()
            && ServerId == acRhs.ServerId
            && Item == acRhs.Item
            && DropId == acRhs.DropId
            && HasLocation == acRhs.HasLocation
            && (!HasLocation || Location == acRhs.Location)
            && HasRotation == acRhs.HasRotation
            && (!HasRotation || Rotation == acRhs.Rotation)
            && CellId == acRhs.CellId
            && WorldSpaceId == acRhs.WorldSpaceId
            && ReferenceId == acRhs.ReferenceId;
    }

    uint32_t ServerId{};
    Inventory::Entry Item{};
    uint64_t DropId{};

    // Additional location information for remote pickups
    bool HasLocation{false};
    Vector3_NetQuantize Location{};
    bool HasRotation{false};
    Vector3_NetQuantize Rotation{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
};
