#pragma once

#include <ExtraData/ExtraDataList.h>
#include <Structs/Inventory.h>
#include <Games/Primitives.h>

/**
 * @brief Dispatched when the contents of an object or actor inventory changes locally.
 */
struct InventoryChangeEvent
{
    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem)
        : FormId(aFormId)
        , Item(std::move(arItem))
    {
    }

    InventoryChangeEvent(const uint32_t aFormId, Inventory::Entry arItem, bool aUpdateClients)
        : FormId(aFormId)
        , Item(std::move(arItem))
        , UpdateClients(aUpdateClients)
    {
    }

    uint32_t FormId{};
    Inventory::Entry Item{};
    bool UpdateClients = true;
};
