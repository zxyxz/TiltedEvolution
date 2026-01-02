#pragma once

#include <unordered_map>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>

struct World;
struct ImguiService;

/**
 * Draws simple directional name markers for party members that are NOT currently
 * spawned locally (i.e., in a different cell/world). Pairs with QuestService which
 * shows native objective markers for in-range members.
 */
struct PartyMarkerOverlayService
{
    PartyMarkerOverlayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PartyMarkerOverlayService() = default;

    TP_NOCOPYMOVE(PartyMarkerOverlayService);

private:
    // Data we cache per player for far-range markers
    struct LastInfo
    {
        NiPoint3 Pos{};      // last known world position
        uint64_t Tick{0};    // last update tick
    };

    // Event handlers
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDraw() noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnPartyJoined(const PartyJoinedEvent&) noexcept;
    void OnPartyLeft(const PartyLeftEvent&) noexcept;

    // Helpers
    void PruneNonPartyEntries() noexcept;
    bool HasLiveEntityForPlayer(uint32_t aPlayerId) const noexcept;

    World& m_world;

    // last known position for players
    std::unordered_map<uint32_t, LastInfo> m_last;

    // connections
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_drawImGuiConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
};

