#include <GameServer.h>

#include <Services/OverlayService.h>

#include <ChatMessageTypes.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/PlayerDialogueRequest.h>
#include <Messages/NotifyPlayerDialogue.h>
#include <Messages/TeleportRequest.h>
#include <Messages/NotifyTeleport.h>
#include <Messages/NotifyTeleportRequest.h>
#include <Messages/NotifyTeleportCountdown.h>
#include <Messages/TeleportResponse.h>
#include <Messages/PlayEmoteRequest.h>
#include <Messages/CancelEmoteRequest.h>
#include <Messages/NotifyPlayEmote.h>
#include <Messages/NotifyCancelEmote.h>
#include <fmt/format.h>
#include <algorithm>
#include <cctype>

#include <Events/UpdateEvent.h>

#include <Messages/RequestPlayerHealthUpdate.h>
#include <Messages/NotifyPlayerHealthUpdate.h>

#include "Game/Player.h"
#include <Events/PlayerLeaveEvent.h>
#include <Components/MovementComponent.h>
#include <Components.h>

#include <regex>
#include <string_view>

#include <glm/gtx/norm.hpp>
#include <cmath>

void sendPlayerMessage(const ChatMessageType acType, const String acContent, Player* aSendingPlayer) noexcept;

namespace
{
TiltedPhoques::String TrimCopy(const TiltedPhoques::String& value)
{
    TiltedPhoques::String trimmed = value;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();
    return trimmed;
}

void SendSystemMessage(Player* apPlayer, std::string_view aMessage)
{
    if (!apPlayer)
        return;

    NotifyChatMessageBroadcast notify{};
    notify.MessageType = ChatMessageType::kSystemMessage;
    notify.PlayerName = "";
    notify.ChatMessage = aMessage.data();
    apPlayer->Send(notify);
}

bool PopulateTeleportDestination(World& aWorld, Player* apTarget, NotifyTeleport& aOutMessage) noexcept
{
    if (!apTarget)
        return false;

    const auto character = apTarget->GetCharacter();
    if (!character.has_value())
        return false;

    const auto* pMovementComponent = aWorld.try_get<MovementComponent>(*character);
    if (!pMovementComponent)
        return false;

    const auto& cellComponent = apTarget->GetCellComponent();
    aOutMessage.CellId = cellComponent.Cell;
    aOutMessage.Position = pMovementComponent->Position;
    aOutMessage.WorldSpaceId = cellComponent.WorldSpaceId;
    return true;
}

constexpr float kTeleportMovementCancelDistanceSquared = 9.f; // 3 units squared
} // namespace


OverlayService::OverlayService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_chatMessageConnection = aDispatcher.sink<PacketEvent<SendChatMessageRequest>>().connect<&OverlayService::HandleChatMessage>(this);
    m_playerEnterWorldConnection = aDispatcher.sink<PlayerEnterWorldEvent>().connect<&OverlayService::HandlePlayerJoin>(this);
    m_playerDialogueConnection = aDispatcher.sink<PacketEvent<PlayerDialogueRequest>>().connect<&OverlayService::OnPlayerDialogue>(this);
    m_teleportConnection = aDispatcher.sink<PacketEvent<TeleportRequest>>().connect<&OverlayService::OnTeleport>(this);
    m_teleportResponseConnection = aDispatcher.sink<PacketEvent<TeleportResponse>>().connect<&OverlayService::OnTeleportResponse>(this);
    m_playerHealthConnection = aDispatcher.sink<PacketEvent<RequestPlayerHealthUpdate>>().connect<&OverlayService::OnPlayerHealthUpdate>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&OverlayService::OnPlayerLeave>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&OverlayService::OnUpdate>(this);
    m_playEmoteConnection = aDispatcher.sink<PacketEvent<PlayEmoteRequest>>().connect<&OverlayService::OnPlayEmoteRequest>(this);
    m_cancelEmoteConnection = aDispatcher.sink<PacketEvent<CancelEmoteRequest>>().connect<&OverlayService::OnCancelEmoteRequest>(this);
}

