#pragma once

#include <unordered_set>
#include <vector>
#include <chrono>

#include <Events/PacketEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>

struct World;
struct SendChatMessageRequest;
struct Player;

// Simple unanimous vote-to-set-time feature, initiated via chat command.
// Commands (global/party/local chat all acceptable):
//   /votetime HH[:MM]   -> starts a vote
//   /yes                -> vote yes on the active vote
//   /no                 -> vote no (aborts)
// Vote requires 100% Yes from the players online at initiation time.
// Vote times out after a short period (default 60s).
class VoteTimeService
{
public:
    VoteTimeService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    void HandleChatCommand(Player* player, const TiltedPhoques::String& message) noexcept;

private:
    void OnChat(const PacketEvent<SendChatMessageRequest>& aMsg) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnPlayerJoin(const PlayerJoinEvent&) noexcept; // ignored for eligibility
    void OnPlayerLeave(const PlayerLeaveEvent& aEvt) noexcept;

    void StartVote(uint32_t aStarterId, int aHour, int aMinute) noexcept;
    void CastYes(uint32_t aPlayerId) noexcept;
    void CastNo(uint32_t aPlayerId) noexcept;
    void CheckAndMaybeApply() noexcept;
    void BroadcastSystem(const TiltedPhoques::String& aText) const noexcept;
    static TiltedPhoques::String FormatTime(int h, int m) noexcept;

    World& m_world;

    bool m_active{false};
    int m_targetHour{0};
    int m_targetMinute{0};
    uint32_t m_starterId{0};
    std::chrono::steady_clock::time_point m_deadline{};

    // Snapshot of eligible players (IDs) at start time
    std::unordered_set<uint32_t> m_eligible{};
    std::unordered_set<uint32_t> m_yes{};
    std::unordered_set<uint32_t> m_no{};

    entt::scoped_connection m_chatConn;
    entt::scoped_connection m_updateConn;
    entt::scoped_connection m_joinConn;
    entt::scoped_connection m_leaveConn;
};
