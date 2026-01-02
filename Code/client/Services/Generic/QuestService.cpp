#include <TiltedOnlinePCH.h>

#include <Events/ConnectedEvent.h>

#include <Services/QuestService.h>

#include <Components.h>
#include <PlayerCharacter.h>
#include <Actor.h>
#include <Forms/TESQuest.h>
#include <Games/TES.h>
#include <Games/Overrides.h>

#include <Events/ConnectedEvent.h>
#include <Events/EventDispatcher.h>

#include <Messages/RequestQuestUpdate.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Services/SyncModeService.h>
#include <Structs/SyncMode.h>
#include <TiltedCore/Filesystem.hpp>
#include <filesystem>
#include <fstream>
#include <regex>
#include <algorithm>
#include <cctype>
#include <string>
#include <cstring>

namespace
{
using GateRule = QuestService::GateRule;

uint32_t ParseHex(const std::string& aText) noexcept
{
    try
    {
        return static_cast<uint32_t>(std::stoul(aText, nullptr, 0));
    }
    catch (...)
    {
        return 0;
    }
}

struct RuleMetadata
{
    TiltedPhoques::String IdName{};
    TiltedPhoques::String Name{};
};

bool ParseRuleMetadata(const std::string& aChunk, RuleMetadata& aOut) noexcept
{
    std::regex idNameRx("\"idName\"\\s*:\\s*\"([^\"]+)\"");
    std::regex nameRx("\"name\"\\s*:\\s*\"([^\"]+)\"");

    std::smatch match;

    if (std::regex_search(aChunk, match, idNameRx))
        aOut.IdName = match[1].str().c_str();

    if (std::regex_search(aChunk, match, nameRx))
        aOut.Name = match[1].str().c_str();

    return !aOut.IdName.empty();
}

bool ParseStageRange(const std::string& aChunk, uint16_t& aStageMin, uint16_t& aStageMax, TiltedPhoques::String& aNotes) noexcept
{
    std::regex minRx("\"stageMin\"\\s*:\\s*([0-9]+)");
    std::regex maxRx("\"stageMax\"\\s*:\\s*([0-9]+)");
    std::regex notesRx("\"notes\"\\s*:\\s*\"([^\"]+)\"");

    std::smatch match;

    if (!std::regex_search(aChunk, match, minRx))
        return false;

    aStageMin = static_cast<uint16_t>(ParseHex(match[1].str()));
    aStageMax = aStageMin;
    if (std::regex_search(aChunk, match, maxRx))
        aStageMax = static_cast<uint16_t>(ParseHex(match[1].str()));

    if (aStageMax < aStageMin)
        std::swap(aStageMin, aStageMax);

    if (std::regex_search(aChunk, match, notesRx))
        aNotes = match[1].str().c_str();
    else
        aNotes.clear();

    return true;
}

bool GateStatusEquals(const QuestService::GateStatus& aLeft, const QuestService::GateStatus& aRight) noexcept
{
    return aLeft.Active == aRight.Active
        && aLeft.FormId == aRight.FormId
        && aLeft.Stage == aRight.Stage
        && aLeft.IdName == aRight.IdName
        && aLeft.Name == aRight.Name
        && aLeft.Notes == aRight.Notes;
}

void LoadRulesFromFile(const std::filesystem::path& aPath, TiltedPhoques::Vector<GateRule>& aOutRules) noexcept
{
    std::ifstream file(aPath);
    if (!file.is_open())
    {
        spdlog::warn("Quest gating: failed to open {}", aPath.string());
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::replace(content.begin(), content.end(), '\n', ' ');
    std::replace(content.begin(), content.end(), '\r', ' ');

    std::regex chunkRx("\\{[^\\{\\}]*?idName[^\\}]*?\\}");
    std::regex blacklistEntryRx("\\{[^\\{\\}]*?stageMin[^\\}]*?\\}");

    auto chunkIt = std::sregex_iterator(content.begin(), content.end(), chunkRx);
    auto end = std::sregex_iterator();
    for (; chunkIt != end; ++chunkIt)
    {
        size_t start = static_cast<size_t>(chunkIt->position());
        size_t limit = content.size();
        auto nextBase = std::next(chunkIt);
        if (nextBase != end)
            limit = static_cast<size_t>(nextBase->position());

        const std::string slice = content.substr(start, limit - start);
        RuleMetadata metadata{};
        if (!ParseRuleMetadata(slice, metadata))
            continue;

        bool added = false;
        auto entryIt = std::sregex_iterator(slice.begin(), slice.end(), blacklistEntryRx);
        for (; entryIt != end; ++entryIt)
        {
            const auto entry = entryIt->str();
            uint16_t stageMin = 0;
            uint16_t stageMax = 0;
            TiltedPhoques::String notes{};
            if (!ParseStageRange(entry, stageMin, stageMax, notes))
                continue;

            GateRule rule{};
            rule.IdName = metadata.IdName;
            rule.Name = metadata.Name;
            rule.Notes = notes;
            rule.StageMin = stageMin;
            rule.StageMax = stageMax;
            aOutRules.push_back(rule);
            added = true;
        }

        if (!added)
        {
            uint16_t stageMin = 0;
            uint16_t stageMax = 0;
            TiltedPhoques::String notes{};
            if (ParseStageRange(slice, stageMin, stageMax, notes))
            {
                GateRule rule{};
                rule.IdName = metadata.IdName;
                rule.Name = metadata.Name;
                rule.Notes = notes;
                rule.StageMin = stageMin;
                rule.StageMax = stageMax;
                aOutRules.push_back(rule);
            }
        }
    }

    if (aOutRules.empty())
        spdlog::warn("Quest gating: no rules parsed from {}", aPath.string());
}
} // namespace

using Quest = TESQuest; // Alias for PAPYRUS_FUNCTION namespace string "Quest"

static TESQuest* FindQuestByNameId(const String& name)
{
    auto* pModManager = ModManager::Get();
    if (!pModManager)
        return nullptr;

    auto& questRegistry = pModManager->quests;
    auto it = std::find_if(questRegistry.begin(), questRegistry.end(), [&name](auto* quest) {
        if (!quest)
            return false;
        const char* idName = quest->idName.AsAscii();
        return idName && std::strcmp(idName, name.c_str()) == 0;
    });

    return it != questRegistry.end() ? *it : nullptr;
}

// Helper: map PlayerId -> Actor*
static Actor* GetActorByPlayerId(uint32_t aPlayerId, entt::registry& aWorld)
{
    auto view = aWorld.view<FormIdComponent, PlayerComponent>();
    auto it = std::find_if(view.begin(), view.end(), [view, aPlayerId](auto e) { return view.get<PlayerComponent>(e).Id == aPlayerId; });
    if (it == view.end())
        return nullptr;
    const auto& formIdComponent = view.get<FormIdComponent>(*it);
    return Cast<Actor>(TESForm::GetById(formIdComponent.Id));
}

bool RuleTargetsQuest(const GateRule& aRule, TESQuest* apQuest) noexcept
{
    if (!apQuest)
        return false;

    if (!aRule.IdName.empty())
    {
        const char* idName = apQuest->idName.AsAscii();
        if (!idName || std::strcmp(idName, aRule.IdName.c_str()) != 0)
            return false;
    }

    return true;
}

bool IsQuestCompleted(const TESQuest* apQuest) noexcept
{
    if (!apQuest)
        return false;
    const uint16_t completedFlag = static_cast<uint16_t>(TESQuest::Flags::Completed);
    return (apQuest->flags & completedFlag) != 0;
}

QuestService::QuestService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_joinedConnection = aDispatcher.sink<ConnectedEvent>().connect<&QuestService::OnConnected>(this);
    m_questUpdateConnection = aDispatcher.sink<NotifyQuestUpdate>().connect<&QuestService::OnQuestUpdate>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&QuestService::OnUpdate>(this);

