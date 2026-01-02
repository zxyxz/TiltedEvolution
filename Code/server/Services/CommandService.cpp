#include <Services/CommandService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Messages/SetTimeCommandRequest.h>
#include <Messages/NotifySetTimeResult.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/TeleportCommandRequest.h>
#include <Messages/TeleportCommandResponse.h>
#include <ChatMessageTypes.h>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace
{
void SendSystemMessage(Player* apPlayer, std::string_view aMessage) noexcept
{
    if (!apPlayer)
        return;

    NotifyChatMessageBroadcast notify{};
    notify.MessageType = ChatMessageType::kSystemMessage;
    notify.PlayerName = "";
    notify.ChatMessage = aMessage.data();
    apPlayer->Send(notify);
}

Player* FindPlayerByUsername(World& aWorld, const TiltedPhoques::String& aUsername)
{
    if (aUsername.empty())
        return nullptr;

    TiltedPhoques::String needle = aUsername;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (Player* pPlayer : aWorld.GetPlayerManager())
    {
        if (!pPlayer)
            continue;

        TiltedPhoques::String candidate = pPlayer->GetUsername();
        std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (candidate == needle)
            return pPlayer;
    }

    return nullptr;
}
} // namespace

CommandService::CommandService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_setTimeConnection = aDispatcher.sink<PacketEvent<SetTimeCommandRequest>>().connect<&CommandService::OnSetTimeCommand>(this);
    m_teleportConnection = aDispatcher.sink<PacketEvent<TeleportCommandRequest>>().connect<&CommandService::OnTeleportCommandRequest>(this);
}

void CommandService::OnSetTimeCommand(const PacketEvent<SetTimeCommandRequest>& acMessage) const noexcept
{
    NotifySetTimeResult response{};

    if (!m_world.GetAdminService().IsAdmin(acMessage.pPlayer->GetUsername()))
    {
        response.Result = NotifySetTimeResult::SetTimeResult::kNoPermission;
        acMessage.pPlayer->Send(response);
        SendSystemMessage(acMessage.pPlayer, "SetTime requires admin permissions.");
        return;
    }

    const int hour = static_cast<int>(acMessage.Packet.Hours);
    const int minute = static_cast<int>(acMessage.Packet.Minutes);
    const float timescale = m_world.GetCalendarService().GetTimeScale();
    const bool success = m_world.GetCalendarService().SetTime(hour, minute, timescale);

    response.Result = success ? NotifySetTimeResult::SetTimeResult::kSuccess : NotifySetTimeResult::SetTimeResult::kNoPermission;
    acMessage.pPlayer->Send(response);

    if (!success)
    {
        SendSystemMessage(acMessage.pPlayer, "SetTime failed: hour must be 0-23 and minute 0-59.");
    }
}

void CommandService::OnTeleportCommandRequest(const PacketEvent<TeleportCommandRequest>& acMessage) const noexcept
{
    if (!m_world.GetAdminService().IsAdmin(acMessage.pPlayer->GetUsername()))
    {
        SendSystemMessage(acMessage.pPlayer, "Teleport requires admin permissions.");
        return;
    }

    Player* pTarget = FindPlayerByUsername(m_world, acMessage.Packet.TargetPlayer);
    if (!pTarget)
    {
        SendSystemMessage(acMessage.pPlayer, "Teleport failed: player not found.");
        return;
    }

    const auto character = pTarget->GetCharacter();
    if (!character.has_value())
    {
        SendSystemMessage(acMessage.pPlayer, "Teleport failed: target has no character.");
        return;
    }

    const auto* pMovement = m_world.try_get<MovementComponent>(*character);
    if (!pMovement)
    {
        SendSystemMessage(acMessage.pPlayer, "Teleport failed: target position unavailable.");
        return;
    }

    TeleportCommandResponse response{};
    const auto& cellComponent = pTarget->GetCellComponent();
    response.CellId = cellComponent.Cell;
    response.WorldSpaceId = cellComponent.WorldSpaceId;
    response.Position = pMovement->Position;
    acMessage.pPlayer->Send(response);
}
