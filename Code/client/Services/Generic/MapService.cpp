#include <Services/MapService.h>

#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/RemoveWaypointEvent.h>
#include <Messages/PartyFastTravelMarkersRequest.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/NotifyPartyInfo.h>
#include <Messages/NotifyPartyFastTravelMarkers.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/RequestRemoveWaypoint.h>

#include <ExtraData/ExtraMapMarker.h>
#include <Events/EventDispatcher.h>

#include <PlayerCharacter.h>
#include <Utils.h>
#include <Forms/TESWorldSpace.h>

MapService::MapService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld), m_dispatcher(aDispatcher), m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&MapService::OnUpdate>(this);

    m_playerNotifySetWaypointConnection =
        m_dispatcher.sink<NotifySetWaypoint>().connect<&MapService::OnNotifySetWaypoint>(this);
    m_playerNotifyRemoveWaypointConnection =
        m_dispatcher.sink<NotifyRemoveWaypoint>().connect<&MapService::OnNotifyRemoveWaypoint>(this);
    m_playerSetWaypointConnection =
        m_dispatcher.sink<SetWaypointEvent>().connect<&MapService::OnSetWaypoint>(this);
    m_playerRemoveWaypointConnection =
        m_dispatcher.sink<RemoveWaypointEvent>().connect<&MapService::OnRemoveWaypoint>(this);

    m_partyInfoConnection =
        m_dispatcher.sink<NotifyPartyInfo>().connect<&MapService::OnNotifyPartyInfo>(this);
    m_partyFastTravelMarkersConnection =
        m_dispatcher.sink<NotifyPartyFastTravelMarkers>().connect<&MapService::OnNotifyPartyFastTravelMarkers>(this);
    m_partyJoinedConnection =
        m_dispatcher.sink<PartyJoinedEvent>().connect<&MapService::OnPartyJoined>(this);

    EventDispatcherManager::Get()->loadGameEvent.RegisterSink(this);
}

namespace
{
constexpr uint8_t kMapMarkerFlag_Visible = 1 << 0;
constexpr uint8_t kMapMarkerFlag_CanTravelTo = 1 << 1;

struct LocationDiscoveryEvent
{
    MapMarkerData* mapMarkerData;
    const char* worldspaceID;
};

inline EventDispatcher<LocationDiscoveryEvent>* GetLocationDiscoveryEventDispatcher() noexcept
{
    using TGetEventSource = EventDispatcher<LocationDiscoveryEvent>*();

    // CommonLibSSE: RE::LocationDiscovery::GetEventSource() -> RELOCATION_ID(40056, 41067)
    POINTER_SKYRIMSE(TGetEventSource, s_getEventSource, 40056);

    return s_getEventSource.Get()();
}

inline bool IsFastTravelMarker(TESObjectREFR* apRefr) noexcept
{
    if (!apRefr)
        return false;

    auto* pExtraData = apRefr->GetExtraDataList();
    if (!pExtraData)
        return false;

    auto* pExtraMapMarker = static_cast<const ExtraMapMarker*>(pExtraData->GetByType(ExtraDataType::MapMarker));
    if (!pExtraMapMarker || !pExtraMapMarker->mapData)
        return false;

    const uint8_t flags = pExtraMapMarker->mapData->flags;
    return (flags & kMapMarkerFlag_Visible) && (flags & kMapMarkerFlag_CanTravelTo);
}

inline bool DiscoverFastTravelMarker(TESObjectREFR* apMarkerRefr) noexcept
{
    if (!apMarkerRefr)
        return false;

    auto* pExtraData = apMarkerRefr->GetExtraDataList();
    if (!pExtraData)
        return false;

    auto* pExtraMapMarker = static_cast<ExtraMapMarker*>(pExtraData->GetByType(ExtraDataType::MapMarker));
    if (!pExtraMapMarker || !pExtraMapMarker->mapData)
        return false;

    const uint8_t oldFlags = pExtraMapMarker->mapData->flags;
    const bool wasFastTravelMarker = (oldFlags & kMapMarkerFlag_Visible) && (oldFlags & kMapMarkerFlag_CanTravelTo);

    pExtraMapMarker->mapData->flags |= (kMapMarkerFlag_Visible | kMapMarkerFlag_CanTravelTo);

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    const auto handle = apMarkerRefr->GetHandle();
    if (!handle)
        return false;

    auto* pCurrentMarkers = pPlayer->GetCurrentMapMarkers();
    if (!pCurrentMarkers)
        return true;

    auto& currentMarkers = *pCurrentMarkers;
    for (uint32_t i = 0; i < currentMarkers.length; ++i)
    {
        if (currentMarkers[i].handle.iBits == handle.handle.iBits)
            return true;
    }

    const uint32_t oldLen = currentMarkers.length;
    currentMarkers.Resize(oldLen + 1);
    currentMarkers[oldLen] = handle;

    // Fire the game's location discovery event so scripts/stat tracking/UI can react, but avoid refiring if already discovered.
    if (!wasFastTravelMarker)
    {
        if (auto* pDispatcher = GetLocationDiscoveryEventDispatcher(); pDispatcher != nullptr)
        {
            const char* pWorldspaceName = "";
            if (TESWorldSpace* pWorldSpace = apMarkerRefr->GetWorldSpace(); pWorldSpace != nullptr)
            {
                if (pWorldSpace->fullName.value.AsAscii() != nullptr)
                    pWorldspaceName = pWorldSpace->fullName.value.AsAscii();
            }

            LocationDiscoveryEvent ev{};
            ev.mapMarkerData = pExtraMapMarker->mapData;
            ev.worldspaceID = pWorldspaceName;

            pDispatcher->PushEvent(&ev);
        }
    }

    return true;
}
}

