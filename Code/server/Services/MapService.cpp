#include <Services/MapService.h>
#include <GameServer.h>

#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/RequestRemoveWaypoint.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/PartyFastTravelMarkersRequest.h>
#include <Messages/NotifyPartyFastTravelMarkers.h>

#include <Events/UpdateEvent.h>

MapService::MapService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
      m_playerSetWaypointConnection = 
          aDispatcher.sink<PacketEvent<RequestSetWaypoint>>().connect<&MapService::OnSetWaypointRequest>(this);
      m_playerRemoveWaypointConnection =
          aDispatcher.sink<PacketEvent<RequestRemoveWaypoint>>().connect<&MapService::OnRemoveWaypointRequest>(this);
      m_partyFastTravelMarkersConnection =
          aDispatcher.sink<PacketEvent<PartyFastTravelMarkersRequest>>().connect<&MapService::OnPartyFastTravelMarkersRequest>(this);
      m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&MapService::OnUpdate>(this);
}

void MapService::OnSetWaypointRequest(const PacketEvent<RequestSetWaypoint>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    NotifySetWaypoint notify{};
    notify.Position = message.Position;
    notify.WorldSpaceFormID = message.WorldSpaceFormID;

    const auto& partyComponent = acMessage.pPlayer->GetParty();
    if (!partyComponent.JoinedPartyId.has_value())
        return;

    GameServer::Get()->SendToParty(notify, partyComponent, acMessage.GetSender());
}

void MapService::OnRemoveWaypointRequest(const PacketEvent<RequestRemoveWaypoint>& acMessage) const noexcept
{
    NotifyRemoveWaypoint notify{};

    const auto& partyComponent = acMessage.pPlayer->GetParty();
    if (!partyComponent.JoinedPartyId.has_value())
        return;

    GameServer::Get()->SendToParty(notify, partyComponent, acMessage.GetSender());
}

void MapService::OnPartyFastTravelMarkersRequest(const PacketEvent<PartyFastTravelMarkersRequest>& acMessage) noexcept
{
    // Client-side gating via ServerSettings should prevent sending/receiving when disabled.
    // Keep server-side behavior permissive to avoid breaking older clients and mixed versions.

    const auto& partyComponent = acMessage.pPlayer->GetParty();
    if (!partyComponent.JoinedPartyId.has_value())
        return;

    const uint32_t partyId = *partyComponent.JoinedPartyId;
    const uint32_t senderPlayerId = acMessage.pPlayer->GetId();

    auto& state = m_partyFastTravelMarkers[partyId];
    auto* pParty = m_world.GetPartyService().GetById(partyId);
    if (!pParty)
        return;

    // Keep the cached state accurate for the current party membership (avoid leaking markers from players who left).
    TiltedPhoques::Set<uint32_t> memberIds{};
    memberIds.reserve(pParty->Members.size());
    for (auto* pMember : pParty->Members)
    {
        if (pMember)
            memberIds.insert(pMember->GetId());
    }

    auto& byPlayer = state.ByPlayer;
    TiltedPhoques::Vector<uint32_t> toErase{};
    toErase.reserve(byPlayer.size());

    for (const auto& entry : byPlayer)
    {
        if (!memberIds.contains(entry.first))
            toErase.push_back(entry.first);
    }

    for (const auto playerId : toErase)
        byPlayer.erase(playerId);

    // Rebuild union from per-player sets.
    state.Union.clear();
    for (const auto& entry : byPlayer)
    {
        for (const auto& marker : entry.second)
            state.Union.insert(marker);
    }

    auto& senderSet = byPlayer[senderPlayerId];
    if (acMessage.Packet.FullSync)
        senderSet.clear();

    TiltedPhoques::Vector<GameId> newToParty{};
    newToParty.reserve(acMessage.Packet.Markers.size());

    for (const auto& marker : acMessage.Packet.Markers)
    {
        if (!marker)
            continue;

        senderSet.insert(marker);

        if (state.Union.insert(marker).second)
            newToParty.push_back(marker);
    }

    if (!newToParty.empty())
    {
        NotifyPartyFastTravelMarkers notify{};
        notify.Markers = newToParty;

        GameServer::Get()->SendToParty(notify, partyComponent, acMessage.GetSender());
    }

    TiltedPhoques::Vector<GameId> missingForSender{};
    missingForSender.reserve(state.Union.size());

    for (const auto& marker : state.Union)
    {
        if (!senderSet.contains(marker))
            missingForSender.push_back(marker);
    }

    if (!missingForSender.empty())
    {
        NotifyPartyFastTravelMarkers notify{};
        notify.Markers = std::move(missingForSender);

        acMessage.pPlayer->Send(notify);
    }
}

void MapService::OnUpdate(const UpdateEvent&) noexcept
{
    const auto now = GameServer::Get()->GetTick();
    if (m_nextCleanupTick > now)
        return;

    // Clean up every 30 seconds.
    m_nextCleanupTick = now + 30000;

    auto it = m_partyFastTravelMarkers.begin();
    while (it != m_partyFastTravelMarkers.end())
    {
        const uint32_t partyId = it->first;
        const auto* pParty = m_world.GetPartyService().GetById(partyId);
        if (!pParty)
        {
            it = m_partyFastTravelMarkers.erase(it);
            continue;
        }

        // Prune per-player caches for players no longer in the party.
        TiltedPhoques::Set<uint32_t> memberIds{};
        memberIds.reserve(pParty->Members.size());
        for (auto* pMember : pParty->Members)
        {
            if (pMember)
                memberIds.insert(pMember->GetId());
        }

        auto& state = const_cast<PartyFastTravelMarkerState&>(it->second);
        auto& byPlayer = state.ByPlayer;
        TiltedPhoques::Vector<uint32_t> toErase{};
        toErase.reserve(byPlayer.size());

        for (const auto& entry : byPlayer)
        {
            if (!memberIds.contains(entry.first))
                toErase.push_back(entry.first);
        }

        for (const auto playerId : toErase)
            byPlayer.erase(playerId);

        // Rebuild union from current party members to avoid keeping markers contributed only by players who left.
        TiltedPhoques::Set<GameId> newUnion{};
        for (const auto& entry : byPlayer)
        {
            const auto& markers = entry.second;
            for (const auto& marker : markers)
                newUnion.insert(marker);
        }
        state.Union = std::move(newUnion);

        ++it;
    }
}
