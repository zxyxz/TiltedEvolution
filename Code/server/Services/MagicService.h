#pragma once

#include <Events/PacketEvent.h>
#include <Messages/RemoveSpellRequest.h>

struct World;
struct SpellCastRequest;
struct InterruptCastRequest;
struct AddTargetRequest;
struct HealingProximityRequest;

/**
 * @brief Relays spell casting and magic effects.
 */
struct MagicService
{
    MagicService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~MagicService() noexcept = default;

    TP_NOCOPYMOVE(MagicService);

protected:
    /**
     * @brief Relays spell cast messages to other clients and broadcasts healing proximity.
     */
    void OnSpellCastRequest(const PacketEvent<SpellCastRequest>& acMessage) const noexcept;
    /**
     * @brief Relays spell interrupt messages to other clients.
     */
    void OnInterruptCastRequest(const PacketEvent<InterruptCastRequest>& acMessage) const noexcept;
    /**
     * @brief Relays magic effect messages to other clients.
     */
    void OnAddTargetRequest(const PacketEvent<AddTargetRequest>& acMessage) const noexcept;
    /**
    * @brief Relays spell removal messages to other clients.
    */
    void OnRemoveSpellRequest(const PacketEvent<RemoveSpellRequest>& acMessage) const noexcept;
    /**
    * @brief Broadcasts healing proximity event to nearby downed players.
    */
    void OnHealingProximityRequest(const PacketEvent<HealingProximityRequest>& acMessage) const noexcept;


private:
    World& m_world;

    entt::scoped_connection m_spellCastConnection;
    entt::scoped_connection m_interruptCastConnection;
    entt::scoped_connection m_addTargetConnection;
    entt::scoped_connection m_removeSpellConnection;
    entt::scoped_connection m_healingProximityConnection;
};
