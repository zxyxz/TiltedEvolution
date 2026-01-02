#pragma once

#include <cstdint>

struct World;
struct TransportService;
class Actor;

#include <Events/EquipmentChangeEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Events/UpdateEvent.h>

/**
 * @brief Manages inventories of actors and containers.
 *
 * The initial contents of actor inventories are synced through spawn messages through CharacterService
 * @see CharacterService
 * The initial contents of container inventories are synced through the ObjectService
 * @see ObjectService
 * The InventoryService manages any changes to both the current equipment of actors,
 * and the contents of both actor and object inventories.
 */
struct InventoryService
{
    InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~InventoryService() noexcept = default;

    TP_NOCOPYMOVE(InventoryService);

    /**
     * @brief Check weapon state.
     * @see RunWeaponStateUpdates
     */
    void OnUpdate(const UpdateEvent& acUpdateEvent) noexcept;
    /**
     * @brief Sends out inventory changes made by the local client.
     */
    void OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept;
    /**
     * @brief Sends out equipment changes made by the local client.
     */
    void OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept;

    /**
     * @brief Applies inventory changes sent by the server.
     */
    void OnNotifyInventoryChanges(const NotifyInventoryChanges& acMessage) noexcept;
    /**
     * @brief Applies equipment changes sent by the server.
     */
    void OnNotifyEquipmentChanges(const NotifyEquipmentChanges& acMessage) noexcept;

private:
    void ApplyEquipmentChange(Actor* pActor, const NotifyEquipmentChanges& acMessage) noexcept;
    void ProcessPendingEquipment() noexcept;
    void ProcessPendingEquipmentChanges() noexcept;
    void ProcessPendingEquipmentRequests() noexcept;
    void ProcessPendingInventoryChanges() noexcept;
    bool SendEquipmentChange(const EquipmentChangeEvent& acEvent) noexcept;
    enum class ActorReadinessStatus : uint8_t
    {
        Ready,
        MissingActor,
        Missing3D,
        MissingContainerData
    };
    ActorReadinessStatus EvaluateActorReadiness(Actor* pActor) const noexcept;
    static const char* DescribeReadiness(ActorReadinessStatus aStatus) noexcept;

    /**
     * Checks whether local actors their weapon draw states have changed,
     * and if so, send the new states to the server.
     */
    void RunWeaponStateUpdates() noexcept;
    /**
    * Checks whether an NPC's (local or remote) equipment is bugged (i.e. naked NPCS)
    * and resets their inventory.
    */
    void RunNakedNPCBugChecks() noexcept;

    bool TryApplyInventoryChange(const NotifyInventoryChanges& acMessage) noexcept;
    bool TryApplyEquipmentChange(const NotifyEquipmentChanges& acMessage) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_inventoryConnection;
    entt::scoped_connection m_equipmentConnection;
    entt::scoped_connection m_inventoryChangeConnection;
    entt::scoped_connection m_equipmentChangeConnection;

    TiltedPhoques::Vector<EquipmentChangeEvent> m_pendingEquipmentRequests;
    TiltedPhoques::Vector<NotifyEquipmentChanges> m_pendingEquipmentChanges;
    TiltedPhoques::Vector<NotifyInventoryChanges> m_pendingInventoryChanges;
};
