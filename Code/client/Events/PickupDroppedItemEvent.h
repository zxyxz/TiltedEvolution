#pragma once

#include <Structs/Inventory.h>
#include <Structs/GameId.h>
#include <Games/Primitives.h>

struct PickupDroppedItemEvent
{
    PickupDroppedItemEvent(uint32_t aActorFormId, uint64_t aDropId)
        : ActorFormId(aActorFormId)
        , DropId(aDropId)
    {
    }

    uint32_t ActorFormId{};
    uint64_t DropId{};
    uint32_t ReferenceFormId{};
    bool HasItemData{false};
    Inventory::Entry Item{};
    bool HasLocation{false};
    NiPoint3 Location{};
    bool HasRotation{false};
    NiPoint3 Rotation{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
};
