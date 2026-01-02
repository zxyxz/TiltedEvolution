#pragma once

#include <Games/Events.h>
#include <Events/EventDispatcher.h>
#include <Events/AddTargetEvent.h>
#include <Messages/AddTargetRequest.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyRemoveSpell.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <queue>
#include <array>

struct Actor;
struct World;
struct TransportService;
struct SpellItem;

struct UpdateEvent;
struct SpellCastEvent;
struct InterruptCastEvent;
struct AddTargetEvent;
struct RemoveSpellEvent;

struct NotifySpellCast;
struct NotifyInterruptCast;
struct NotifyHealingProximity;
struct NotifyPartyMemberDowned;

/**
 * @brief Handles magic spell casting and magic effects.
 *
 * To the poor sod that thinks of venturing into this part of the code base..
 * Yes, the magic sync code is a mess. It is easily the ugliest part of the codebase.
 * I did not get around to refactoring this before going open source, but it definitely should be.
 * I'm sorry, and good luck.
 *
 * Contact cosideci for more info.
 */
struct MagicService
{
    MagicService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~MagicService() noexcept = default;

    TP_NOCOPYMOVE(MagicService);

    /**
     * @brief Starts revealing remote players for a few seconds
     * @see UpdateRevealOtherPlayersEffect
     */
    void StartRevealingOtherPlayers() noexcept;

  protected:
    /**
     * @brief Checks to apply queued effects on each frame.
     */
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    /**
     * @brief Sends local spell cast to the server.
     */
    void OnSpellCastEvent(const SpellCastEvent& acSpellCastEvent) noexcept;
    /**
     * @brief Casts a spell based on a server message.
     */
    void OnNotifySpellCast(const NotifySpellCast& acMessage) const noexcept;
    /**
     * @brief Sends local interruption of spell cast to the server.
     */
    void OnInterruptCastEvent(const InterruptCastEvent& acEvent) noexcept;
    /**
     * @brief Interrupts a spell cast based on a server message.
     */
    void OnNotifyInterruptCast(const NotifyInterruptCast& acMessage) const noexcept;
    /**
     * @brief Sends magic effect and its target to the server.
     */
    void OnAddTargetEvent(const AddTargetEvent& acEvent) noexcept;
    /**
     * @brief Applies a magic effect based on a server message.
     */
    void OnNotifyAddTarget(const NotifyAddTarget& acMessage) noexcept;
    /**
     * @brief Sends a message to remove a spell from a player.
     */
    void OnRemoveSpellEvent(const RemoveSpellEvent& acEvent) noexcept;
    /*
     * @brief Handles removal of a spell
     */
    void OnNotifyRemoveSpell(const NotifyRemoveSpell& acMessage) noexcept;
    /**
     * @brief Handles healing proximity for reviving downed players
     */
    void OnNotifyHealingProximity(const NotifyHealingProximity& acMessage) noexcept;
    /**
     * @brief Receives party downed/revived events and triggers UI/chat and per-player reveal effect.
     */
    void OnNotifyPartyMemberDowned(const NotifyPartyMemberDowned& acMessage) noexcept;

  private:
    /**
     * Sometimes, certain magic effects are applied on actors that do not yet exist
     * within the client's entity component system (for example when a beast is summoned).
     * This function periodically checks whether the queued magic effects' targets exist already
     * and if so, sends out a message to add the magic effect to the target.
     */
    void ApplyQueuedEffects() noexcept;

    /**
     * Apply the "reveal players" effect on remote players.
     */
    void UpdateRevealOtherPlayersEffect(bool aForceTrigger = false) noexcept;
    /**
     * @brief Apply the reveal effect to currently downed party members only.
     */
    void UpdateRevealDownedPlayersEffect() noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    TransportService& m_transport;
    bool m_revealingOtherPlayers = false;
    bool m_revealingDownedPlayers = false;

    struct DownedMemberInfo
    {
        uint32_t PlayerId = 0;
        float PositionX = 0.f;
        float PositionY = 0.f;
        float PositionZ = 0.f;
    };

    struct ReviveChannelState
    {
        uint32_t CasterServerId = 0;
        float RequiredSeconds = 0.f;
        float AccumulatedSeconds = 0.f;
        std::chrono::steady_clock::time_point LastPingAt{};
        std::string HealerName;
    };

    struct HealerChannelState
    {
        bool Active = false;
        float RequiredSeconds = 0.f;
        float AccumulatedSeconds = 0.f;
        std::chrono::steady_clock::time_point LastUpdate{};
        float StartingMagickaValue = 0.f;
        bool HasStartingMagicka = false;
        bool CostBuffApplied = false;
    };

