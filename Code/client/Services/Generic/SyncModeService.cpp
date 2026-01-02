#include <TiltedOnlinePCH.h>

#include <Services/SyncModeService.h>

#include <OverlayApp.hpp>
#include <include/cef_values.h>

#include <Components.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/UpdateEvent.h>
#include <Games/References.h>
#include <Games/Skyrim/Actor.h>
#include <PlayerCharacter.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerSyncMode.h>
#include <Messages/RequestSetSyncMode.h>
#include <Messages/RequestDroppedItems.h>
#include <Messages/RequestCurrentWeather.h>
#include <Services/OverlayService.h>
#include <Services/QuestService.h>
#include <Services/PartyService.h>
#include <Services/TransportService.h>
#include <Services/CharacterService.h>
#include <Systems/ModSystem.h>
#include <World.h>
#include <Games/Memory.h>
#include <cstring>
#include <ExtraData/ExtraDataList.h>
#include <Forms/TESGlobal.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Games/Overrides.h>
#include <Forms/TESNPC.h>
#include <ExtraData/ExtraGhost.h>
#include <string>

namespace
{
// Ghost visual spell (applied to remote player actors only).
constexpr uint32_t kGhostGlowSpellFormId = 0x000D2056;
// TESObjectREFR::RecordFlags::kCollisionsDisabled (CommonLibSSE-Reference) for refs/actors.
constexpr uint32_t kCollisionDisabledFlag = 1u << 4;
constexpr uint32_t kGhostRefFlagMask = kCollisionDisabledFlag | TESForm::IGNORE_FRIENDLY_HITS;

struct GhostGlowEffect
{
    MagicItem* pSpell = nullptr;
    TiltedPhoques::Vector<EffectItem*> effects;
};

GhostGlowEffect GetGhostGlowEffect() noexcept
{
    static bool s_attempted = false;
    static GhostGlowEffect s_effect{};

    if (!s_attempted)
    {
        s_attempted = true;

        s_effect.pSpell = Cast<MagicItem>(TESForm::GetById(kGhostGlowSpellFormId));
        if (!s_effect.pSpell)
        {
            spdlog::warn("Ghost glow spell {:X} not found (or not a MagicItem); blue glow disabled", kGhostGlowSpellFormId);
            return s_effect;
        }

        if (s_effect.pSpell->listOfEffects.Empty())
        {
            spdlog::warn("Ghost glow spell {:X} has no effects; blue glow disabled", kGhostGlowSpellFormId);
            s_effect.pSpell = nullptr;
            return s_effect;
        }

        // Apply all effects from the spell (some spells bundle multiple visuals).
        s_effect.effects.clear();
        for (auto* pEffect : s_effect.pSpell->listOfEffects)
        {
            if (!pEffect || !pEffect->pEffectSetting)
                continue;
            s_effect.effects.push_back(pEffect);
        }

        if (s_effect.effects.empty())
        {
            spdlog::warn("Ghost glow spell {:X} has no valid effects; blue glow disabled", kGhostGlowSpellFormId);
            s_effect = {};
            return s_effect;
        }
    }

    return s_effect;
}

void ApplyGhostGlowEffect(Actor* apActor) noexcept
{
    if (!apActor)
        return;

    const auto effect = GetGhostGlowEffect();
    if (!effect.pSpell || effect.effects.empty())
        return;

    // Prevent our own forced visuals from feeding back into magic sync hooks/events.
    ScopedSpellCastOverride _;

    for (auto* pEffectItem : effect.effects)
    {
        MagicTarget::AddTargetData data{};
        data.pCaster = nullptr;
        data.pSpell = effect.pSpell;
        data.pEffectItem = pEffectItem;
        data.fMagnitude = 1.f;
        data.fUnkFloat1 = 1.f;
        data.eCastingSource = MagicSystem::CastingSource::CASTING_SOURCE_COUNT;

        apActor->magicTarget.AddTarget(data, false, false);
    }
}
} // namespace

SyncModeService::SyncModeService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&SyncModeService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&SyncModeService::OnDisconnected>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&SyncModeService::OnUpdate>(this);
    m_playerJoinedConnection = aDispatcher.sink<NotifyPlayerJoined>().connect<&SyncModeService::OnPlayerJoined>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&SyncModeService::OnPlayerLeft>(this);
    m_notifySyncModeConnection = aDispatcher.sink<NotifyPlayerSyncMode>().connect<&SyncModeService::OnNotifyPlayerSyncMode>(this);
    m_remoteRemovedConnection = m_world.on_destroy<RemoteComponent>().connect<&SyncModeService::OnRemoteComponentRemoved>(this);
}