void MapService::OnUpdate(const UpdateEvent&) noexcept
{
    ProcessPendingFastTravelMarkers();

    if (!m_transport.IsConnected())
        return;

    if (!m_world.Get().GetPartyService().IsInParty())
        return;

    if (!m_world.GetServerSettings().SyncPartyFastTravelMarkers)
        return;

    const auto now = m_transport.GetClock().GetCurrentTick();
    if (m_nextMarkerScanTick > now)
        return;

    // Scan at most twice per second.
    m_nextMarkerScanTick = now + 500;

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const auto* pCurrentMarkers = pPlayer->GetCurrentMapMarkers();
    if (!pCurrentMarkers)
        return;

    const auto& currentMarkers = *pCurrentMarkers;

    TiltedPhoques::Vector<GameId> newlyDiscovered{};
    newlyDiscovered.reserve(currentMarkers.length);

    ModSystem& modSystem = m_world.Get().GetModSystem();

    for (uint32_t i = 0; i < currentMarkers.length; ++i)
    {
        const uint32_t handleBits = currentMarkers[i].handle.iBits;
        if (handleBits == 0)
            continue;

        TESObjectREFR* pMarker = TESObjectREFR::GetByHandle(handleBits);
        if (!pMarker)
            continue;

        if (!IsFastTravelMarker(pMarker))
            continue;

        GameId markerId{};
        if (!modSystem.GetServerModId(pMarker->formID, markerId))
            continue;

        if (m_knownFastTravelMarkers.insert(markerId).second)
            newlyDiscovered.push_back(markerId);
    }

    SendFastTravelMarkers(newlyDiscovered, /*aAllowEmpty*/ false, /*aFullSync*/ false);
}

void MapService::OnSetWaypoint(const SetWaypointEvent& acMessage) noexcept
{
    if (!m_transport.IsConnected())
        return;

    RequestSetWaypoint request{};
    request.Position = acMessage.Position;

    ModSystem& modSystem = m_world.Get().GetModSystem();
    modSystem.GetServerModId(acMessage.WorldSpaceFormID, request.WorldSpaceFormID);

    m_transport.Send(request);
}