void OverlayService::SendCommandList(Player* apPlayer) const noexcept
{
    m_world.GetChatCommandService().SendCommandList(apPlayer);
}

void sendPlayerMessage(const ChatMessageType acType, const String acContent, Player* aSendingPlayer) noexcept
{
    NotifyChatMessageBroadcast notifyMessage{};

    std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"};
    notifyMessage.MessageType = acType;
    notifyMessage.PlayerName = std::regex_replace(aSendingPlayer->GetUsername(), escapeHtml, "");
    notifyMessage.ChatMessage = std::regex_replace(acContent, escapeHtml, "");

    if (auto out = spdlog::get("ConOut"))
    {
        const char* label = "Chat";
        switch (notifyMessage.MessageType)
        {
        case kGlobalChat: label = "Global"; break;
        case kPartyChat: label = "Party"; break;
        case kLocalChat: label = "Local"; break;
        case kPlayerDialogue: label = "Dialogue"; break;
        default: break;
        }
        if (!notifyMessage.PlayerName.empty())
            out->info("[{}] {}: {}", label, notifyMessage.PlayerName.c_str(), notifyMessage.ChatMessage.c_str());
        else
            out->info("[{}] {}", label, notifyMessage.ChatMessage.c_str());
    }

    auto character = aSendingPlayer->GetCharacter();

    switch (notifyMessage.MessageType)
    {
    case kGlobalChat: GameServer::Get()->SendToPlayers(notifyMessage); break;

    case kSystemMessage: spdlog::error("PlayerId {} attempted to send a System Message.", aSendingPlayer->GetId()); break;

    case kPlayerDialogue: GameServer::Get()->SendToParty(notifyMessage, aSendingPlayer->GetParty()); break;

    case kPartyChat: GameServer::Get()->SendToParty(notifyMessage, aSendingPlayer->GetParty()); break;

    case kLocalChat:
        if (character)
        {
            if (!GameServer::Get()->SendToPlayersInRange(notifyMessage, *character))
                spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
        }
        break;
    default: spdlog::error("{} is not a known MessageType", static_cast<uint64_t>(notifyMessage.MessageType)); break;
    }
}

void OverlayService::HandlePlayerJoin(const PlayerEnterWorldEvent& acEvent) const noexcept
{
    SendCommandList(const_cast<Player*>(acEvent.pPlayer));
}

void OverlayService::HandleChatMessage(const PacketEvent<SendChatMessageRequest>& acMessage) const noexcept
{
    const TiltedPhoques::String trimmed = TrimCopy(acMessage.Packet.ChatMessage);
    if (!trimmed.empty() && trimmed.front() == '/')
    {
        if (m_world.GetChatCommandService().TryHandle(acMessage.pPlayer, trimmed))
            return;

        SendSystemMessage(acMessage.pPlayer, "Unknown command. Type /help for a list of commands.");
        return;
    }

    auto [canceled, reason] = m_world.GetScriptService().HandleChatMessage(*acMessage.pPlayer->GetCharacter(), acMessage.Packet.ChatMessage);
    if (canceled)
        return;

    sendPlayerMessage(acMessage.Packet.MessageType, acMessage.Packet.ChatMessage, acMessage.pPlayer);
}

void OverlayService::OnPlayerDialogue(const PacketEvent<PlayerDialogueRequest>& acMessage) const noexcept
{
    sendPlayerMessage(kPlayerDialogue, acMessage.Packet.Text, acMessage.pPlayer);
}

