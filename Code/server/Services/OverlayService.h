#pragma once

#include <Messages/SendChatMessageRequest.h>

#include <Events/PacketEvent.h>
#include <Events/PlayerEnterWorldEvent.h>
#include <Events/UpdateEvent.h>

#include <unordered_map>
#include <unordered_set>

#include <glm/vec3.hpp>

#include <Messages/NotifyTeleport.h>

struct World;

struct PlayerDialogueRequest;
struct TeleportRequest;
struct RequestPlayerHealthUpdate;
struct TeleportResponse;
struct PlayEmoteRequest;
struct CancelEmoteRequest;
struct PlayerLeaveEvent;
struct Player;

/**
 * @brief Dispatches UI events that modify the UI view of other cients.
 */
class OverlayService
{
public:
    OverlayService(World& aWorld, entt::dispatcher& aDispatcher);
    void SendCommandList(Player* apPlayer) const noexcept;

protected:
    /**
     * @brief Applies regex on chat message and relays it to other clients.
     */
    void HandleChatMessage(const PacketEvent<SendChatMessageRequest>& acMessage) const noexcept;
    void OnPlayerDialogue(const PacketEvent<PlayerDialogueRequest>& acMessage) const noexcept;
    void OnTeleport(const PacketEvent<TeleportRequest>& acMessage) noexcept;
    void OnTeleportResponse(const PacketEvent<TeleportResponse>& acMessage) noexcept;
    void OnPlayerHealthUpdate(const PacketEvent<RequestPlayerHealthUpdate>& acMessage) const noexcept;
    void HandlePlayerJoin(const PlayerEnterWorldEvent& acEvent) const noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnPlayEmoteRequest(const PacketEvent<PlayEmoteRequest>& acMessage) const noexcept;
    void OnCancelEmoteRequest(const PacketEvent<CancelEmoteRequest>& acMessage) const noexcept;

private:
    struct PendingTeleportCountdown
    {
        uint32_t RequesterId{};
        uint32_t ResponderId{};
        NotifyTeleport TeleportMessage{};
        glm::vec3 InitialPosition{};
        bool HasInitialPosition{false};
        float TimeRemaining{};
        TiltedPhoques::String TargetName{};
        uint16_t LastAnnouncedSeconds{};
    };

    World& m_world;

    entt::scoped_connection m_chatMessageConnection;
    entt::scoped_connection m_playerDialogueConnection;
    entt::scoped_connection m_teleportConnection;
    entt::scoped_connection m_teleportResponseConnection;
    entt::scoped_connection m_playerHealthConnection;
    entt::scoped_connection m_playerEnterWorldConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_playEmoteConnection;
    entt::scoped_connection m_cancelEmoteConnection;
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>> m_pendingTeleportRequests;
    std::unordered_map<uint32_t, PendingTeleportCountdown> m_activeTeleportCountdowns;
};