void MapService::OnRemoveWaypoint(const RemoveWaypointEvent& acMessage) noexcept
{
    if (!m_transport.IsConnected())
        return;

    RequestRemoveWaypoint request{};
    m_transport.Send(request);
}

void MapService::OnNotifySetWaypoint(const NotifySetWaypoint& acMessage) noexcept
{
    NiPoint3 pos{};
    pos.x = acMessage.Position.x;
    pos.y = acMessage.Position.y;
    pos.z = acMessage.Position.z;

    ModSystem& modSystem = m_world.Get().GetModSystem();
    const uint32_t cWorldSpaceID = modSystem.GetGameId(acMessage.WorldSpaceFormID);
    TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(cWorldSpaceID));

    PlayerCharacter::Get()->SetWaypoint(&pos, pWorldSpace);
}

void MapService::OnNotifyRemoveWaypoint(const NotifyRemoveWaypoint& acMessage) noexcept
{
    PlayerCharacter::Get()->RemoveWaypoint();
}

void MapService::OnNotifyPartyFastTravelMarkers(const NotifyPartyFastTravelMarkers& acMessage) noexcept
{
    if (!m_world.GetServerSettings().SyncPartyFastTravelMarkers)
        return;

    // Queue for retry to handle missing mod resolution / unloaded references.
    for (const auto& marker : acMessage.Markers)
    {
        if (!marker)
            continue;

        if (m_knownFastTravelMarkers.insert(marker).second)
            m_pendingFastTravelMarkers.push_back(marker);
    }

    ProcessPendingFastTravelMarkers();
}

void MapService::OnNotifyPartyInfo(const NotifyPartyInfo& acMessage) noexcept
{
    if (!m_transport.IsConnected())
        return;

    if (!m_world.Get().GetPartyService().IsInParty())
        return;

    if (!m_world.GetServerSettings().SyncPartyFastTravelMarkers)
        return;

    TiltedPhoques::Set<uint32_t> newMembers{};
    newMembers.reserve(acMessage.PlayerIds.size());
    for (const auto playerId : acMessage.PlayerIds)
        newMembers.insert(playerId);

    bool membersChanged = (newMembers.size() != m_lastPartyMembers.size());
    if (!membersChanged)
    {
        for (const auto playerId : newMembers)
        {
            if (!m_lastPartyMembers.contains(playerId))
            {
                membersChanged = true;
                break;
            }
        }
    }

    const bool leaderChanged = (m_lastLeaderPlayerId != 0) && (m_lastLeaderPlayerId != acMessage.LeaderPlayerId);

    // Update cached party snapshot.
    m_lastPartyMembers = std::move(newMembers);
    m_lastLeaderPlayerId = acMessage.LeaderPlayerId;

    if (membersChanged || leaderChanged)
    {
        spdlog::debug("Party updated: triggering fast travel marker resync (membersChanged={}, leaderChanged={})", membersChanged,
                      leaderChanged);
        SyncFastTravelMarkers(/*aForceSendEvenIfEmpty*/ true);
    }
}

void MapService::OnPartyJoined(const PartyJoinedEvent&) noexcept
{
    if (!m_world.GetServerSettings().SyncPartyFastTravelMarkers)
        return;

    // Full sync on join to ensure two-way union.
    const auto& partyService = m_world.Get().GetPartyService();
    m_lastPartyMembers.clear();
    for (const auto playerId : partyService.GetPartyMembers())
        m_lastPartyMembers.insert(playerId);
    m_lastLeaderPlayerId = partyService.GetLeaderPlayerId();

    spdlog::debug("Party joined: syncing fast travel markers");
    SyncFastTravelMarkers(/*aForceSendEvenIfEmpty*/ true);
}

