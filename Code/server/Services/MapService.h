#pragma once

#include <Events/PacketEvent.h>
#include <Structs/GameId.h>

struct World;
struct UpdateEvent;
struct RequestSetWaypoint;
struct RequestRemoveWaypoint;
struct PartyFastTravelMarkersRequest;

/**
 * @brief Handles player specific actions that might change the information needed by other clients about that player.
 */
struct MapService
{
    MapService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~MapService() noexcept = default;

    TP_NOCOPYMOVE(MapService);

  protected:
    void OnSetWaypointRequest(const PacketEvent<RequestSetWaypoint>& acMessage) const noexcept;
    void OnRemoveWaypointRequest(const PacketEvent<RequestRemoveWaypoint>& acMessage) const noexcept;
    void OnPartyFastTravelMarkersRequest(const PacketEvent<PartyFastTravelMarkersRequest>& acMessage) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

  private:
    struct PartyFastTravelMarkerState
    {
        TiltedPhoques::Set<GameId> Union{};
        TiltedPhoques::Map<uint32_t, TiltedPhoques::Set<GameId>> ByPlayer{};
    };

    World& m_world;
    TiltedPhoques::Map<uint32_t, PartyFastTravelMarkerState> m_partyFastTravelMarkers{};
    uint64_t m_nextCleanupTick{0};

    entt::scoped_connection m_playerSetWaypointConnection;
    entt::scoped_connection m_playerRemoveWaypointConnection;
    entt::scoped_connection m_partyFastTravelMarkersConnection;
    entt::scoped_connection m_updateConnection;
};