void SyncModeService::SetLocalMode(const SyncMode aMode) noexcept
{
    const auto previousMode = m_localMode;
    if (previousMode == aMode)
        return;

    TiltedPhoques::Set<uint32_t> refreshIds;
    if (previousMode == SyncMode::Ghost && aMode == SyncMode::Normal)
    {
        auto view = m_world.view<GhostComponent, PlayerComponent, RemoteComponent>();
        for (auto entity : view)
        {
            const auto& playerComponent = view.get<PlayerComponent>(entity);
            if (playerComponent.Id == m_localPlayerId)
                continue;

            const auto it = m_remoteModes.find(playerComponent.Id);
            if (it != std::end(m_remoteModes) && it->second == SyncMode::Ghost)
                continue;

            refreshIds.insert(view.get<RemoteComponent>(entity).Id);
        }
    }

    m_localMode = aMode;

    UpdateWorldEncounters();

    if (auto* pCharacterService = m_world.ctx().find<CharacterService>(); pCharacterService)
        pCharacterService->OnSyncModeChanged(previousMode, m_localMode);

    if (m_transport.IsOnline())
    {
        RequestSetSyncMode request{};
        request.Mode = aMode;
        m_transport.Send(request);
    }

    RefreshGhostStates();
    UpdateOverlaySyncStatus();

    if (!refreshIds.empty())
        RefreshRemotePlayers(refreshIds);

    if (previousMode == SyncMode::Ghost && aMode == SyncMode::Normal)
    {
        // Lightweight resync: ask for cell drops and weather so we can clean up obvious gaps.
        RequestResync();
    }
}

void SyncModeService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    m_localPlayerId = acEvent.PlayerId;
    m_remoteModes.clear();

    UpdateWorldEncounters();

    if (m_localMode != SyncMode::Normal)
    {
        RequestSetSyncMode request{};
        request.Mode = m_localMode;
        m_transport.Send(request);
    }

    RefreshGhostStates();
    UpdateOverlaySyncStatus();
}

void SyncModeService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    // Proactively strip ghost state from any remaining remote actors before we clear tracking data.
    ClearGhostStates();

    m_localPlayerId = 0;
    m_localMode = SyncMode::Normal;
    m_remoteModes.clear();
    m_pending3DRefresh.clear();
    m_glowApplied.clear();
    m_addedExtraGhost.clear();
    m_originalNpcFlags.clear();
    m_originalRefFlagBits.clear();
    UpdateWorldEncounters();
    UpdateOverlaySyncStatus();
}

void SyncModeService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_transport.IsOnline())
        return;

    RefreshGhostStates();

    if (!m_pending3DRefresh.empty())
    {
        // Process 3D refresh requests outside of the UpdateReference3D hook (more stable during cell loads).
        TiltedPhoques::Set<uint32_t> pending;
        pending.swap(m_pending3DRefresh);

        for (const uint32_t formId : pending)
        {
            Actor* pActor = Cast<Actor>(TESForm::GetById(formId));
            if (!pActor)
                continue;

            if (pActor == PlayerCharacter::Get())
                continue;

            // Only remote player actors are eligible for ghost visuals.
            auto view = m_world.view<FormIdComponent, PlayerComponent, RemoteComponent>();
            const auto it = std::find_if(std::begin(view), std::end(view),
                [view, formId](entt::entity e) { return view.get<FormIdComponent>(e).Id == formId; });
            if (it == std::end(view))
                continue;

            const auto entity = *it;
            const bool isGhosted = m_world.any_of<GhostComponent>(entity);
            if (!isGhosted)
                continue;

            ApplyGhostToActor(pActor, true);
        }
    }
}

void SyncModeService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    m_remoteModes[acMessage.PlayerId] = SyncMode::Normal;
}

void SyncModeService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    m_remoteModes.erase(acMessage.PlayerId);
}

