#pragma once

#include <Structs/GameId.h>
#include <Events/EventDispatcher.h>
#include <Games/Events.h>

struct World;
struct TransportService;

struct UpdateEvent;
struct SetWaypointEvent;
struct RemoveWaypointEvent;
struct NotifySetWaypoint;
struct NotifyRemoveWaypoint;
struct NotifyPartyInfo;
struct NotifyPartyFastTravelMarkers;
struct PartyJoinedEvent;

/**
 * @brief Handles map-related synchronization (waypoints, party fast travel markers).
 */
struct MapService : BSTEventSink<TESLoadGameEvent>
{
    MapService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~MapService() noexcept = default;

    TP_NOCOPYMOVE(MapService);

  protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnSetWaypoint(const SetWaypointEvent& acMessage) noexcept;
    void OnRemoveWaypoint(const RemoveWaypointEvent& acMessage) noexcept;
    void OnNotifySetWaypoint(const NotifySetWaypoint& acMessage) noexcept;
    void OnNotifyRemoveWaypoint(const NotifyRemoveWaypoint& acMessage) noexcept;
    void OnNotifyPartyInfo(const NotifyPartyInfo& acMessage) noexcept;
    void OnNotifyPartyFastTravelMarkers(const NotifyPartyFastTravelMarkers& acMessage) noexcept;
    void OnPartyJoined(const PartyJoinedEvent& acMessage) noexcept;
    BSTEventResult OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*) override;

  private:
    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    void SendFastTravelMarkers(const TiltedPhoques::Vector<GameId>& aMarkers, bool aAllowEmpty, bool aFullSync) noexcept;
    TiltedPhoques::Vector<GameId> CollectLocalFastTravelMarkers() const noexcept;
    void ProcessPendingFastTravelMarkers() noexcept;
    void SyncFastTravelMarkers(bool aForceSendEvenIfEmpty) noexcept;

    uint64_t m_nextMarkerScanTick{0};
    TiltedPhoques::Set<GameId> m_knownFastTravelMarkers{};
    TiltedPhoques::Vector<GameId> m_pendingFastTravelMarkers{};

    TiltedPhoques::Set<uint32_t> m_lastPartyMembers{};
    uint32_t m_lastLeaderPlayerId{0};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playerSetWaypointConnection;
    entt::scoped_connection m_playerRemoveWaypointConnection;
    entt::scoped_connection m_playerNotifySetWaypointConnection;
    entt::scoped_connection m_playerNotifyRemoveWaypointConnection;
    entt::scoped_connection m_partyInfoConnection;
    entt::scoped_connection m_partyFastTravelMarkersConnection;
    entt::scoped_connection m_partyJoinedConnection;
};
