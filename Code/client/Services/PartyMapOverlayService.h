#pragma once

#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <mutex>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/SetWaypointEvent.h>

#include <Messages/NotifyPlayerCellChanged.h>
#include <Messages/NotifyPartyPositions.h>

struct World;

// CEF overlay to show party pins on the world map and set waypoints.
// - Shows party members and lets you set the in‑game waypoint to a member's
//   last known position/worldspace (no quests / fake actors).
// - Optional: can be extended to auto-follow a member's position.
struct PartyMapOverlayService
{
    PartyMapOverlayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PartyMapOverlayService() = default;

    TP_NOCOPYMOVE(PartyMapOverlayService);

private:
    struct LastInfo
    {
        glm::vec3 Pos{};
        uint64_t Tick{0};
        glm::vec3 Velocity{};
        std::chrono::steady_clock::time_point SampleTime{};
    };

    struct WorldspaceInfo
    {
        uint32_t WorldSpaceFormId{0}; // numeric TES form id (game id)
        bool HasWorld{false};
        bool IsInterior{false};
    };

    struct LastScreen
    {
        float sx{0.f};
        float sy{0.f};
        uint64_t Tick{0};
    };

    void OnUpdate(const UpdateEvent&) noexcept;
    void OnDisconnected(const DisconnectedEvent&) noexcept;
    void OnPartyJoined(const PartyJoinedEvent&) noexcept;
    void OnPartyLeft(const PartyLeftEvent&) noexcept;
    void OnPlayerCellChanged(const NotifyPlayerCellChanged& aMsg) noexcept;
    void OnPartyPositions(const NotifyPartyPositions& aMsg) noexcept;

    void PruneNonPartyEntries() noexcept;

    void SetWaypointFor(uint32_t aPlayerId) noexcept;
    void StoreLastInfo(uint32_t aPlayerId, const glm::vec3& aPos, uint64_t aTick) noexcept;

    // Approximate cross-world conversion using per-player anchors
    bool ComputeCrossWorldApprox(uint32_t aPlayerId, uint32_t aSrcWsId, const glm::vec3& aSrcPos,
                                 uint32_t aDstWsId, glm::vec3& aOutDstPos) const noexcept;

    World& m_world;

    // caches
    std::unordered_map<uint32_t, LastInfo> m_last;           // playerId -> last known pos (current world)
    std::unordered_map<uint32_t, WorldspaceInfo> m_worlds;   // playerId -> worldspace id (current world)
    // history of last known positions per worldspace (keyed by worldspace FormID)
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, glm::vec3>> m_lastPerWorld;
    // last projected screen pos cache to hide brief transition gaps
    std::unordered_map<uint32_t, LastScreen> m_lastScreen;
    uint32_t m_lastValidDisplayWorldId{0};
    mutable std::mutex m_cacheMutex;

    // connections
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
    entt::scoped_connection m_cellChangedConnection;
    entt::scoped_connection m_positionsConnection;
};
