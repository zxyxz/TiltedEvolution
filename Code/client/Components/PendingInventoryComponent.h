#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/Inventory.h>

struct PendingInventoryComponent
{
    PendingInventoryComponent() = default;

    PendingInventoryComponent(Inventory aInventory, bool aIsDead, bool aIsWeaponDrawn)
        : InventoryContent(std::move(aInventory))
        , IsDead(aIsDead)
        , IsWeaponDrawn(aIsWeaponDrawn)
    {
    }

    Inventory InventoryContent{};
    bool IsDead{false};
    bool IsWeaponDrawn{false};
};