void OverlayService::OnTeleport(const PacketEvent<TeleportRequest>& acMessage) noexcept
{
    Player* pRequester = acMessage.pPlayer;
    if (!pRequester)
        return;

    Player* pTargetPlayer = m_world.GetPlayerManager().GetById(acMessage.Packet.PlayerId);
    if (!pTargetPlayer)
    {
        SendSystemMessage(pRequester, "Teleport request failed: player not found.");
        return;
    }

    if (pTargetPlayer->GetId() == pRequester->GetId())
    {
        SendSystemMessage(pRequester, "You cannot request to teleport to yourself.");
        return;
    }

    auto& pending = m_pendingTeleportRequests[pTargetPlayer->GetId()];
    if (!pending.insert(pRequester->GetId()).second)
    {
        SendSystemMessage(pRequester, fmt::format("Teleport request to {} is already pending.", pTargetPlayer->GetUsername().c_str()));
        return;
    }

    NotifyTeleportRequest notify{};
    notify.RequesterId = static_cast<uint16_t>(pRequester->GetId());
    notify.RequesterName = pRequester->GetUsername();
    pTargetPlayer->Send(notify);

    SendSystemMessage(pTargetPlayer, fmt::format("{} wants to teleport to you.", pRequester->GetUsername().c_str()));
    SendSystemMessage(pRequester, fmt::format("Teleport request sent to {}.", pTargetPlayer->GetUsername().c_str()));
}

void OverlayService::OnTeleportResponse(const PacketEvent<TeleportResponse>& acMessage) noexcept
{
    Player* pResponder = acMessage.pPlayer;
    if (!pResponder)
        return;

    const uint32_t responderId = pResponder->GetId();
    const uint32_t requesterId = acMessage.Packet.RequesterId;

    auto pendingIt = m_pendingTeleportRequests.find(responderId);
    if (pendingIt == m_pendingTeleportRequests.end())
    {
        SendSystemMessage(pResponder, "No pending teleport request found.");
        return;
    }

    auto& requesters = pendingIt->second;
    if (requesters.erase(requesterId) == 0)
    {
        SendSystemMessage(pResponder, "No pending teleport request found.");
        return;
    }

    if (requesters.empty())
        m_pendingTeleportRequests.erase(pendingIt);

    Player* pRequester = m_world.GetPlayerManager().GetById(requesterId);
    if (!pRequester)
    {
        SendSystemMessage(pResponder, "Teleport requester is no longer online.");
        return;
    }

    if (!acMessage.Packet.Accepted)
    {
        SendSystemMessage(pRequester, fmt::format("{} declined your teleport request.", pResponder->GetUsername().c_str()));
        SendSystemMessage(pResponder, fmt::format("You declined {}'s teleport request.", pRequester->GetUsername().c_str()));
        return;
    }

    NotifyTeleport teleportMessage{};
    if (!PopulateTeleportDestination(m_world, pResponder, teleportMessage))
    {
        SendSystemMessage(pResponder, "Unable to locate your position for teleport.");
        SendSystemMessage(pRequester, "Teleport request failed.");
        return;
    }

    OverlayService::PendingTeleportCountdown pending{};
    pending.RequesterId = requesterId;
    pending.ResponderId = responderId;
    pending.TeleportMessage = teleportMessage;
    pending.TimeRemaining = 5.f;
    pending.TargetName = pResponder->GetUsername();
    pending.LastAnnouncedSeconds = 5;

    if (const auto character = pRequester->GetCharacter())
    {
        if (const auto* pMovement = m_world.try_get<MovementComponent>(*character))
        {
            pending.InitialPosition = pMovement->Position;
            pending.HasInitialPosition = true;
        }
    }

    m_activeTeleportCountdowns[pending.RequesterId] = pending;

    NotifyTeleportCountdown countdown{};
    countdown.TargetPlayerId = static_cast<uint16_t>(responderId);
    countdown.TargetName = pending.TargetName;
    countdown.DurationSeconds = 5;
    countdown.Cancelled = false;
    pRequester->Send(countdown);

    SendSystemMessage(pRequester, fmt::format("{} accepted your teleport request. Teleporting in {} seconds. Do not move!", pending.TargetName.c_str(), countdown.DurationSeconds));
    SendSystemMessage(pResponder, fmt::format("You accepted {}'s teleport request. Teleporting them in {} seconds.", pRequester->GetUsername().c_str(), countdown.DurationSeconds));
}

