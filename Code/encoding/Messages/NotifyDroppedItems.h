#pragma once

#include "Message.h"
#include <Structs/GameId.h>
#include <Structs/Inventory.h>
#include <Structs/ServerItemType.h>
#include <Structs/Vector3_NetQuantize.h>
#include <TiltedCore/Stl.hpp>

struct NotifyDroppedItems final : ServerMessage
{
    struct Entry
    {
        uint64_t DropId{};
        uint32_t ServerId{};
        uint32_t ActorFormId{};
        ServerItemType Type{ServerItemType::Dropped};
        Inventory::Entry Item{};
        bool HasLocation{false};
        Vector3_NetQuantize Location{};
        bool HasRotation{false};
        Vector3_NetQuantize Rotation{};
        GameId CellId{};
        GameId WorldSpaceId{};
        GameId ReferenceId{};
        uint64_t SpawnEpoch{};

        bool operator==(const Entry& acRhs) const noexcept
        {
            return DropId == acRhs.DropId && ServerId == acRhs.ServerId && ActorFormId == acRhs.ActorFormId && Type == acRhs.Type && Item == acRhs.Item && HasLocation == acRhs.HasLocation &&
                   (!HasLocation || Location == acRhs.Location) && HasRotation == acRhs.HasRotation && (!HasRotation || Rotation == acRhs.Rotation) && CellId == acRhs.CellId && WorldSpaceId == acRhs.WorldSpaceId &&
                   ReferenceId == acRhs.ReferenceId && SpawnEpoch == acRhs.SpawnEpoch;
        }
    };

    static constexpr ServerOpcode Opcode = kNotifyDroppedItems;

    NotifyDroppedItems()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyDroppedItems() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyDroppedItems& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && RequestId == acRhs.RequestId && Entries == acRhs.Entries && CreationEnginePickedUpReferences == acRhs.CreationEnginePickedUpReferences;
    }

    uint32_t RequestId{};
    TiltedPhoques::Vector<Entry> Entries{};
    TiltedPhoques::Vector<GameId> CreationEnginePickedUpReferences{};
};
