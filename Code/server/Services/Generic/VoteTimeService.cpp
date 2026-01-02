#include <stdafx.h>

#include <Services/VoteTimeService.h>
#include <World.h>

#include <GameServer.h>
#include <Game/Player.h>
#include <Game/PlayerManager.h>

#include <Messages/SendChatMessageRequest.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <ChatMessageTypes.h>

#include <Services/CalendarService.h>

#include <cctype>

using TiltedPhoques::String;

namespace {
static bool ParseTime(const String& s, int& outH, int& outM) noexcept
{
    // Accept HH or HH:MM, range check
    outH = 0; outM = 0;
    int h = 0, m = 0;
    size_t colon = s.find(':');
    try {
        if (colon == String::npos)
        {
            h = std::stoi(s.c_str());
            m = 0;
        }
        else
        {
            h = std::stoi(s.substr(0, colon).c_str());
            m = std::stoi(s.substr(colon + 1).c_str());
        }
    } catch (...) { return false; }
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return false;
    outH = h; outM = m; return true;
}

static String ToLower(String s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    return s;
}
}

VoteTimeService::VoteTimeService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_chatConn   = aDispatcher.sink<PacketEvent<SendChatMessageRequest>>().connect<&VoteTimeService::OnChat>(this);
    m_updateConn = aDispatcher.sink<UpdateEvent>().connect<&VoteTimeService::OnUpdate>(this);
    m_joinConn   = aDispatcher.sink<PlayerJoinEvent>().connect<&VoteTimeService::OnPlayerJoin>(this);
    m_leaveConn  = aDispatcher.sink<PlayerLeaveEvent>().connect<&VoteTimeService::OnPlayerLeave>(this);
}

void VoteTimeService::BroadcastSystem(const String& text) const noexcept
{
    NotifyChatMessageBroadcast msg{};
    msg.MessageType = kSystemMessage;
    msg.PlayerName = "";
    msg.ChatMessage = text;
    GameServer::Get()->SendToPlayers(msg);
}

String VoteTimeService::FormatTime(int h, int m) noexcept
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return String(buf);
}

void VoteTimeService::StartVote(uint32_t starterId, int hour, int minute) noexcept
{
    m_active = true;
    m_targetHour = hour;
    m_targetMinute = minute;
    m_starterId = starterId;
    m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);

    m_eligible.clear();
    m_yes.clear();
    m_no.clear();

    // Snapshot eligible players at start
    m_world.GetPlayerManager().ForEach([&](Player* p){ if (p) m_eligible.insert(p->GetId()); });

    // Starter counts as yes
    m_yes.insert(starterId);

    BroadcastSystem("Vote to set time to " + FormatTime(hour, minute) + " started. Type /yes or /no (60s timeout).");

    CheckAndMaybeApply();
}

void VoteTimeService::CastYes(uint32_t id) noexcept
{
    if (!m_active) return;
    if (m_eligible.find(id) == m_eligible.end()) return;
    if (m_no.find(id) != m_no.end()) return; // already no
    m_yes.insert(id);
    CheckAndMaybeApply();
}

void VoteTimeService::CastNo(uint32_t id) noexcept
{
    if (!m_active) return;
    if (m_eligible.find(id) == m_eligible.end()) return;
    m_no.insert(id);
    BroadcastSystem("Vote to set time to " + FormatTime(m_targetHour, m_targetMinute) + " failed: a player voted no.");
    m_active = false;
}

void VoteTimeService::CheckAndMaybeApply() noexcept
{
    if (!m_active) return;

    // All eligible must be in yes set
    for (auto pid : m_eligible)
    {
        if (m_yes.find(pid) == m_yes.end())
            return;
    }

    // Unanimous
    const bool ok = m_world.GetCalendarService().SetTime(m_targetHour, m_targetMinute, m_world.GetCalendarService().GetTimeScale());
    if (ok)
        BroadcastSystem("Time set to " + FormatTime(m_targetHour, m_targetMinute) + " by unanimous vote.");
    else
        BroadcastSystem("Failed to set time. (Invalid time?)");

    m_active = false;
}

void VoteTimeService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_active) return;
    if (std::chrono::steady_clock::now() > m_deadline)
    {
        BroadcastSystem("Vote to set time to " + FormatTime(m_targetHour, m_targetMinute) + " expired.");
        m_active = false;
    }
}

void VoteTimeService::OnPlayerJoin(const PlayerJoinEvent&) noexcept
{
    // Do not add to eligibility after vote start (vote snapshot behavior)
}

void VoteTimeService::OnPlayerLeave(const PlayerLeaveEvent& evt) noexcept
{
    if (!m_active) return;
    const auto id = evt.pPlayer ? evt.pPlayer->GetId() : 0u;
    if (id == 0) return;
    // If they were eligible, remove them and re-check
    if (m_eligible.erase(id) > 0)
    {
        m_yes.erase(id);
        m_no.erase(id);
        CheckAndMaybeApply();
    }
}

void VoteTimeService::OnChat(const PacketEvent<SendChatMessageRequest>& aMsg) noexcept
{
    HandleChatCommand(aMsg.pPlayer, aMsg.Packet.ChatMessage);
}

void VoteTimeService::HandleChatCommand(Player* player, const TiltedPhoques::String& message) noexcept
{
    const uint32_t playerId = player ? player->GetId() : 0u;
    if (playerId == 0)
        return;

    String text = ToLower(message);

    // Strip leading/trailing spaces
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();

    if (text.rfind("/votetime", 0) == 0)
    {
        // Parse argument after command
        String arg;
        if (text.size() > 9)
        {
            arg = text.substr(9);
            // strip spaces
            while (!arg.empty() && std::isspace(static_cast<unsigned char>(arg.front())))
                arg.erase(arg.begin());
        }
        int h = 0, m = 0;
        if (!arg.empty() && ParseTime(arg, h, m))
        {
            StartVote(playerId, h, m);
        }
        else
        {
            BroadcastSystem("Usage: /votetime HH[:MM]");
        }
        return;
    }

    if (text == "/yes")
    {
        CastYes(playerId);
        return;
    }
    if (text == "/no")
    {
        CastNo(playerId);
        return;
    }
}