void OverlayService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_activeTeleportCountdowns.empty())
        return;

    for (auto it = m_activeTeleportCountdowns.begin(); it != m_activeTeleportCountdowns.end();)
    {
        auto& pending = it->second;

        Player* pRequester = m_world.GetPlayerManager().GetById(pending.RequesterId);
        if (!pRequester)
        {
            it = m_activeTeleportCountdowns.erase(it);
            continue;
        }

        Player* pResponder = m_world.GetPlayerManager().GetById(pending.ResponderId);
        bool cancelTeleport = pResponder == nullptr;

        if (!cancelTeleport && pending.HasInitialPosition)
        {
            if (const auto character = pRequester->GetCharacter())
            {
                if (const auto* pMovement = m_world.try_get<MovementComponent>(*character))
                {
                    const float distanceSquared = glm::distance2(pMovement->Position, pending.InitialPosition);
                    if (distanceSquared > kTeleportMovementCancelDistanceSquared)
                        cancelTeleport = true;
                }
            }
        }

        if (cancelTeleport)
        {
            NotifyTeleportCountdown cancelMessage{};
            cancelMessage.TargetPlayerId = static_cast<uint16_t>(pending.ResponderId);
            cancelMessage.TargetName = pending.TargetName;
            cancelMessage.DurationSeconds = 0;
            cancelMessage.Cancelled = true;
            cancelMessage.Reason = pResponder ? "Teleport cancelled: you moved." : "Teleport cancelled: target player disconnected.";
            pRequester->Send(cancelMessage);

            SendSystemMessage(pRequester, cancelMessage.Reason.c_str());
            if (pResponder)
                SendSystemMessage(pResponder, fmt::format("{} moved. Teleport cancelled.", pRequester->GetUsername().c_str()));

            it = m_activeTeleportCountdowns.erase(it);
            continue;
        }

        pending.TimeRemaining -= acEvent.Delta;
        if (pending.TimeRemaining <= 0.f)
        {
            pRequester->Send(pending.TeleportMessage);

            NotifyTeleportCountdown clearMessage{};
            clearMessage.TargetPlayerId = static_cast<uint16_t>(pending.ResponderId);
            clearMessage.TargetName = pending.TargetName;
            clearMessage.DurationSeconds = 0;
            clearMessage.Cancelled = true;
            clearMessage.Reason = "";
            pRequester->Send(clearMessage);

            SendSystemMessage(pRequester, fmt::format("Teleporting to {}.", pending.TargetName.c_str()));
            if (pResponder)
                SendSystemMessage(pResponder, fmt::format("{} is teleporting to you.", pRequester->GetUsername().c_str()));

            it = m_activeTeleportCountdowns.erase(it);
            continue;
        }

        const auto secondsRemaining = static_cast<uint16_t>(std::ceil(pending.TimeRemaining));
        if (secondsRemaining != pending.LastAnnouncedSeconds && secondsRemaining > 0)
        {
            NotifyTeleportCountdown update{};
            update.TargetPlayerId = static_cast<uint16_t>(pending.ResponderId);
            update.TargetName = pending.TargetName;
            update.DurationSeconds = secondsRemaining;
            update.Cancelled = false;
            update.Reason = "";
            pRequester->Send(update);
            pending.LastAnnouncedSeconds = secondsRemaining;
        }

        ++it;
    }
}

void OverlayService::OnPlayEmoteRequest(const PacketEvent<PlayEmoteRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    const auto entity = m_world.TryResolveEntity(message.ServerId);
    if (!entity)
    {
        spdlog::debug("Play emote request for unknown entity {:X}", message.ServerId);
        return;
    }

    const auto* pOwner = m_world.try_get<OwnerComponent>(*entity);
    if (!pOwner || pOwner->GetOwner() != acMessage.pPlayer)
    {
        spdlog::warn("Play emote request rejected for entity {:X}", message.ServerId);
        return;
    }

    NotifyPlayEmote notify{};
    notify.ServerId = message.ServerId;
    notify.EventName = message.EventName;

    const auto senderCell = acMessage.pPlayer->GetCellComponent().Cell;
    if (!senderCell)
        return;

    TiltedPhoques::Vector<ConnectionId_t> players;
    players.reserve(m_world.GetPlayerManager().Count());

    for (Player* pPlayer : m_world.GetPlayerManager())
        players.push_back(pPlayer->GetConnectionId());

    for (auto connectionId : players)
    {
        Player* pPlayer = m_world.GetPlayerManager().GetByConnectionId(connectionId);
        if (!pPlayer || pPlayer == acMessage.pPlayer)
            continue;

        if (pPlayer->GetCellComponent().Cell == senderCell)
            pPlayer->Send(notify);
    }
}