    // Game quest events
    auto* pEventList = EventDispatcherManager::Get();
    pEventList->questStartStopEvent.RegisterSink(this);
    pEventList->questStageEvent.RegisterSink(this);
    pEventList->loadGameEvent.RegisterSink(this);
}

void QuestService::OnConnected(const ConnectedEvent&) noexcept
{
    m_gateActive = false;
    m_gateRescanTimer = 0.0;
    m_gateRulesLoaded = false;
    m_initialGateScan = false;
    m_gateStatus = {};
    LoadGateRules();
    EvaluateGatesFromWorld();
}

BSTEventResult QuestService::OnEvent(const TESQuestStartStopEvent* apEvent, const EventDispatcher<TESQuestStartStopEvent>*)
{
    EvaluateGateForQuest(apEvent->formId, 0);

    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
        return BSTEventResult::kOk;

    if (ScopedQuestOverride::IsOverriden() || !m_world.Get().GetPartyService().IsInParty())
        return BSTEventResult::kOk;

    spdlog::info("Quest start/stop event: {:X}", apEvent->formId);

    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;

        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(__FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                             Id.LogFormat(),  pQuest->currentStage, pQuest->IsStopped() ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started,
                             static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                             pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }

        m_world.GetRunner().Queue(
            [&, formId = pQuest->formID, stageId = pQuest->currentStage, stopped = pQuest->IsStopped(), type = pQuest->type]()
            {
                GameId Id;
                auto& modSys = m_world.GetModSystem();
                if (modSys.GetServerModId(formId, Id))
                {
                    RequestQuestUpdate update;
                    update.Id = Id;
                    update.Stage = stageId;
                    update.Status = stopped ? RequestQuestUpdate::Stopped : RequestQuestUpdate::Started;
                    update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type);

                    m_world.GetTransport().Send(update);
                }
            });
    }

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESQuestStageEvent* apEvent, const EventDispatcher<TESQuestStageEvent>*)
{
    EvaluateGateForQuest(apEvent->formId, apEvent->stageId);

    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
        return BSTEventResult::kOk;

    if (ScopedQuestOverride::IsOverriden() || !m_world.Get().GetPartyService().IsInParty())
        return BSTEventResult::kOk;

    spdlog::info("Quest stage event: {:X}, stage: {}", apEvent->formId, apEvent->stageId);

    if (TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(apEvent->formId)))
    {
        if (IsNonSyncableQuest(pQuest))
            return BSTEventResult::kOk;

        if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
        {
            GameId Id;
            auto& modSys = m_world.GetModSystem();
            if (modSys.GetServerModId(pQuest->formID, Id))
            {
                spdlog::info(__FUNCTION__ ": queuing type none/misc quest gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                             Id.LogFormat(), pQuest->currentStage,
                             RequestQuestUpdate::StageUpdate,
                             static_cast<std::underlying_type_t<TESQuest::Type>>(pQuest->type),
                             pQuest->formID, pQuest->fullName.value.AsAscii());
            }
        }

        m_world.GetRunner().Queue(
            [&, formId = apEvent->formId, stageId = apEvent->stageId, type = pQuest->type]()
            {
                GameId Id;
                auto& modSys = m_world.GetModSystem();
                if (modSys.GetServerModId(formId, Id))
                {
                    RequestQuestUpdate update;
                    update.Id = Id;
                    update.Stage = stageId;
                    update.Status = RequestQuestUpdate::StageUpdate;
                    update.ClientQuestType = static_cast<std::underlying_type_t<TESQuest::Type>>(type);

                    m_world.GetTransport().Send(update);
                }
            });
    }

    return BSTEventResult::kOk;
}

