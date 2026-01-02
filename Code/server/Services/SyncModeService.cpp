#include <Services/SyncModeService.h>

#include <Events/PacketEvent.h>
#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>

#include <Game/Player.h>
#include <GameServer.h>
#include <Messages/NotifyPlayerSyncMode.h>
#include <Messages/RequestSetSyncMode.h>
#include <World.h>

SyncModeService::SyncModeService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_playerJoinConnection = aDispatcher.sink<PlayerJoinEvent>().connect<&SyncModeService::OnPlayerJoin>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&SyncModeService::OnPlayerLeave>(this);
    m_requestConnection = aDispatcher.sink<PacketEvent<RequestSetSyncMode>>().connect<&SyncModeService::OnRequestSetSyncMode>(this);
}

void SyncModeService::OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept
{
    SendSnapshot(acEvent.pPlayer);

    // Inform other players about the newcomer (default mode is Normal unless overridden later).
    BroadcastMode(acEvent.pPlayer->GetId(), acEvent.pPlayer->GetSyncMode(), acEvent.pPlayer);
}

void SyncModeService::OnPlayerLeave(const PlayerLeaveEvent&) noexcept
{
    // No state to cleanup yet; sync mode lives on the Player object.
}

void SyncModeService::OnRequestSetSyncMode(const PacketEvent<RequestSetSyncMode>& acEvent) noexcept
{
    Player* const pPlayer = acEvent.pPlayer;
    if (!pPlayer)
        return;

    const SyncMode newMode = acEvent.Packet.Mode;
    if (pPlayer->GetSyncMode() == newMode)
        return;

    pPlayer->SetSyncMode(newMode);

    BroadcastMode(pPlayer->GetId(), newMode);
}

void SyncModeService::BroadcastMode(const uint32_t aPlayerId, const SyncMode aMode, Player* apIgnoredPlayer) const noexcept
{
    NotifyPlayerSyncMode notify{};
    notify.PlayerId = aPlayerId;
    notify.Mode = aMode;

    GameServer::Get()->SendToPlayers(notify, apIgnoredPlayer);
}

void SyncModeService::SendSnapshot(Player* apPlayer) const noexcept
{
    if (!apPlayer)
        return;

    NotifyPlayerSyncMode notify{};

    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        notify.PlayerId = pPlayer->GetId();
        notify.Mode = pPlayer->GetSyncMode();
        apPlayer->Send(notify);
    }
}