void SyncModeService::OnNotifyPlayerSyncMode(const NotifyPlayerSyncMode& acMessage) noexcept
{
    SyncMode previousMode = SyncMode::Normal;
    entt::entity playerEntity = entt::null;
    uint32_t refreshServerId = 0;
    bool wasGhosted = false;

    if (acMessage.PlayerId == m_localPlayerId)
    {
        previousMode = m_localMode;
    }
    else
    {
        if (auto it = m_remoteModes.find(acMessage.PlayerId); it != std::end(m_remoteModes))
            previousMode = it->second;

        auto view = m_world.view<PlayerComponent, RemoteComponent>();
        const auto it = std::find_if(std::begin(view), std::end(view),
            [view, playerId = acMessage.PlayerId](entt::entity e) { return view.get<PlayerComponent>(e).Id == playerId; });
        if (it != std::end(view))
        {
            playerEntity = *it;
            refreshServerId = view.get<RemoteComponent>(playerEntity).Id;
            wasGhosted = m_world.any_of<GhostComponent>(playerEntity);
        }
    }

    if (acMessage.PlayerId == m_localPlayerId)
        m_localMode = acMessage.Mode;
    else
        m_remoteModes[acMessage.PlayerId] = acMessage.Mode;

    RefreshGhostStates();
    if (acMessage.PlayerId == m_localPlayerId)
        UpdateOverlaySyncStatus();

    if (acMessage.PlayerId != m_localPlayerId && previousMode != acMessage.Mode && refreshServerId != 0)
    {
        const bool isGhosted = m_world.valid(playerEntity) && m_world.any_of<GhostComponent>(playerEntity);
        if (m_localMode == SyncMode::Normal && previousMode == SyncMode::Ghost && acMessage.Mode == SyncMode::Normal && (wasGhosted || isGhosted))
        {
            if (auto* pCharacterService = m_world.ctx().find<CharacterService>(); pCharacterService)
                pCharacterService->RefreshRemotePlayer(refreshServerId);
        }
    }
}

void SyncModeService::UpdateOverlaySyncStatus() const noexcept
{
    auto* pOverlay = m_world.GetOverlayService().GetOverlayApp();
    if (!pOverlay)
        return;

    const bool isolated = (m_transport.IsOnline() && m_localMode == SyncMode::Ghost);
    std::string title;
    std::string detail;
    std::string moreInfo;

    if (isolated)
    {
        title = "Quest Isolation";
        detail = "Sync paused";

        if (auto* pQuestService = m_world.ctx().find<QuestService>(); pQuestService)
        {
            const auto& gateStatus = pQuestService->GetGateStatus();
            if (gateStatus.Active)
            {
                std::string questLabel;
                if (!gateStatus.Name.empty())
                    questLabel = gateStatus.Name.c_str();
                else if (!gateStatus.IdName.empty())
                    questLabel = gateStatus.IdName.c_str();

                if (!questLabel.empty())
                    detail = "Sync paused due to quest: " + questLabel;

                moreInfo = "Stage " + std::to_string(gateStatus.Stage);
                if (!gateStatus.IdName.empty())
                    moreInfo += " (" + std::string(gateStatus.IdName.c_str()) + ")";
                if (!gateStatus.Notes.empty())
                    moreInfo += "\nNotes: " + std::string(gateStatus.Notes.c_str());
            }
        }
    }

    auto pArgs = CefListValue::Create();
    pArgs->SetBool(0, isolated);
    pArgs->SetString(1, title);
    pArgs->SetString(2, detail);
    pArgs->SetString(3, moreInfo);

    pOverlay->ExecuteAsync("setSyncStatus", pArgs);
}

void SyncModeService::UpdateWorldEncounters() noexcept
{
    TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
    if (!pWorldEncountersEnabled)
        return;

    if (m_localMode == SyncMode::Ghost || !m_transport.IsOnline())
    {
        pWorldEncountersEnabled->f = 1.f;
        return;
    }

    const auto& partyService = m_world.GetPartyService();
    pWorldEncountersEnabled->f = (partyService.IsInParty() && partyService.IsLeader()) ? 1.f : 0.f;
}

bool SyncModeService::ShouldGhost(const uint32_t aPlayerId) const noexcept
{
    if (aPlayerId == m_localPlayerId)
        return m_localMode == SyncMode::Ghost;

    // While we're quest-gated, all remote players should be presented as ghosts locally (regardless of their own mode).
    if (m_localMode == SyncMode::Ghost)
        return true;

    const auto itor = m_remoteModes.find(aPlayerId);
    if (itor != std::end(m_remoteModes))
        return itor->second == SyncMode::Ghost;

    return false;
}

void SyncModeService::RefreshGhostStates() noexcept
{
    auto view = m_world.view<PlayerComponent, RemoteComponent, FormIdComponent>();
    for (auto entity : view)
    {
        const auto& playerComponent = view.get<PlayerComponent>(entity);
        const bool shouldGhost = ShouldGhost(playerComponent.Id);

        const auto* pGhostComponent = m_world.try_get<GhostComponent>(entity);
        const bool isGhosted = pGhostComponent && pGhostComponent->IsGhost;

        if (shouldGhost != isGhosted)
        {
            ToggleGhostState(entity, shouldGhost);
            continue;
        }
    }
}