BSTEventResult QuestService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    m_initialGateScan = false;
    m_gateRescanTimer = 0.0;
    return BSTEventResult::kOk;
}

void QuestService::OnQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept
{
    m_world.GetRunner().Queue([this, update = aUpdate]()
    {
        if (!TryApplyQuestUpdate(update))
        {
            spdlog::debug("Quest update pending for {:X}, stage {}", update.Id.LogFormat(), update.Stage);
            m_pendingUpdates.push_back(update);
        }
    });
}

void QuestService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_initialGateScan)
    {
        m_initialGateScan = true;
        EvaluateGatesFromWorld();
    }

    FlushPendingUpdates();
    // Re-evaluate gate rules periodically in case we missed events
    constexpr double cGateRescanInterval = 2.5;
    m_gateRescanTimer += acEvent.Delta;
    if (m_gateRescanTimer >= cGateRescanInterval)
    {
        m_gateRescanTimer = 0.0;
        EvaluateGatesFromWorld();
    }
}

void QuestService::FlushPendingUpdates() noexcept
{
    if (m_pendingUpdates.empty())
        return;

    TiltedPhoques::Vector<NotifyQuestUpdate> remaining;
    remaining.reserve(m_pendingUpdates.size());

    for (const auto& update : m_pendingUpdates)
    {
        if (!TryApplyQuestUpdate(update))
            remaining.push_back(update);
    }

    m_pendingUpdates = std::move(remaining);
}

