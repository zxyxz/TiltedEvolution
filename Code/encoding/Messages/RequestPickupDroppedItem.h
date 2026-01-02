#pragma once

#include "Message.h"
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>

struct RequestPickupDroppedItem final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestPickupDroppedItem;

    RequestPickupDroppedItem()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestPickupDroppedItem& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode()
            && ServerId == acRhs.ServerId
            && DropId == acRhs.DropId
            && Item == acRhs.Item
            && HasLocation == acRhs.HasLocation
            && (!HasLocation || Location == acRhs.Location)
            && HasRotation == acRhs.HasRotation
            && (!HasRotation || Rotation == acRhs.Rotation)
            && CellId == acRhs.CellId
            && WorldSpaceId == acRhs.WorldSpaceId
            && ReferenceId == acRhs.ReferenceId;
    }

    uint32_t ServerId{};
    uint64_t DropId{};
    Inventory::Entry Item{};
    bool HasLocation{false};
    Vector3_NetQuantize Location{};
    bool HasRotation{false};
    Vector3_NetQuantize Rotation{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
};
