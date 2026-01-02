#pragma once

#include <Structs/SyncMode.h>

#include <Events/PacketEvent.h>

struct World;
struct Player;
struct PlayerJoinEvent;
struct PlayerLeaveEvent;
struct RequestSetSyncMode;

/**
 * @brief Tracks player sync modes (Normal/Ghost) and relays updates to other clients.
 */
struct SyncModeService
{
    SyncModeService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~SyncModeService() noexcept = default;

    TP_NOCOPYMOVE(SyncModeService);

private:
    void OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void OnRequestSetSyncMode(const PacketEvent<RequestSetSyncMode>& acEvent) noexcept;

    void BroadcastMode(uint32_t aPlayerId, SyncMode aMode, Player* apIgnoredPlayer = nullptr) const noexcept;
    void SendSnapshot(Player* apPlayer) const noexcept;

    World& m_world;

    entt::scoped_connection m_playerJoinConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_requestConnection;
};