    std::unordered_map<uint32_t, DownedMemberInfo> m_downedPartyMembers;
    std::optional<ReviveChannelState> m_victimReviveState;
    HealerChannelState m_healerChannelState;
    std::array<bool, MagicSystem::CastingSource::CASTING_SOURCE_COUNT> m_localHealingHandsSources{};
    bool m_isLocalHealingHandsActive = false;
    uint32_t m_activeHealingHandsSpellId = 0;
    double m_healingHandsPingAccumulator = 0.0;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_spellCastEventConnection;
    entt::scoped_connection m_notifySpellCastConnection;
    entt::scoped_connection m_interruptCastEventConnection;
    entt::scoped_connection m_notifyInterruptCastConnection;
    entt::scoped_connection m_addTargetEventConnection;
    entt::scoped_connection m_notifyAddTargetConnection;
    entt::scoped_connection m_removeSpellEventConnection;
    entt::scoped_connection m_notifyRemoveSpell;
    entt::scoped_connection m_notifyHealingProximityConnection;
    entt::scoped_connection m_notifyPartyMemberDownedConnection;

    /*
     * @brief Queued magic effects.
     * @see ApplyQueuedEffects
     * Tracks effects queued for incompletely constructed actors / targets
     * There may be multiple. There may be sequencing between the targets
     * that matters. And, the objects might also be destroyed
     * before application, so they must time out
     */
  public:
    class MagicQueue
    {
      public:
        MagicQueue() = default;
        MagicQueue(const MagicQueue&) = default;
        ~MagicQueue() noexcept = default;

        bool Expired() const noexcept { return std::chrono::steady_clock::now() > m_expiration; }
        static spdlog::level::level_enum m_logLevel; // Initializer at file end, non-const so debugger can change.
        template <typename... Args> static inline void Spdlog(spdlog::format_string_t<Args...> aFmt, Args&&... args)
        {
            spdlog::log(m_logLevel, aFmt, std::forward<Args>(args)...);
        }

      private:
        const std::chrono::steady_clock::duration m_queueLimit{std::chrono::seconds(4)};
        const std::chrono::steady_clock::time_point m_expiration{std::chrono::steady_clock::now() + m_queueLimit};
    };

    class MagicAddTargetEventQueue : public MagicQueue
    {
      public:
        MagicAddTargetEventQueue() = default;
        MagicAddTargetEventQueue(const AddTargetEvent& aTarget) : m_AddTargetEvent(aTarget) {};
        ~MagicAddTargetEventQueue() noexcept = default;
        const AddTargetEvent& Target() const noexcept           { return m_AddTargetEvent; }

      private:
        AddTargetEvent m_AddTargetEvent;
    };

    class MagicNotifyAddTargetQueue : public MagicQueue
    {
      public:
        MagicNotifyAddTargetQueue() = default;
        MagicNotifyAddTargetQueue(const NotifyAddTarget& aTarget) : m_NotifyAddTarget(aTarget) {};
        ~MagicNotifyAddTargetQueue() noexcept = default;
        const NotifyAddTarget& Target() const noexcept            { return m_NotifyAddTarget; }

      private:
        NotifyAddTarget m_NotifyAddTarget;
    };

  private:
    std::queue<MagicAddTargetEventQueue> m_queuedEffects;
    std::queue<MagicNotifyAddTargetQueue> m_queuedRemoteEffects;

    void UpdateReviveChannels(double aDeltaSeconds) noexcept;
    void UpdateHealerChannel(double aDeltaSeconds) noexcept;
    void UpdateHealingHandsBroadcast(double aDeltaSeconds) noexcept;
    [[nodiscard]] bool IsHealingHandsSpell(uint32_t aSpellFormId, const SpellItem* apSpell = nullptr) const noexcept;
    [[nodiscard]] float GetRequiredReviveDuration(float aRestorationLevel) const noexcept;
    void UpdateVictimReviveUi(const ReviveChannelState& aState) const noexcept;
    void StopVictimReviveUi() noexcept;
    void UpdateHealerUi() const noexcept;
    void StopHealerUi() noexcept;
    void ApplyHealerCostModifiers(Actor* apCaster) noexcept;
    void ClearHealerCostModifiers(Actor* apCaster) noexcept;
    [[nodiscard]] bool HasDownedPartyMemberInRange(float aRange) noexcept;
    [[nodiscard]] Actor* FindActorByServerId(uint32_t aServerId) const noexcept;
    [[nodiscard]] std::string ResolvePlayerName(uint32_t aServerId) const;
    [[nodiscard]] std::optional<uint32_t> GetLocalServerId() const noexcept;
    bool SendHealingProximityPing(uint32_t aSpellFormId) noexcept;
    void ResetLocalHealingHandsState() noexcept;
    void HandleHealingHandsInterrupt(MagicSystem::CastingSource aSource) noexcept;
};

// Exposed so we can increase log level of just this tricky code, in debugger or a build.
// Non-const has to be initialized outside of class.
spdlog::level::level_enum MagicService::MagicQueue::m_logLevel{spdlog::level::debug};
