#pragma once

#include <World.h>
#include <Events/EventDispatcher.h>
#include <Events/UpdateEvent.h>
#include <Games/Events.h>
#include <TiltedCore/Stl.hpp>
#include <Messages/NotifyQuestUpdate.h>
#include <cstdint>

struct NotifyQuestUpdate;

struct TESQuest;

/**
 * @brief Handles quest sync
 */
class QuestService final : public BSTEventSink<TESQuestStartStopEvent>, public BSTEventSink<TESQuestStageEvent>, public BSTEventSink<TESLoadGameEvent>
{
public:
    QuestService(World&, entt::dispatcher&);
    ~QuestService() = default;

    static bool IsNonSyncableQuest(TESQuest* apQuest);
    static void DebugDumpQuests();
    static bool StopQuest(uint32_t aformId);

    struct GateRule
    {
        TiltedPhoques::String IdName{};
        TiltedPhoques::String Name{};
        TiltedPhoques::String Notes{};
        uint16_t StageMin{0};
        uint16_t StageMax{0};

        [[nodiscard]] bool Matches(uint16_t aStage) const noexcept { return aStage >= StageMin && aStage <= StageMax; }
    };

    struct GateStatus
    {
        bool Active{false};
        uint32_t FormId{0};
        uint16_t Stage{0};
        TiltedPhoques::String IdName{};
        TiltedPhoques::String Name{};
        TiltedPhoques::String Notes{};
    };

    [[nodiscard]] const GateStatus& GetGateStatus() const noexcept { return m_gateStatus; }

private:
    friend struct QuestEventHandler;

    void OnConnected(const ConnectedEvent&) noexcept;

    // Game quest events
    BSTEventResult OnEvent(const TESQuestStartStopEvent*, const EventDispatcher<TESQuestStartStopEvent>*) override;
    BSTEventResult OnEvent(const TESQuestStageEvent*, const EventDispatcher<TESQuestStageEvent>*) override;
    BSTEventResult OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*) override;

    // Network quest updates
    void OnQuestUpdate(const NotifyQuestUpdate&) noexcept;
    void OnUpdate(const UpdateEvent&) noexcept;
    bool TryApplyQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept;
    void FlushPendingUpdates() noexcept;
    void EvaluateGateForQuest(uint32_t aFormId, uint16_t aStage) noexcept;
    void EvaluateGatesFromWorld() noexcept;
    void LoadGateRules() noexcept;

    World& m_world;

    // Existing connections
    entt::scoped_connection m_joinedConnection;
    entt::scoped_connection m_leftConnection;
    entt::scoped_connection m_questUpdateConnection;
    entt::scoped_connection m_updateConnection;

    TiltedPhoques::Vector<NotifyQuestUpdate> m_pendingUpdates;
    TiltedPhoques::Vector<GateRule> m_gateRules;
    GateStatus m_gateStatus{};

    double m_gateRescanTimer{0.0};
    bool m_initialGateScan{false};
    bool m_gateActive{false};
    bool m_gateRulesLoaded{false};
};
