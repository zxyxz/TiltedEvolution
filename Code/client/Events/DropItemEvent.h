#pragma once

#include <Structs/GameId.h>
#include <Structs/Guid.h>
#include <Structs/Inventory.h>
#include <Games/Primitives.h>

struct DropItemEvent
{
    DropItemEvent(uint32_t aActorFormId, Inventory::Entry aItem, Guid aClientDropId, NiPoint3 aLocation, NiPoint3 aRotation, uint32_t aHandleBits, GameId aCellId, GameId aWorldSpaceId, GameId aReferenceId)
        : ActorFormId(aActorFormId)
        , Item(std::move(aItem))
        , ClientDropId(aClientDropId)
        , Location(aLocation)
        , Rotation(aRotation)
        , HandleBits(aHandleBits)
        , CellId(aCellId)
        , WorldSpaceId(aWorldSpaceId)
        , ReferenceId(aReferenceId)
    {
    }

    uint32_t ActorFormId{};
    Inventory::Entry Item{};
    Guid ClientDropId{};
    NiPoint3 Location{};
    NiPoint3 Rotation{};
    uint32_t HandleBits{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
};
