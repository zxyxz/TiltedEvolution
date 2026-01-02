#pragma once

#include <include/internal/cef_ptr.h>
#include <chrono>
#include <string>
#include <unordered_map>

namespace TiltedPhoques
{
struct OverlayApp;
}

struct RenderSystemD3D11;
struct D3D11RenderProvider;
struct FormIdComponent;
struct World;
struct Actor;
struct UpdateEvent;
struct ConnectedEvent;
struct DisconnectedEvent;
struct TransportService;
struct NotifyChatMessageBroadcast;
struct NotifyPlayerList;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;
struct NotifyPlayerProfileImage;
struct NotifyPlayerDialogue;
struct ConnectionErrorEvent;
struct NotifyPlayerLevel;
struct NotifyPlayerCellChanged;
struct NotifyTeleportRequest;
struct NotifyTeleportCountdown;
struct NotifyTeleport;
struct NotifyPlayerHealthUpdate;
struct NotifyCommandList;
struct NotifyPlayEmote;
struct NotifyCancelEmote;
enum ChatMessageTypes;
struct PartyJoinedEvent;
struct PartyLeftEvent;
struct Actor;

using TiltedPhoques::OverlayApp;

/**
 * @brief Controls the UI from the client.
 */
struct OverlayService
{
    OverlayService(World& aWorld, TransportService& transport, entt::dispatcher& aDispatcher);
    ~OverlayService() noexcept;

    TP_NOCOPYMOVE(OverlayService);

    void Create(RenderSystemD3D11* apRenderSystem) noexcept;

    void Render() noexcept;
    void Reset() const noexcept;
    void Reload() noexcept;

    void Initialize() noexcept;

    void SetActive(bool aActive) noexcept;
    [[nodiscard]] bool GetActive() const noexcept;

    void SetInGame(bool aInGame) noexcept;
    [[nodiscard]] bool GetInGame() const noexcept;

    void SetVersion(const std::string& acVersion);

    OverlayApp* GetOverlayApp() const noexcept { return m_pOverlay.get(); }

    void SendSystemMessage(const std::string& acMessage);

    void SetPlayerHealthPercentage(uint32_t aFormId) const noexcept;

    // Send world map party pins to the CEF UI as a JSON string: [{"x":float,"y":float,"id":number}, ...]
    void SetPartyPinsJson(const std::string& aJson) noexcept;

protected:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnConnectedEvent(const ConnectedEvent&) noexcept;
    void OnDisconnectedEvent(const DisconnectedEvent&) noexcept;
    void OnWaitingFor3DRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept;
    void OnPlayerComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept;
    void OnConnectionError(const ConnectionErrorEvent& acConnectedEvent) const noexcept;
    void OnChatMessageReceived(const NotifyChatMessageBroadcast&) noexcept;
    void OnPlayerDialogue(const NotifyPlayerDialogue&) noexcept;
    void OnPlayerJoined(const NotifyPlayerJoined&) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft&) noexcept;
    void OnPlayerProfileImage(const NotifyPlayerProfileImage&) noexcept;
    void OnPlayerLevel(const NotifyPlayerLevel&) noexcept;
    void OnPlayerCellChanged(const NotifyPlayerCellChanged& acMessage) const noexcept;
    void OnNotifyTeleportRequest(const NotifyTeleportRequest& acMessage) noexcept;
    void OnNotifyTeleportCountdown(const NotifyTeleportCountdown& acMessage) noexcept;
    void OnNotifyTeleport(const NotifyTeleport& acMessage) noexcept;
    void OnNotifyPlayerHealthUpdate(const NotifyPlayerHealthUpdate& acMessage) noexcept;
    void OnNotifyCommandList(const NotifyCommandList& acMessage) noexcept;
    void OnNotifyPlayEmote(const NotifyPlayEmote& acMessage) noexcept;
    void OnNotifyCancelEmote(const NotifyCancelEmote& acMessage) noexcept;
    void OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept;
    void OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept;

private:
    struct RemoteEmoteState
    {
        std::string EventName;
        std::chrono::steady_clock::time_point LastPlayed{};
    };

    void RunDebugDataUpdates() noexcept;
    void RunPlayerHealthUpdates() noexcept;
    void UpdateRemoteEmoteLoops() noexcept;

    CefRefPtr<OverlayApp> m_pOverlay{nullptr};
    TiltedPhoques::UniquePtr<D3D11RenderProvider> m_pProvider;

    World& m_world;
    TransportService& m_transport;

    bool m_active = false;
    bool m_inGame = false;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_connectionErrorConnection;
    entt::scoped_connection m_cellChangeEventConnection;
    entt::scoped_connection m_chatMessageConnection;
    entt::scoped_connection m_playerDialogueConnection;
    entt::scoped_connection m_playerJoinedConnection;
    entt::scoped_connection m_playerLeftConnection;
    entt::scoped_connection m_playerAvatarConnection;
    entt::scoped_connection m_playerAddedConnection;
    entt::scoped_connection m_playerRemovedConnection;
    entt::scoped_connection m_playerLevelConnection;
    entt::scoped_connection m_cellChangedConnection;
    entt::scoped_connection m_teleportConnection;
    entt::scoped_connection m_teleportRequestConnection;
    entt::scoped_connection m_teleportCountdownConnection;
    entt::scoped_connection m_playerHealthConnection;
    entt::scoped_connection m_commandListConnection;
    entt::scoped_connection m_playEmoteConnection;
    entt::scoped_connection m_cancelEmoteConnection;
    entt::scoped_connection m_partyJoinedConnection;
    entt::scoped_connection m_partyLeftConnection;

    std::unordered_map<uint32_t, RemoteEmoteState> m_remoteEmotes;
};