void MapService::SyncFastTravelMarkers(bool aForceSendEvenIfEmpty) noexcept
{
    if (!m_world.GetServerSettings().SyncPartyFastTravelMarkers)
        return;

    // Force a quick scan and also send a full marker list (or an empty request) to receive the party union.
    m_nextMarkerScanTick = 0;

    const auto markers = CollectLocalFastTravelMarkers();
    for (const auto& marker : markers)
        m_knownFastTravelMarkers.insert(marker);

    spdlog::debug("Fast travel marker sync: sending {} markers (forceEmpty={})", markers.size(), aForceSendEvenIfEmpty);
    SendFastTravelMarkers(markers, /*aAllowEmpty*/ aForceSendEvenIfEmpty, /*aFullSync*/ true);
}

void MapService::SendFastTravelMarkers(const TiltedPhoques::Vector<GameId>& aMarkers, bool aAllowEmpty, bool aFullSync) noexcept
{
    if (aMarkers.empty() && !aAllowEmpty)
        return;

    if (!m_transport.IsConnected())
        return;

    if (!m_world.Get().GetPartyService().IsInParty())
        return;

    if (!m_world.GetServerSettings().SyncPartyFastTravelMarkers)
        return;

    PartyFastTravelMarkersRequest request{};
    request.FullSync = aFullSync;
    request.Markers = aMarkers;
    m_transport.Send(request);
}

BSTEventResult MapService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*) 
{
    // Switching saves can change which markers are actually discovered; clear local caches so party sync can re-unlock correctly.
    spdlog::info("Load game: resetting fast travel marker sync state");

    m_knownFastTravelMarkers.clear();
    m_pendingFastTravelMarkers.clear();
    m_nextMarkerScanTick = 0;

    if (m_world.GetServerSettings().SyncPartyFastTravelMarkers && m_transport.IsConnected() && m_world.Get().GetPartyService().IsInParty())
        SyncFastTravelMarkers(/*aForceSendEvenIfEmpty*/ true);

    return BSTEventResult::kOk;
}

TiltedPhoques::Vector<GameId> MapService::CollectLocalFastTravelMarkers() const noexcept
{
    TiltedPhoques::Set<GameId> unique{};

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return {};

    const auto* pCurrentMarkers = pPlayer->GetCurrentMapMarkers();
    if (!pCurrentMarkers)
        return {};

    const auto& currentMarkers = *pCurrentMarkers;
    unique.reserve(currentMarkers.length);

    ModSystem& modSystem = m_world.Get().GetModSystem();

    for (uint32_t i = 0; i < currentMarkers.length; ++i)
    {
        const uint32_t handleBits = currentMarkers[i].handle.iBits;
        if (handleBits == 0)
            continue;

        TESObjectREFR* pMarker = TESObjectREFR::GetByHandle(handleBits);
        if (!pMarker)
            continue;

        if (!IsFastTravelMarker(pMarker))
            continue;

        GameId markerId{};
        if (modSystem.GetServerModId(pMarker->formID, markerId))
            unique.insert(markerId);
    }

    TiltedPhoques::Vector<GameId> out{};
    out.reserve(unique.size());
    for (const auto& marker : unique)
        out.push_back(marker);

    return out;
}

void MapService::ProcessPendingFastTravelMarkers() noexcept
{
    if (!m_world.GetServerSettings().SyncPartyFastTravelMarkers)
        return;

    if (m_pendingFastTravelMarkers.empty())
        return;

    ModSystem& modSystem = m_world.Get().GetModSystem();

    TiltedPhoques::Vector<GameId> remaining{};
    remaining.reserve(m_pendingFastTravelMarkers.size());

    for (const auto& marker : m_pendingFastTravelMarkers)
    {
        const uint32_t gameFormId = modSystem.GetGameId(marker);
        if (gameFormId == 0)
        {
            remaining.push_back(marker);
            continue;
        }

        TESObjectREFR* pMarker = Cast<TESObjectREFR>(TESForm::GetById(gameFormId));
        if (!pMarker)
        {
            remaining.push_back(marker);
            continue;
        }

        if (!DiscoverFastTravelMarker(pMarker))
        {
            remaining.push_back(marker);
            continue;
        }
    }

    m_pendingFastTravelMarkers = std::move(remaining);
}