void SyncModeService::ClearGhostStates() noexcept
{
    auto view = m_world.view<GhostComponent, FormIdComponent>();
    TiltedPhoques::Vector<entt::entity> entities(view.begin(), view.end());

    for (auto entity : entities)
    {
        if (!ToggleGhostState(entity, false))
            m_world.remove<GhostComponent>(entity);
    }
}

bool SyncModeService::ToggleGhostState(const entt::entity aEntity, const bool aGhost) noexcept
{
    const auto* pFormIdComponent = m_world.try_get<FormIdComponent>(aEntity);
    if (!pFormIdComponent)
        return false;

    Actor* pActor = Cast<Actor>(TESForm::GetById(pFormIdComponent->Id));
    if (!pActor)
        return false;

    if (!ApplyGhostToActor(pActor, aGhost))
        return false;

    if (aGhost)
        m_world.emplace_or_replace<GhostComponent>(aEntity, true);
    else
        m_world.remove<GhostComponent>(aEntity);

    return true;
}

bool SyncModeService::ApplyGhostToActor(Actor* apActor, const bool aGhost) noexcept
{
    if (!apActor)
        return false;

    const bool has3D = apActor->GetNiNode() != nullptr;
    const uint32_t formId = apActor->formID;

    if (aGhost)
    {
        const bool firstGhostApplication = m_originalRefFlagBits.find(formId) == std::end(m_originalRefFlagBits);
        if (firstGhostApplication)
            m_originalRefFlagBits[formId] = apActor->flags & kGhostRefFlagMask;

        apActor->flags |= kGhostRefFlagMask;

        if (auto* pNpc = Cast<TESNPC>(apActor->baseForm))
        {
            const uint32_t baseId = pNpc->formID;
            // Base forms are shared across player clones, so keep a refcount per NPC base.
            auto& npcState = m_originalNpcFlags[baseId];
            if (npcState.RefCount == 0)
                npcState.Flags = pNpc->actorData.flags;
            if (firstGhostApplication)
                npcState.RefCount++;

            // Vanilla ghost flag: this is what makes actors behave as "ghosts" (no collision / hard interaction)
            // and is more reliable than trying to manually tweak Havok state.
            pNpc->actorData.flags |= (1u << 29); // TESActorBaseData::kIsGhost
        }

        // Add per-reference ExtraGhost so Havok/activation treat the actor as intangible.
        if (auto* pExtraData = apActor->GetExtraDataList())
        {
            if (!pExtraData->Contains(ExtraDataType::Ghost))
            {
                if (auto* pExtraGhost = Memory::New<ExtraGhost>())
                {
                    pExtraGhost->ghost = true;
                    if (!pExtraData->bitfield)
                    {
                        pExtraData->bitfield = Memory::Allocate<ExtraDataList::Bitfield>();
                        std::memset(pExtraData->bitfield, 0, sizeof(ExtraDataList::Bitfield));
                    }
                    if (pExtraData->Add(ExtraDataType::Ghost, pExtraGhost))
                        m_addedExtraGhost.insert(formId);
                    else
                        Memory::Delete(pExtraGhost);
                }
            }
        }

        if (has3D)
        {
            if (m_glowApplied.find(formId) == std::end(m_glowApplied))
            {
                ApplyGhostGlowEffect(apActor);
                m_glowApplied.insert(formId);
            }
            apActor->UpdateAlpha();
            apActor->QueueUpdate();
        }

        return true;
    }

    m_glowApplied.erase(formId);
    if (auto it = m_originalRefFlagBits.find(formId); it != std::end(m_originalRefFlagBits))
    {
        apActor->flags = (apActor->flags & ~kGhostRefFlagMask) | it->second;
        m_originalRefFlagBits.erase(it);
    }
    else
    {
        apActor->flags &= ~kGhostRefFlagMask;
    }
    if (auto* pNpc = Cast<TESNPC>(apActor->baseForm))
    {
        const uint32_t baseId = pNpc->formID;
        if (auto it = m_originalNpcFlags.find(baseId); it != std::end(m_originalNpcFlags))
        {
            auto& npcState = it->second;
            if (npcState.RefCount > 0)
                npcState.RefCount--;

            if (npcState.RefCount == 0)
            {
                pNpc->actorData.flags = npcState.Flags;
                m_originalNpcFlags.erase(it);
            }
            else
            {
                // Keep the ghost flag active while other ghosted actors share this base.
                pNpc->actorData.flags |= (1u << 29);
            }
        }
        else
        {
            pNpc->actorData.flags &= ~(1u << 29);
        }
    }
    if (auto* pExtraData = apActor->GetExtraDataList())
    {
        if (m_addedExtraGhost.contains(formId))
        {
            if (auto* pExtraGhost = pExtraData->GetByType(ExtraDataType::Ghost))
            {
                pExtraData->Remove(ExtraDataType::Ghost, pExtraGhost);
                Memory::Delete(pExtraGhost);
            }
            m_addedExtraGhost.erase(formId);
        }
    }
    if (has3D)
    {
        apActor->UpdateAlpha();
        apActor->QueueUpdate();
    }

    return true;
}