void OverlayService::OnCancelEmoteRequest(const PacketEvent<CancelEmoteRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    const auto entity = m_world.TryResolveEntity(message.ServerId);
    if (!entity)
    {
        spdlog::debug("Cancel emote request for unknown entity {:X}", message.ServerId);
        return;
    }

    const auto* pOwner = m_world.try_get<OwnerComponent>(*entity);
    if (!pOwner || pOwner->GetOwner() != acMessage.pPlayer)
    {
        spdlog::warn("Cancel emote request rejected for entity {:X}", message.ServerId);
        return;
    }

    NotifyCancelEmote notify{};
    notify.ServerId = message.ServerId;

    const auto senderCell = acMessage.pPlayer->GetCellComponent().Cell;
    if (!senderCell)
        return;

    TiltedPhoques::Vector<ConnectionId_t> players;
    players.reserve(m_world.GetPlayerManager().Count());

    for (Player* pPlayer : m_world.GetPlayerManager())
        players.push_back(pPlayer->GetConnectionId());

    for (auto connectionId : players)
    {
        Player* pPlayer = m_world.GetPlayerManager().GetByConnectionId(connectionId);
        if (!pPlayer || pPlayer == acMessage.pPlayer)
            continue;

        if (pPlayer->GetCellComponent().Cell == senderCell)
            pPlayer->Send(notify);
    }
}

void OverlayService::OnPlayerHealthUpdate(const PacketEvent<RequestPlayerHealthUpdate>& acMessage) const noexcept
{
    NotifyPlayerHealthUpdate notify{};
    notify.PlayerId = acMessage.pPlayer->GetId();
    notify.Percentage = acMessage.Packet.Percentage;

    GameServer::Get()->SendToParty(notify, acMessage.pPlayer->GetParty(), acMessage.GetSender());
}

void OverlayService::OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept
{
    if (!acEvent.pPlayer)
        return;

    const uint32_t playerId = acEvent.pPlayer->GetId();
    m_pendingTeleportRequests.erase(playerId);

    for (auto it = m_pendingTeleportRequests.begin(); it != m_pendingTeleportRequests.end();)
    {
        it->second.erase(playerId);
        if (it->second.empty())
            it = m_pendingTeleportRequests.erase(it);
        else
            ++it;
    }

    for (auto it = m_activeTeleportCountdowns.begin(); it != m_activeTeleportCountdowns.end();)
    {
        const auto& pending = it->second;
        const bool involvesPlayer = pending.RequesterId == playerId || pending.ResponderId == playerId;
        if (!involvesPlayer)
        {
            ++it;
            continue;
        }

        const bool requesterLeft = pending.RequesterId == playerId;
        const bool responderLeft = pending.ResponderId == playerId;

        if (!requesterLeft)
        {
            if (Player* pRequester = m_world.GetPlayerManager().GetById(pending.RequesterId))
            {
                NotifyTeleportCountdown cancelMessage{};
                cancelMessage.TargetPlayerId = static_cast<uint16_t>(pending.ResponderId);
                cancelMessage.TargetName = pending.TargetName;
                cancelMessage.DurationSeconds = 0;
                cancelMessage.Cancelled = true;
                cancelMessage.Reason = responderLeft ? "Teleport cancelled: target player disconnected." : "Teleport cancelled.";
                pRequester->Send(cancelMessage);

                SendSystemMessage(pRequester, cancelMessage.Reason.c_str());
            }
        }
        else if (Player* pResponder = m_world.GetPlayerManager().GetById(pending.ResponderId))
        {
            SendSystemMessage(pResponder, fmt::format("{} disconnected. Teleport request cancelled.", acEvent.pPlayer->GetUsername().c_str()));
        }

        it = m_activeTeleportCountdowns.erase(it);
    }
}
