#pragma once

struct World;
struct TransportService;
struct PlayerCharacter;

struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct ServerSettings;
struct GridCellChangeEvent;
struct CellChangeEvent;
struct PlayerDialogueEvent;
struct PlayerLevelEvent;
struct PartyJoinedEvent;
struct PartyLeftEvent;

struct NotifyPlayerRespawn;

/**
 * @brief Handles logic related to the local player.
 */
struct PlayerService
{
    PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~PlayerService() noexcept = default;

    TP_NOCOPYMOVE(PlayerService);

    // Called from UI (CEF) when the player presses the respawn button.
    void RequestManualRespawn() noexcept;

    // Called when a party member revives the local player with a healing spell.
    void OnHealRevive() noexcept;

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnServerSettingsReceived(const ServerSettings& acSettings) noexcept;
    void OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) const noexcept;
    void OnGridCellChangeEvent(const GridCellChangeEvent& acEvent) const noexcept;
    void OnCellChangeEvent(const CellChangeEvent& acEvent) const noexcept;
    void OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) const noexcept;
    void OnPlayerLevelEvent(const PlayerLevelEvent& acEvent) const noexcept;
    void OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept;

private:
    /**
     * @brief Run the respawn timer and drive death screen updates.
     */
    void RunRespawnUpdates(const double acDeltaTime) noexcept;
    void RunPostDeathUpdates(const double acDeltaTime) noexcept;
    /**
     * @brief Make sure difficulty doesn't get changed while connected
     */
    void RunDifficultyUpdates() const noexcept;
    void RunLevelUpdates() const noexcept;
    void RunBeastFormDetection() const noexcept;
    void SyncCachedEquipment(PlayerCharacter* apPlayer) noexcept;

    void ToggleDeathSystem(bool aSet) noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;

    double m_respawnTimer = 0.0;
    int32_t m_serverDifficulty = 6;
    int32_t m_previousDifficulty = 6;

    bool m_isDeathSystemEnabled = true;

    // True while the player is in bleedout and the death screen is active.
    bool m_waitingForRespawn = false;
    // Becomes true when the cooldown has finished and the respawn button can be pressed.
    bool m_canRespawn = false;
    // Set by button click/heal to trigger respawn from RunRespawnUpdates
    bool m_shouldRespawnAtEntrance = false;
    bool m_shouldRespawnInPlace = false;

    bool m_knockdownStart = false;
    double m_knockdownTimer = 0.0;

    bool m_godmodeStart = false;
    double m_godmodeTimer = 0.0;

    uint32_t m_cachedLeftHandSpellId = 0;
    uint32_t m_cachedRightHandSpellId = 0;
    uint32_t m_cachedLeftHandItemId = 0;
    uint32_t m_cachedRightHandItemId = 0;
    uint32_t m_cachedTwoHandedItemId = 0;
    uint32_t m_cachedAmmoId = 0;
    uint32_t m_cachedPowerId = 0;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_settingsConnection;
    entt::scoped_connection m_notifyRespawnConnection;
    entt::scoped_connection m_gridCellChangeConnection;
    entt::scoped_connection m_cellChangeConnection;
    entt::scoped_connection m_playerDialogueConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;
};