bool QuestService::TryApplyQuestUpdate(const NotifyQuestUpdate& aUpdate) noexcept
{
    ModSystem& modSystem = World::Get().GetModSystem();
    const uint32_t formId = modSystem.GetGameId(aUpdate.Id);
    if (formId == 0)
    {
        spdlog::warn("Quest update awaiting mod resolution, base id: {:X}, mod id: {:X}", aUpdate.Id.BaseId, aUpdate.Id.ModId);
        return false;
    }

    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(formId));
    if (!pQuest)
    {
        spdlog::warn("Quest {:X} not loaded yet, deferring update", formId);
        return false;
    }

    if (pQuest->type == TESQuest::Type::None || pQuest->type == TESQuest::Type::Miscellaneous)
    {
        spdlog::info(__FUNCTION__ ": receiving type none/misc quest update gameId {:X} questStage {} questStatus {} questType {} formId {:X} name {}",
                     aUpdate.Id.LogFormat(), aUpdate.Stage, aUpdate.Status,
                     aUpdate.ClientQuestType, formId, pQuest->fullName.value.AsAscii());
    }

    bool bResult = false;

    ScopedQuestOverride questOverride;

    switch (aUpdate.Status)
    {
    case NotifyQuestUpdate::Started:
    {
        bool startSuccess = false;
        pQuest->EnsureQuestStarted(startSuccess, true);
        pQuest->SetActive(true);
        pQuest->ScriptSetStage(aUpdate.Stage);
        bResult = true;
        spdlog::info("Remote quest started: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    }
    case NotifyQuestUpdate::StageUpdate:
        pQuest->SetActive(true);
        pQuest->ScriptSetStage(aUpdate.Stage);
        bResult = true;
        spdlog::info("Remote quest updated: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    case NotifyQuestUpdate::Stopped:
        bResult = StopQuest(formId);
        spdlog::info("Remote quest stopped: {:X}, stage: {}", formId, aUpdate.Stage);
        break;
    default:
        spdlog::warn("Unhandled quest update status {} for quest {:X}", aUpdate.Status, formId);
        break;
    }

    if (!bResult)
        spdlog::error("Failed to update the client quest state, quest: {:X}, stage: {}, status: {}", formId, aUpdate.Stage, aUpdate.Status);

    return bResult;
}

bool QuestService::StopQuest(uint32_t aformId)
{
    TESQuest* pQuest = Cast<TESQuest>(TESForm::GetById(aformId));
    if (pQuest)
    {
        ScopedQuestOverride _;
        pQuest->SetActive(false);
        pQuest->SetStopped();
        return true;
    }

    return false;
}

static constexpr std::array kNonSyncableQuestIds = std::to_array<uint32_t>({
    0x2BA16,   // Werewolf transformation quest
    0x20071D0, // Vampire transformation quest
    0x3AC44,   // MS13BleakFallsBarrowLeverScene
    0xF2593 // Skill experience quest
});

bool QuestService::IsNonSyncableQuest(TESQuest* apQuest)
{
    return    apQuest->stages.Empty()
           || std::find(kNonSyncableQuestIds.begin(), kNonSyncableQuestIds.end(), apQuest->formID) != kNonSyncableQuestIds.end();
}

void QuestService::DebugDumpQuests()
{
    auto* pModManager = ModManager::Get();
    if (!pModManager)
        return;

    auto& quests = pModManager->quests;
    for (TESQuest* pQuest : quests)
    {
        if (!pQuest)
            continue;
        const char* idName = pQuest->idName.AsAscii();
        spdlog::info("{:X}|{}|{}|{}", pQuest->formID, (uint8_t)pQuest->type, pQuest->priority, idName ? idName : "");
    }
}

void QuestService::EvaluateGateForQuest(uint32_t aFormId, uint16_t aStage) noexcept
{
    if (!m_gateRulesLoaded)
        LoadGateRules();

    const auto it = std::find_if(m_gateRules.begin(), m_gateRules.end(),
        [&](const GateRule& rule) { return RuleTargetsQuest(rule, Cast<TESQuest>(TESForm::GetById(aFormId))); });
    if (it == m_gateRules.end())
        return;

    // If the event is relevant, recompute the full gate state using current quest data.
    EvaluateGatesFromWorld();
}

void QuestService::EvaluateGatesFromWorld() noexcept
{
    if (!m_gateRulesLoaded)
        LoadGateRules();

    bool shouldGate = false;
    uint32_t matchedQuestId = 0;
    uint16_t matchedStage = 0;
    const GateRule* matchedRule = nullptr;
    TESQuest* matchedQuest = nullptr;
    for (const auto& rule : m_gateRules)
    {
        TESQuest* pQuest = nullptr;
        uint32_t formId = 0;

        if (!rule.IdName.empty())
        {
            pQuest = FindQuestByNameId(rule.IdName);
            if (pQuest)
                formId = pQuest->formID;
        }
        if (!RuleTargetsQuest(rule, pQuest))
            continue;

        if (!pQuest || pQuest->IsStopped() || IsQuestCompleted(pQuest))
            continue;

        if (rule.Matches(pQuest->currentStage))
        {
            shouldGate = true;
            matchedQuestId = formId;
            matchedStage = pQuest->currentStage;
            matchedRule = &rule;
            matchedQuest = pQuest;
            break;
        }
    }

    GateStatus nextStatus{};
    nextStatus.Active = shouldGate;
    if (shouldGate)
    {
        nextStatus.FormId = matchedQuestId;
        nextStatus.Stage = matchedStage;
        if (matchedRule)
        {
            nextStatus.IdName = matchedRule->IdName;
            nextStatus.Name = matchedRule->Name;
            nextStatus.Notes = matchedRule->Notes;
        }
        if (nextStatus.Name.empty() && matchedQuest)
        {
            if (const char* fullName = matchedQuest->fullName.value.AsAscii())
                nextStatus.Name = fullName;
        }
        if (nextStatus.IdName.empty() && matchedQuest)
        {
            if (const char* idName = matchedQuest->idName.AsAscii())
                nextStatus.IdName = idName;
        }
    }

    const bool statusChanged = !GateStatusEquals(m_gateStatus, nextStatus);
    m_gateStatus = nextStatus;

    const SyncMode desiredMode = shouldGate ? SyncMode::Ghost : SyncMode::Normal;

    const bool modeChange =
        (shouldGate != m_gateActive || m_world.GetSyncModeService().GetLocalMode() != desiredMode);
    if (modeChange)
    {
        m_gateActive = shouldGate;
        m_world.GetSyncModeService().SetLocalMode(desiredMode);
        if (shouldGate)
            spdlog::info("Entering quest sync gate for quest {:X} stage {}", matchedQuestId, matchedStage);
        else
            spdlog::info("Exiting quest sync gate");
    }
    else if (statusChanged && shouldGate)
    {
        m_world.GetSyncModeService().RefreshOverlaySyncStatus();
    }
}

void QuestService::LoadGateRules() noexcept
{
    m_gateRulesLoaded = true;
    m_gateRules.clear();

    const auto basePath = TiltedPhoques::GetPath();
    const std::filesystem::path isolationDir = basePath / "Isolation";

    std::error_code ec;
    if (!std::filesystem::exists(isolationDir, ec))
    {
        std::filesystem::create_directories(isolationDir, ec);
        if (ec)
        {
            spdlog::warn("Quest gating: failed to create isolation directory {}", isolationDir.string());
            return;
        }
        spdlog::info("Quest gating: created isolation directory {}", isolationDir.string());
    }

    for (const auto& entry : std::filesystem::directory_iterator(isolationDir, ec))
    {
        if (ec)
            break;

        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".json")
            continue;

        LoadRulesFromFile(entry.path(), m_gateRules);
    }

    spdlog::info("Quest gating: loaded {} gate rules from {}", m_gateRules.size(), isolationDir.string());
}
