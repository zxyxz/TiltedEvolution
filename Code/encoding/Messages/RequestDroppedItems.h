#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/Vector3_NetQuantize.h>

struct RequestDroppedItems final : ClientMessage
{
    struct DiscoveryEntry
    {
        GameId ReferenceId{};
        GameId CellId{};
        GameId WorldSpaceId{};
        Inventory::Entry Item{};
        bool HasLocation{false};
        Vector3_NetQuantize Location{};
        bool HasRotation{false};
        Vector3_NetQuantize Rotation{};

        bool operator==(const DiscoveryEntry& acRhs) const noexcept
        {
            return ReferenceId == acRhs.ReferenceId && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId && Item == acRhs.Item && HasLocation == acRhs.HasLocation &&
                   (!HasLocation || Location == acRhs.Location) && HasRotation == acRhs.HasRotation && (!HasRotation || Rotation == acRhs.Rotation);
        }
    };

    static constexpr ClientOpcode Opcode = kRequestDroppedItems;

    RequestDroppedItems()
        : ClientMessage(Opcode)
    {
    }

    virtual ~RequestDroppedItems() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const RequestDroppedItems& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && RequestAll == acRhs.RequestAll && HasCellFilter == acRhs.HasCellFilter && (!HasCellFilter || CellId == acRhs.CellId) && HasWorldSpaceFilter == acRhs.HasWorldSpaceFilter &&
               (!HasWorldSpaceFilter || WorldSpaceId == acRhs.WorldSpaceId) && Discoveries == acRhs.Discoveries;
    }

    uint32_t RequestId{};
    bool RequestAll{false};
    bool HasCellFilter{false};
    GameId CellId{};
    bool HasWorldSpaceFilter{false};
    GameId WorldSpaceId{};
    TiltedPhoques::Vector<DiscoveryEntry> Discoveries{};
};