void SyncModeService::CollectGhostedRemotePlayerServerIds(TiltedPhoques::Set<uint32_t>& aOut) const noexcept
{
    auto view = m_world.view<GhostComponent, PlayerComponent, RemoteComponent>();
    for (auto entity : view)
    {
        const auto& playerComponent = view.get<PlayerComponent>(entity);
        if (playerComponent.Id == m_localPlayerId)
            continue;

        aOut.insert(view.get<RemoteComponent>(entity).Id);
    }
}

void SyncModeService::RefreshRemotePlayers(const TiltedPhoques::Set<uint32_t>& aServerIds) noexcept
{
    if (aServerIds.empty())
        return;

    auto* pCharacterService = m_world.ctx().find<CharacterService>();
    if (!pCharacterService)
        return;

    for (const auto serverId : aServerIds)
        pCharacterService->RefreshRemotePlayer(serverId);
}

void SyncModeService::RequestResync() const noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    RequestDroppedItems drops{};
    drops.RequestAll = false;

    auto& modSystem = m_world.GetModSystem();
    if (TESObjectCELL* pCell = pPlayer->parentCell)
    {
        drops.HasCellFilter = modSystem.GetServerModId(pCell->formID, drops.CellId);
        if (auto* pWorldSpace = pCell->worldspace)
            drops.HasWorldSpaceFilter = modSystem.GetServerModId(pWorldSpace->formID, drops.WorldSpaceId);
    }

    m_transport.Send(drops);

    RequestCurrentWeather weather{};
    m_transport.Send(weather);
}

void SyncModeService::OnRemoteComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto* pGhostComponent = aRegistry.try_get<GhostComponent>(aEntity);
    const auto* pFormIdComponent = aRegistry.try_get<FormIdComponent>(aEntity);
    if (!pGhostComponent || !pFormIdComponent)
        return;

    Actor* pActor = Cast<Actor>(TESForm::GetById(pFormIdComponent->Id));
    if (pActor)
        ApplyGhostToActor(pActor, false);

    aRegistry.remove<GhostComponent>(aEntity);
}

void SyncModeService::OnActor3DUpdated(Actor* apActor) noexcept
{
    if (!apActor)
        return;

    // Never change the local player's visuals; only remote player actors are ghosted.
    if (apActor == PlayerCharacter::Get())
        return;

    // Only remote player actors are eligible for ghost visuals.
    const auto* pExtension = apActor->GetExtension();
    if (!pExtension || !pExtension->IsRemotePlayer())
        return;

    // FaceGen tints can drop when the 3D rebuilds; force a re-apply next update.
    {
        auto view = m_world.view<FormIdComponent, FaceGenComponent>();
        const uint32_t formId = apActor->formID;
        const auto it = std::find_if(std::begin(view), std::end(view),
            [view, formId](entt::entity e) { return view.get<FormIdComponent>(e).Id == formId; });
        if (it != std::end(view))
            view.get<FaceGenComponent>(*it).Generated = false;
    }

    // Force a re-apply after 3D rebuilds/cell transitions (effects can get dropped when 3D reloads).
    m_glowApplied.erase(apActor->formID);

    // Defer work to OnUpdate to avoid doing extra engine calls from inside UpdateReference3D.
    m_pending3DRefresh.insert(apActor->formID);
}

void SyncModeService::OnLoadGameReset() noexcept
{
    ClearGhostStates();
    m_pending3DRefresh.clear();
    m_glowApplied.clear();
    m_addedExtraGhost.clear();
    m_originalNpcFlags.clear();
    m_originalRefFlagBits.clear();
    m_remoteModes.clear();
}
