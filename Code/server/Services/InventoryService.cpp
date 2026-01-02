#include "InventoryService.h"

#include <Components.h>
#include <World.h>
#include <GameServer.h>

#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/DrawWeaponRequest.h>


InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_inventoryChangeConnection = aDispatcher.sink<PacketEvent<RequestInventoryChanges>>().connect<&InventoryService::OnInventoryChanges>(this);
    m_equipmentChangeConnection = aDispatcher.sink<PacketEvent<RequestEquipmentChanges>>().connect<&InventoryService::OnEquipmentChanges>(this);
    m_drawWeaponConnection = aDispatcher.sink<PacketEvent<DrawWeaponRequest>>().connect<&InventoryService::OnWeaponDrawnRequest>(this);
}

void InventoryService::OnInventoryChanges(const PacketEvent<RequestInventoryChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    const auto entity = m_world.TryResolveEntity(message.ServerId);
    if (!entity)
    {
        spdlog::warn("Inventory update requested for unknown entity {:X}", message.ServerId);
        return;
    }

    auto view = m_world.view<InventoryComponent, OwnerComponent>();

    const auto it = view.find(*entity);

    if (it != view.end())
    {
        auto& ownerComponent = view.get<OwnerComponent>(*it);
        if (ownerComponent.GetOwner() != acMessage.pPlayer)
        {
            spdlog::warn("Inventory change denied for {:X}: player {:X} not owner", message.ServerId, acMessage.pPlayer->GetConnectionId());
            return;
        }

        auto& inventoryComponent = view.get<InventoryComponent>(*it);
        inventoryComponent.Content.AddOrRemoveEntry(message.Item);
    }
    else
    {
        spdlog::warn("Inventory change requested for entity {:X} without InventoryComponent", message.ServerId);
        return;
    }

    if (!message.UpdateClients)
        return;

    NotifyInventoryChanges notify;
    notify.ServerId = message.ServerId;
    notify.Item = message.Item;

    if (!GameServer::Get()->SendToPlayersInRange(notify, *entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void InventoryService::OnEquipmentChanges(const PacketEvent<RequestEquipmentChanges>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    const auto entity = m_world.TryResolveEntity(message.ServerId);
    if (!entity)
    {
        spdlog::warn("Equipment update requested for unknown entity {:X}", message.ServerId);
        return;
    }

    auto view = m_world.view<InventoryComponent, OwnerComponent>();

    const auto it = view.find(*entity);

    if (it != view.end())
    {
        auto& ownerComponent = view.get<OwnerComponent>(*it);
        if (ownerComponent.GetOwner() != acMessage.pPlayer)
        {
            spdlog::warn("Equipment change denied for {:X}: player {:X} not owner", message.ServerId, acMessage.pPlayer->GetConnectionId());
            return;
        }

        auto& inventoryComponent = view.get<InventoryComponent>(*it);
        inventoryComponent.Content.UpdateEquipment(message.CurrentInventory);
    }
    else
    {
        spdlog::warn("Equipment change requested for entity {:X} without InventoryComponent", message.ServerId);
        return;
    }

    const auto effectiveCount = message.Count == 0 ? 1 : message.Count;

    NotifyEquipmentChanges notify;
    notify.ServerId = message.ServerId;
    notify.ItemId = message.ItemId;
    notify.EquipSlotId = message.EquipSlotId;
    notify.Count = effectiveCount;
    notify.Unequip = message.Unequip;
    notify.IsSpell = message.IsSpell;
    notify.IsShout = message.IsShout;

    if (!GameServer::Get()->SendToPlayersInRange(notify, *entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void InventoryService::OnWeaponDrawnRequest(const PacketEvent<DrawWeaponRequest>& acMessage) noexcept
{
    auto& message = acMessage.Packet;

    const auto entity = m_world.TryResolveEntity(message.Id);
    if (!entity)
    {
        spdlog::debug("Weapon drawn request for unknown entity {:X}", message.Id);
        return;
    }

    auto characterView = m_world.view<CharacterComponent, OwnerComponent>();
    const auto it = characterView.find(*entity);

    if (it != std::end(characterView) && characterView.get<OwnerComponent>(*it).GetOwner() == acMessage.pPlayer)
    {
        auto& characterComponent = characterView.get<CharacterComponent>(*it);
        characterComponent.SetWeaponDrawn(message.IsWeaponDrawn);
        spdlog::debug("Updating weapon drawn state {:x}:{}", message.Id, message.IsWeaponDrawn);
    }
}
