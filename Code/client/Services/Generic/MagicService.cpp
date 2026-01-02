#include <Services/MagicService.h>
#include <Services/PlayerService.h>
#include <Messages/NotifyPartyMemberDowned.h>

#include <World.h>

#include <Events/UpdateEvent.h>
#include <Events/SpellCastEvent.h>
#include <Events/InterruptCastEvent.h>
#include <Events/AddTargetEvent.h>
#include <Events/RemoveSpellEvent.h>

#include <Messages/RemoveSpellRequest.h>

#include <Messages/SpellCastRequest.h>
#include <Messages/InterruptCastRequest.h>

#include <Messages/NotifySpellCast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyHealingProximity.h>
#include <Messages/HealingProximityRequest.h>

#include <Actor.h>
#include <Magic/ActorMagicCaster.h>
#include <Games/ActorExtension.h>
#include <EquipManager.h>

#include <Structs/Skyrim/AnimationGraphDescriptor_VampireLordBehavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_WerewolfBehavior.h>

#include <Games/Overrides.h>

#include <Forms/SpellItem.h>
#include <PlayerCharacter.h>

#include <Games/TES.h>
#include <OverlayApp.hpp>
#include <ChatMessageTypes.h>
#include <Components.h>
#include <Games/Skyrim/Forms/ActorValueInfo.h>
#include <Games/Skyrim/Misc/ActorValueOwner.h>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <client/Utils.h>

namespace
{
constexpr uint32_t cHealingHandsBaseId = 0x4D3F2;
constexpr float cHealingHandsRange = 300.0f;
constexpr auto cReviveChannelTimeout = std::chrono::milliseconds(2000);
constexpr uint32_t cFormIdMask = 0x00FFFFFF;
constexpr double cHealingHandsPingInterval = 0.35;
}

MagicService::MagicService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&MagicService::OnUpdate>(this);
    m_spellCastEventConnection = m_dispatcher.sink<SpellCastEvent>().connect<&MagicService::OnSpellCastEvent>(this);
    m_notifySpellCastConnection = m_dispatcher.sink<NotifySpellCast>().connect<&MagicService::OnNotifySpellCast>(this);
    m_interruptCastEventConnection = m_dispatcher.sink<InterruptCastEvent>().connect<&MagicService::OnInterruptCastEvent>(this);
    m_notifyInterruptCastConnection = m_dispatcher.sink<NotifyInterruptCast>().connect<&MagicService::OnNotifyInterruptCast>(this);
    m_addTargetEventConnection = m_dispatcher.sink<AddTargetEvent>().connect<&MagicService::OnAddTargetEvent>(this);
    m_notifyAddTargetConnection = m_dispatcher.sink<NotifyAddTarget>().connect<&MagicService::OnNotifyAddTarget>(this);
    m_removeSpellEventConnection = m_dispatcher.sink<RemoveSpellEvent>().connect<&MagicService::OnRemoveSpellEvent>(this);
    m_notifyRemoveSpell = m_dispatcher.sink<NotifyRemoveSpell>().connect<&MagicService::OnNotifyRemoveSpell>(this);
    m_notifyHealingProximityConnection = m_dispatcher.sink<NotifyHealingProximity>().connect<&MagicService::OnNotifyHealingProximity>(this);

    // Listen for party member downed/revived notifications
    m_notifyPartyMemberDownedConnection = m_dispatcher.sink<NotifyPartyMemberDowned>().connect<&MagicService::OnNotifyPartyMemberDowned>(this);
}

void MagicService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    ApplyQueuedEffects();

    UpdateRevealOtherPlayersEffect();
    UpdateRevealDownedPlayersEffect();
    UpdateReviveChannels(acEvent.Delta);
    UpdateHealerChannel(acEvent.Delta);
    UpdateHealingHandsBroadcast(acEvent.Delta);
}

void MagicService::OnSpellCastEvent(const SpellCastEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
        return;

    if (!acEvent.pCaster->pCasterActor || !acEvent.pCaster->pCasterActor->GetNiNode())
    {
        spdlog::warn("Spell cast event has no actor or actor is not loaded");
        return;
    }

    SpellItem* pSpell = Cast<SpellItem>(TESForm::GetById(acEvent.SpellId));

    if (pSpell && pSpell->IsHealingSpell() && IsHealingHandsSpell(acEvent.SpellId, pSpell))
    {
        if (SendHealingProximityPing(acEvent.SpellId))
        {
            const auto source = static_cast<MagicSystem::CastingSource>(acEvent.pCaster->GetCastingSource());
            if (source >= 0 && source < MagicSystem::CastingSource::CASTING_SOURCE_COUNT)
            {
                m_localHealingHandsSources[source] = true;
                m_isLocalHealingHandsActive = true;
                m_activeHealingHandsSpellId = acEvent.SpellId;
                m_healingHandsPingAccumulator = cHealingHandsPingInterval;
            }
        }
    }

    // only sync concentration spells through spell cast sync, the rest through projectile sync for accuracy
    if (pSpell)
    {
        if ((pSpell->eCastingType != MagicSystem::CastingType::CONCENTRATION || pSpell->IsHealingSpell()) && !pSpell->IsWardSpell() && !pSpell->IsInvisibilitySpell())
        {
            spdlog::debug("Canceled magic spell");
            return;
        }
    }

    uint32_t formId = acEvent.pCaster->pCasterActor->formID;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto casterEntityIt = std::find_if(std::begin(view), std::end(view), [formId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (casterEntityIt == std::end(view))
        return;

    auto& localComponent = view.get<LocalComponent>(*casterEntityIt);

    SpellCastRequest request{};

    request.CasterId = localComponent.Id;
    request.CastingSource = acEvent.pCaster->GetCastingSource();
    request.IsDualCasting = acEvent.pCaster->GetIsDualCasting();

    if (!m_world.GetModSystem().GetServerModId(acEvent.SpellId, request.SpellFormId))
    {
        spdlog::error("Server spell id not found for spell form id {:X}", acEvent.SpellId);
        return;
    }

    if (acEvent.DesiredTargetID != 0)
    {
        auto targetView = m_world.view<FormIdComponent>();
        const auto targetEntityIt = std::find_if(std::begin(targetView), std::end(targetView), [id = acEvent.DesiredTargetID, targetView](entt::entity entity) { return targetView.get<FormIdComponent>(entity).Id == id; });

        if (targetEntityIt != std::end(targetView))
        {
            auto desiredTargetIdRes = Utils::GetServerId(*targetEntityIt);
            if (desiredTargetIdRes.has_value())
                request.DesiredTarget = desiredTargetIdRes.value();
            else
                spdlog::debug("{}: failed to find server id", __FUNCTION__);
        }
    }

    spdlog::debug("Spell cast event sent, ID: {:X}, Source: {}, IsDualCasting: {}, desired target: {:X}", request.CasterId, request.CastingSource, request.IsDualCasting, request.DesiredTarget);

    m_transport.Send(request);
}

void MagicService::OnNotifySpellCast(const NotifySpellCast& acMessage) const noexcept
{
    using CS = MagicSystem::CastingSource;

    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.CasterId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Caster with remote id {:X} not found.", acMessage.CasterId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);
    TESForm* pForm = TESForm::GetById(formIdComponent.Id);
    Actor* pActor = Cast<Actor>(pForm);

    pActor->GenerateMagicCasters();

    // Only left hand casters need dual casting (?)
    pActor->casters[CS::LEFT_HAND]->SetDualCasting(acMessage.IsDualCasting);

    if (acMessage.CastingSource >= 4)
    {
        spdlog::warn("{}: could not find casting source {}", __FUNCTION__, acMessage.CastingSource);
        return;
    }

    MagicItem* pSpell = nullptr;

    pSpell = pActor->magicItems[acMessage.CastingSource];

    if (!pSpell)
    {
        const uint32_t cSpellFormId = World::Get().GetModSystem().GetGameId(acMessage.SpellFormId);
        if (cSpellFormId == 0)
        {
            spdlog::error("Could not find spell form id for GameId base {:X}, mod {:X}", acMessage.SpellFormId.BaseId, acMessage.SpellFormId.ModId);
            return;
        }

        TESForm* pSpellForm = TESForm::GetById(cSpellFormId);
        if (!pSpellForm)
        {
            spdlog::error("Cannot find spell form, id: {:X}.", cSpellFormId);
            return;
        }

        pSpell = Cast<MagicItem>(pSpellForm);
    }

    if (!pSpell)
    {
        spdlog::error("Could not find spell.");
        return;
    }

    TESObjectREFR* pDesiredTarget = nullptr;

    if (acMessage.DesiredTarget != 0)
    {
        auto view = m_world.view<FormIdComponent>();
        for (auto entity : view)
        {
            std::optional<uint32_t> serverIdRes = Utils::GetServerId(entity);
            if (!serverIdRes.has_value())
            {
                spdlog::debug("{}: failed to find server id", __FUNCTION__);
                continue;
            }

            uint32_t serverId = serverIdRes.value();

            if (serverId == acMessage.DesiredTarget)
            {
                const auto& formIdComponent = view.get<FormIdComponent>(entity);
                pDesiredTarget = Cast<TESObjectREFR>(TESForm::GetById(formIdComponent.Id));
            }
        }
    }

    ScopedSpellCastOverride _;

    MagicCaster* pCaster = pActor->GetMagicCaster(static_cast<CS>(acMessage.CastingSource));
    if (!pCaster)
    {
        spdlog::warn("{}: failed to find caster.", __FUNCTION__);
        return;
    }

    pCaster->CastSpellImmediate(pSpell, false, pDesiredTarget, 1.0f, false, 0.0f);

    spdlog::debug("Successfully casted remote spell");
}

void MagicService::OnInterruptCastEvent(const InterruptCastEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
        return;

    uint32_t formId = acEvent.CasterFormID;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto casterEntityIt = std::find_if(std::begin(view), std::end(view), [formId, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (casterEntityIt == std::end(view))
    {
        spdlog::debug("{}: could not find caster, form id {:X}", __FUNCTION__, formId);
        return;
    }

    auto& localComponent = view.get<LocalComponent>(*casterEntityIt);

    HandleHealingHandsInterrupt(static_cast<MagicSystem::CastingSource>(acEvent.CastingSource));

    InterruptCastRequest request;
    request.CasterId = localComponent.Id;
    request.CastingSource = acEvent.CastingSource;

    spdlog::debug("Sending out interrupt cast");

    m_transport.Send(request);
}

void MagicService::OnNotifyInterruptCast(const NotifyInterruptCast& acMessage) const noexcept
{
    if (acMessage.CastingSource >= 4)
    {
        spdlog::warn("{}: could not find casting source {}", __FUNCTION__, acMessage.CastingSource);
        return;
    }

    auto remoteView = m_world.view<RemoteComponent, FormIdComponent>();
    const auto remoteIt = std::find_if(std::begin(remoteView), std::end(remoteView), [remoteView, Id = acMessage.CasterId](auto entity) { return remoteView.get<RemoteComponent>(entity).Id == Id; });

    if (remoteIt == std::end(remoteView))
    {
        spdlog::warn("Caster with remote id {:X} not found.", acMessage.CasterId);
        return;
    }

    auto formIdComponent = remoteView.get<FormIdComponent>(*remoteIt);

    const TESForm* pForm = TESForm::GetById(formIdComponent.Id);
    Actor* pActor = Cast<Actor>(pForm);

    pActor->GenerateMagicCasters();

    MagicCaster* pCaster = pActor->GetMagicCaster(static_cast<MagicSystem::CastingSource>(acMessage.CastingSource));
    if (!pCaster)
    {
        spdlog::warn("{}: failed to find caster.", __FUNCTION__);
        return;
    }

    pCaster->InterruptCast();

    spdlog::debug("Interrupt remote cast successful");
}

void MagicService::OnAddTargetEvent(const AddTargetEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
        return;

    // These effects are applied through spell cast sync
    if (SpellItem* pSpellItem = Cast<SpellItem>(TESForm::GetById(acEvent.SpellID)))
    {
        if ((pSpellItem->eCastingType == MagicSystem::CastingType::CONCENTRATION && !pSpellItem->IsHealingSpell()) || pSpellItem->IsWardSpell() || pSpellItem->IsInvisibilitySpell() || pSpellItem->IsBoundWeaponSpell())
        {
            return;
        }
    }

    AddTargetRequest request{};

    if (!m_world.GetModSystem().GetServerModId(acEvent.SpellID, request.SpellId.ModId, request.SpellId.BaseId))
    {
        spdlog::error("{}: could not find server ID for spell with formID {:X}, discarding", __FUNCTION__, acEvent.SpellID);
        return;
    }

    if (!m_world.GetModSystem().GetServerModId(acEvent.EffectID, request.EffectId.ModId, request.EffectId.BaseId))
    {
        spdlog::error("{}: could not find server ID for effect with formID {:X}, discarding", __FUNCTION__, acEvent.EffectID);
        return;
    }

    request.Magnitude = acEvent.Magnitude;

    // Because it takes time to create Actors, the Caster or Target may not
    // exist on the server yet, or server may not have told us yet. Have to queue to compensate.
    auto view = m_world.view<FormIdComponent>();
    const auto it = std::find_if(std::begin(view), std::end(view), [id = acEvent.TargetID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

    if (it == std::end(view))
    {
        MagicQueue::Spdlog("{}: server entity for target formID not found, formID: {:X}, queueing", __FUNCTION__, acEvent.TargetID);
        m_queuedEffects.push(MagicAddTargetEventQueue(acEvent));
        return;
    }

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*it);
    if (!serverIdRes.has_value())
    {
        MagicQueue::Spdlog("{}: server ID for target formID not found, formID: {:X}, queueing", __FUNCTION__, acEvent.TargetID);
        m_queuedEffects.push(MagicAddTargetEventQueue(acEvent));
        return;
    }

    request.TargetId = serverIdRes.value();

    if (acEvent.CasterID)
    {
        const auto casterIt = std::find_if(std::begin(view), std::end(view), [id = acEvent.CasterID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

        if (casterIt == std::end(view))
        {
            MagicQueue::Spdlog("{}: server entity for caster formID not found, formID: {:X}, queueing", __FUNCTION__, acEvent.CasterID);
            m_queuedEffects.push(MagicAddTargetEventQueue(acEvent));
            return;
        }

        serverIdRes = Utils::GetServerId(*casterIt);
        if (!serverIdRes.has_value())
        {
            MagicQueue::Spdlog("{}: server ID for caster formID not found, formID: {:X}, queueing", __FUNCTION__, acEvent.CasterID);
            m_queuedEffects.push(MagicAddTargetEventQueue(acEvent));
            return;
        }

        request.CasterId = serverIdRes.value();
    }

    request.IsDualCasting = acEvent.IsDualCasting;
    request.ApplyHealPerkBonus = acEvent.ApplyHealPerkBonus;
    request.ApplyStaminaPerkBonus = acEvent.ApplyStaminaPerkBonus;

    m_transport.Send(request);

    spdlog::debug("Sending effect sync request");
}

void MagicService::OnNotifyAddTarget(const NotifyAddTarget& acMessage) noexcept
{
    const uint32_t cSpellId = World::Get().GetModSystem().GetGameId(acMessage.SpellId);
    if (cSpellId == 0)
    {
        spdlog::error("{}: failed to retrieve formID of server spell id, GameId base: {:X}, mod: {:X}, discarding", __FUNCTION__, acMessage.SpellId.BaseId, acMessage.SpellId.ModId);
        return;
    }

    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(cSpellId));
    if (!pSpell)
    {
        spdlog::error("{}: failed to retrieve spell by formID {:X}, discarding", __FUNCTION__, cSpellId);
        return;
    }

    const uint32_t cEffectId = World::Get().GetModSystem().GetGameId(acMessage.EffectId);
    if (cEffectId == 0)
    {
        spdlog::error("{}: failed to retrieve formID of server effect id, GameId base: {:X}, mod: {:X}, discarding", __FUNCTION__, acMessage.EffectId.BaseId, acMessage.EffectId.ModId);
        return;
    }

    EffectItem* pEffect = pSpell->GetEffect(cEffectId);
    if (!pEffect)
    {
        spdlog::error("{}: failed to retrieve effect by formID {:X}", __FUNCTION__, cEffectId);
        return;
    }

    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.TargetId);
    if (!pActor)
    {
        MagicQueue::Spdlog("{}: could not find targeted Actor for serverID {:X}, queueing", __FUNCTION__, acMessage.TargetId);
        m_queuedRemoteEffects.push(acMessage);
        return;
    }

    Actor* pCaster{};
    acMessage.CasterId && (pCaster = Utils::GetByServerId<Actor>(acMessage.CasterId));
    if (acMessage.CasterId && !pCaster)
    {
        MagicQueue::Spdlog("{}: could not find caster Actor for serverID {:X}, queueing", __FUNCTION__, acMessage.CasterId);
        m_queuedRemoteEffects.push(acMessage);
        return;
    }

    ScopedSpellCastOverride spellOverrideGuard;

    MagicTarget::AddTargetData data{};
    data.pCaster = pCaster;
    data.pSpell = pSpell;
    data.pEffectItem = pEffect;
    data.fMagnitude = acMessage.Magnitude;
    data.fUnkFloat1 = 1.0f;
    data.eCastingSource = MagicSystem::CastingSource::CASTING_SOURCE_COUNT;
    data.bDualCast = acMessage.IsDualCasting;

    if (pEffect->IsWerewolfEffect())
        pActor->GetExtension()->GraphDescriptorHash = AnimationGraphDescriptor_WerewolfBehavior::m_key;

    if (pEffect->IsVampireLordEffect())
        pActor->GetExtension()->GraphDescriptorHash = AnimationGraphDescriptor_VampireLordBehavior::m_key;

    // This hack is here because slow time seems to be twice as slow when cast by an npc
    if (pEffect->IsSlowEffect())
        pActor = PlayerCharacter::Get();

    pActor->magicTarget.AddTarget(data, acMessage.ApplyHealPerkBonus, acMessage.ApplyStaminaPerkBonus);
    spdlog::debug("Applied remote magic effect");
}

void MagicService::OnRemoveSpellEvent(const RemoveSpellEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
        return;

    RemoveSpellRequest request{};

    if (!m_world.GetModSystem().GetServerModId(acEvent.SpellId, request.SpellId.ModId, request.SpellId.BaseId))
    {
        spdlog::error("{}: Could not find spell with form {:X}", __FUNCTION__, acEvent.SpellId);
        return;
    }

    auto view = m_world.view<FormIdComponent>();
    const auto it = std::find_if(std::begin(view), std::end(view), [id = acEvent.TargetId, view](auto entity) {
        return view.get<FormIdComponent>(entity).Id == id;
    });

    if (it == std::end(view))
    {
        spdlog::warn("Form id not found for magic remove target, form id: {:X}", acEvent.TargetId);
        return;
    }

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*it);
    if (!serverIdRes.has_value())
    {
        spdlog::warn("Server id not found for magic remove target, form id: {:X}", acEvent.TargetId);
        return;
    }

    request.TargetId = serverIdRes.value();

    //spdlog::info(__FUNCTION__ ": requesting remove spell with base id {:X} from actor with server id {:X}", request.SpellId.BaseId, request.TargetId);

    m_transport.Send(request);
}

void MagicService::OnNotifyRemoveSpell(const NotifyRemoveSpell& acMessage) noexcept
{
    uint32_t targetFormId = acMessage.TargetId;

    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.TargetId);
    if (!pActor)
    {
        spdlog::warn(__FUNCTION__ ": could not find actor server id {:X}", acMessage.TargetId);
        return;
    }

    const uint32_t cSpellId = World::Get().GetModSystem().GetGameId(acMessage.SpellId);
    if (cSpellId == 0)
    {
        spdlog::error("{}: failed to retrieve spell id, GameId base: {:X}, mod: {:X}", __FUNCTION__,
                      acMessage.SpellId.BaseId, acMessage.SpellId.ModId);
        return;
    }

    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById(cSpellId));
    if (!pSpell)
    {
        spdlog::error("{}: Failed to retrieve spell by id {:X}", __FUNCTION__, cSpellId);
        return;
    }

    // Remove the spell from the actor
    //spdlog::info(__FUNCTION__ ": removing spell with form id {:X} from actor with form id {:X}", cSpellId, targetFormId);
    pActor->RemoveSpell(pSpell);
}

void MagicService::ApplyQueuedEffects() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 100ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    // Search queued events
    while (!m_queuedEffects.empty())
    {
        AddTargetEvent target = m_queuedEffects.front().Target();
        Actor* pCaster = Cast<Actor>(TESForm::GetById(target.CasterID));
        Actor* pTarget = Cast<Actor>(TESForm::GetById(target.TargetID));
        auto pTargetName = !pTarget ? "" : pTarget->baseForm->GetName();
        auto pCasterName = !pCaster ? "" : pCaster->baseForm->GetName();

        // Check for and skip expired (timed out) events, that Actor isn't likely to exist anymore.
        if (m_queuedEffects.front().Expired())
            MagicQueue::Spdlog("{}: removing expired AddTargetEvent from queue: caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                               __FUNCTION__, pCasterName, target.CasterID, target.SpellID, target.EffectID, pTargetName, target.TargetID);
        else
        {
            // Process queued target events. If caster and target now have server IDs, this should work.
            // If they don't, stop processing the list until next iteration.
            auto view = m_world.view<FormIdComponent>();
            const auto it = std::find_if(std::begin(view), std::end(view), [id = target.TargetID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

            if (it == std::end(view))
            {
                spdlog::debug("{}: server entity for AddTargetEvent target formID still not found: caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                              __FUNCTION__, pCasterName, target.CasterID, target.SpellID, target.EffectID, pTargetName, target.TargetID);
                break;
            }

            entt::entity entity = *it;
            std::optional<uint32_t> serverIdRes = Utils::GetServerId(entity);
            if (!serverIdRes.has_value())
            {
                spdlog::debug("{}: serverID for AddTargetEvent target formID still not found: caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                              __FUNCTION__, pCasterName, target.CasterID, target.SpellID, target.EffectID, pTargetName, target.TargetID);
                break;
            }

            if (target.CasterID)
            {
                const auto casterIt = std::find_if(std::begin(view), std::end(view), [id = target.CasterID, view](auto entity) { return view.get<FormIdComponent>(entity).Id == id; });

                if (casterIt == std::end(view))
                {
                    spdlog::debug("{}: serverID for AddTargetEvent caster formID still not found: caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                                  __FUNCTION__, pCasterName, target.CasterID, target.SpellID, target.EffectID, pTargetName, target.TargetID);
                    break;
                }

                serverIdRes = Utils::GetServerId(*casterIt);
                if (!serverIdRes.has_value())
                {
                    spdlog::debug("{}: serverID for AddTargetEvent caster formID still not found: caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                                  __FUNCTION__, pCasterName, target.CasterID, target.SpellID, target.EffectID, pTargetName, target.TargetID);
                    break;
                }
            }

            // At this point, it will succeed or fail, but not queue another one ad infinitum
            MagicQueue::Spdlog("{}: retrying AddTargetEvent for caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                               __FUNCTION__, pCasterName, target.CasterID, target.SpellID, target.EffectID, pTargetName, target.TargetID);
            OnAddTargetEvent(target);
        }
        m_queuedEffects.pop();
    }

    // Same again for remote events
    while (!m_queuedRemoteEffects.empty())
    {
        NotifyAddTarget target = m_queuedRemoteEffects.front().Target();
        Actor* pTarget = Utils::GetByServerId<Actor>(target.TargetId);
        Actor* pCaster = Utils::GetByServerId<Actor>(target.CasterId);
        auto pTargetName = !pTarget ? "" : pTarget->baseForm->GetName();
        auto pCasterName = !pCaster ? "" : pCaster->baseForm->GetName();

        if (m_queuedRemoteEffects.front().Expired())
            MagicQueue::Spdlog("{}: removing expired NotifyAddTarget event from queue: caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                               __FUNCTION__, pCasterName, target.CasterId, target.SpellId, target.EffectId, pTargetName, target.TargetId);
        else
        {
            if (!pTarget)
            {
                spdlog::debug("{}: Actor for target serverID still not found for NotifyAddTarget: caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                              __FUNCTION__, pCasterName, target.CasterId, target.SpellId, target.EffectId, pTargetName, target.TargetId);
                break;
            }

            if (target.CasterId && !pCaster)
            {
                spdlog::debug("{}: Actor for caster serverID still not found for NotifyAddTarget: caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                              __FUNCTION__, pCasterName, target.CasterId, target.SpellId, target.EffectId, pTargetName, target.TargetId);
                break;
            }

            MagicQueue::Spdlog("{}: retrying NotifyAddTarget for caster {}({:X}), spell {:X}, effect {:X}, target {}({:X})",
                               __FUNCTION__, pCasterName, target.CasterId, target.SpellId, target.EffectId, pTargetName,target.TargetId);
            OnNotifyAddTarget(target);
        }
        m_queuedRemoteEffects.pop();
    }
}

void MagicService::StartRevealingOtherPlayers() noexcept
{
    UpdateRevealOtherPlayersEffect(/*forceTrigger=*/true);
}

void MagicService::UpdateRevealOtherPlayersEffect(bool aForceTrigger) noexcept
{
    constexpr auto cRevealDuration = 10s;
    constexpr auto cDelayBetweenUpdates = 2s;

    // Effect's activation and lifecycle

    static std::chrono::steady_clock::time_point revealStartTimePoint;
    static std::chrono::steady_clock::time_point lastSendTimePoint;

    const bool shouldActivate = aForceTrigger || GetAsyncKeyState(VK_F4) & 0x01;

    if (shouldActivate && !m_revealingOtherPlayers)
    {
        m_revealingOtherPlayers = true;
        revealStartTimePoint = std::chrono::steady_clock::now();
    }

    if (!m_revealingOtherPlayers)
        return;

    const auto now = std::chrono::steady_clock::now();

    if (now - revealStartTimePoint > cRevealDuration)
    {
        m_revealingOtherPlayers = false;
        return;
    }

    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    // When active

    Mod* pSkyrimTogether = ModManager::Get()->GetByName("SkyrimTogether.esp");
    if (!pSkyrimTogether)
        return;

    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById((pSkyrimTogether->standardId << 24) | 0x1825));

    if (!pSpell)
        return;

    MagicTarget::AddTargetData data{};
    data.pSpell = pSpell;
    data.pEffectItem = pSpell->GetEffect((pSkyrimTogether->standardId << 24) | 0x1824);
    data.fMagnitude = 1.f;
    data.fUnkFloat1 = 1.f;
    data.eCastingSource = MagicSystem::CastingSource::CASTING_SOURCE_COUNT;

    auto view = World::Get().view<FormIdComponent, PlayerComponent>();
    for (const auto entity : view)
    {
        auto& formIdComponent = view.get<FormIdComponent>(entity);
        if (formIdComponent.Id == 0x14)
            continue;

        auto* pRemotePlayer = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pRemotePlayer)
            continue;

        pRemotePlayer->magicTarget.AddTarget(data, false, false);
    }
}

// Handler for NotifyPartyMemberDowned: updates local state, posts a party chat message, and drives glow logic
void MagicService::OnNotifyPartyMemberDowned(const NotifyPartyMemberDowned& acMessage) noexcept
{
    // Track downed set
    if (acMessage.ServerId != 0)
    {
        if (acMessage.IsDowned)
        {
            DownedMemberInfo info{};
            info.PlayerId = acMessage.PlayerId;
            info.PositionX = acMessage.PositionX;
            info.PositionY = acMessage.PositionY;
            info.PositionZ = acMessage.PositionZ;
            m_downedPartyMembers[acMessage.ServerId] = info;
        }
        else
        {
            m_downedPartyMembers.erase(acMessage.ServerId);
        }
    }

    // Resolve a nicer display name for the player if we know it, otherwise fall back to the ID.
    std::string playerName;
    const auto& players = m_world.GetPartyService().GetPlayers();
    if (auto it = players.find(acMessage.PlayerId); it != players.end())
        playerName = it->second.Name.c_str();
    else
        playerName = "Player " + std::to_string(acMessage.PlayerId);

    // Build a simple party message (no explicit "Party:" prefix, just colored/typed chat on UI side).
    std::string text = acMessage.IsDowned
        ? playerName + " has died! You can revive them using Healing Hands."
        : playerName + " has been revived.";

    // Push to overlay as a system line so it doesn't look like player chat
    if (auto pOverlay = m_world.GetOverlayService().GetOverlayApp())
    {
        auto pArguments = CefListValue::Create();
        pArguments->SetInt(0, static_cast<int>(kSystemMessage));  // message type
        pArguments->SetString(1, text);                      // message text
        pArguments->SetString(2, "");                        // sender label (system)
        pOverlay->ExecuteAsync("message", pArguments);
    }
}

// Periodically re-apply reveal effect to downed party members only
void MagicService::UpdateRevealDownedPlayersEffect() noexcept
{
    using namespace std::chrono_literals;

    if (m_downedPartyMembers.empty())
        return;

    static std::chrono::steady_clock::time_point s_lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 2s;

    const auto now = std::chrono::steady_clock::now();
    if (now - s_lastSendTimePoint < cDelayBetweenUpdates)
        return;

    s_lastSendTimePoint = now;

    Mod* pSkyrimTogether = ModManager::Get()->GetByName("SkyrimTogether.esp");
    if (!pSkyrimTogether)
        return;

    MagicItem* pSpell = Cast<MagicItem>(TESForm::GetById((pSkyrimTogether->standardId << 24) | 0x1825));
    if (!pSpell)
        return;

    MagicTarget::AddTargetData data{};
    data.pSpell = pSpell;
    data.pEffectItem = pSpell->GetEffect((pSkyrimTogether->standardId << 24) | 0x1824);
    data.fMagnitude = 1.f;
    data.fUnkFloat1 = 1.f;
    data.eCastingSource = MagicSystem::CastingSource::CASTING_SOURCE_COUNT;

    // Match Reveal Players targeting: all remote players, then filter by downed server id
    auto view = m_world.view<FormIdComponent, PlayerComponent>();
    for (const auto entity : view)
    {
        const auto& formIdComponent = view.get<FormIdComponent>(entity);

        // Never glow the local player
        if (formIdComponent.Id == 0x14)
            continue;

        // Resolve server id for this actor; skip if we can't
        auto serverIdOpt = Utils::GetServerId(entity);
        if (!serverIdOpt.has_value())
            continue;

        const auto serverId = serverIdOpt.value();

        // Only apply to players currently marked as downed
        if (m_downedPartyMembers.find(serverId) == m_downedPartyMembers.end())
            continue;

        if (auto* pRemotePlayer = Cast<Actor>(TESForm::GetById(formIdComponent.Id)))
            pRemotePlayer->magicTarget.AddTarget(data, false, false);
    }
}

void MagicService::UpdateReviveChannels(double aDeltaSeconds) noexcept
{
    if (!m_victimReviveState)
        return;

    PlayerCharacter* pLocalPlayer = PlayerCharacter::Get();
    if (!pLocalPlayer || !pLocalPlayer->actorState.IsBleedingOut())
    {
        StopVictimReviveUi();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - m_victimReviveState->LastPingAt > cReviveChannelTimeout)
    {
        StopVictimReviveUi();
        return;
    }

    auto& state = *m_victimReviveState;
    state.AccumulatedSeconds = std::min(state.RequiredSeconds, state.AccumulatedSeconds + static_cast<float>(aDeltaSeconds));

    UpdateVictimReviveUi(state);

    if (state.AccumulatedSeconds >= state.RequiredSeconds)
    {
        World::Get().ctx().at<PlayerService>().OnHealRevive();
        StopVictimReviveUi();
    }
}

void MagicService::UpdateHealerChannel(double aDeltaSeconds) noexcept
{
    if (!m_healerChannelState.Active)
        return;

    const auto now = std::chrono::steady_clock::now();

    if (!HasDownedPartyMemberInRange(cHealingHandsRange) || now - m_healerChannelState.LastUpdate > cReviveChannelTimeout)
    {
        StopHealerUi();
        return;
    }

    m_healerChannelState.AccumulatedSeconds = std::min(
        m_healerChannelState.RequiredSeconds,
        m_healerChannelState.AccumulatedSeconds + static_cast<float>(aDeltaSeconds));

    UpdateHealerUi();

    if (m_healerChannelState.AccumulatedSeconds >= m_healerChannelState.RequiredSeconds)
        StopHealerUi();
}

float MagicService::GetRequiredReviveDuration(float aRestorationLevel) const noexcept
{
    constexpr float cMinLevel = 20.f;
    constexpr float cMaxLevel = 100.f;
    constexpr float cMinSeconds = 5.f;
    constexpr float cMaxSeconds = 15.f;

    if (aRestorationLevel <= cMinLevel)
        return cMaxSeconds;
    if (aRestorationLevel >= cMaxLevel)
        return cMinSeconds;

    const float normalized = (aRestorationLevel - cMinLevel) / (cMaxLevel - cMinLevel);
    return cMaxSeconds - normalized * (cMaxSeconds - cMinSeconds);
}

void MagicService::UpdateVictimReviveUi(const ReviveChannelState& aState) const noexcept
{
    if (auto* pOverlay = m_world.GetOverlayService().GetOverlayApp())
    {
        auto pArgs = CefListValue::Create();
        pArgs->SetDouble(0, aState.AccumulatedSeconds);
        pArgs->SetDouble(1, aState.RequiredSeconds);
        pArgs->SetString(2, aState.HealerName);
        pOverlay->ExecuteAsync("updateReviveVictimProgress", pArgs);
    }
}

void MagicService::StopVictimReviveUi() noexcept
{
    if (!m_victimReviveState)
        return;

    if (auto* pOverlay = m_world.GetOverlayService().GetOverlayApp())
    {
        auto pArgs = CefListValue::Create();
        pOverlay->ExecuteAsync("stopReviveVictimProgress", pArgs);
    }

    m_victimReviveState.reset();
}

void MagicService::UpdateHealerUi() const noexcept
{
    if (!m_healerChannelState.Active)
        return;

    if (auto* pOverlay = m_world.GetOverlayService().GetOverlayApp())
    {
        auto pArgs = CefListValue::Create();
        pArgs->SetDouble(0, m_healerChannelState.AccumulatedSeconds);
        pArgs->SetDouble(1, m_healerChannelState.RequiredSeconds);
        pOverlay->ExecuteAsync("updateReviveHealerProgress", pArgs);
    }
}

void MagicService::StopHealerUi() noexcept
{
    if (!m_healerChannelState.Active)
        return;

    if (auto* pOverlay = m_world.GetOverlayService().GetOverlayApp())
    {
        auto pArgs = CefListValue::Create();
        pOverlay->ExecuteAsync("stopReviveHealerProgress", pArgs);
    }

    m_healerChannelState.Active = false;
    m_healerChannelState.AccumulatedSeconds = 0.f;
    m_healerChannelState.RequiredSeconds = 0.f;
    m_healerChannelState.LastUpdate = {};
}

Actor* MagicService::FindActorByServerId(uint32_t aServerId) const noexcept
{
    auto localView = m_world.view<FormIdComponent, LocalComponent>();
    for (auto entity : localView)
    {
        const auto& localComponent = localView.get<LocalComponent>(entity);
        if (localComponent.Id != aServerId)
            continue;

        const auto& formIdComponent = localView.get<FormIdComponent>(entity);
        return Cast<Actor>(TESForm::GetById(formIdComponent.Id));
    }

    auto remoteView = m_world.view<FormIdComponent, RemoteComponent>();
    for (auto entity : remoteView)
    {
        const auto& remoteComponent = remoteView.get<RemoteComponent>(entity);
        if (remoteComponent.Id != aServerId)
            continue;

        const auto& formIdComponent = remoteView.get<FormIdComponent>(entity);
        return Cast<Actor>(TESForm::GetById(formIdComponent.Id));
    }

    return nullptr;
}

bool MagicService::HasDownedPartyMemberInRange(float aRange) noexcept
{
    if (m_downedPartyMembers.empty())
    {
        // Fall back to live actor state if we missed a downed notification.
        const auto& partyMembers = m_world.GetPartyService().GetPartyMembers();
        if (partyMembers.empty())
            return false;

        PlayerCharacter* pLocalPlayer = PlayerCharacter::Get();
        if (!pLocalPlayer)
            return false;

        const float rangeSquared = aRange * aRange;
        auto remoteView = m_world.view<FormIdComponent, PlayerComponent, RemoteComponent>();
        for (auto entity : remoteView)
        {
            const auto& playerComponent = remoteView.get<PlayerComponent>(entity);
            if (std::find(partyMembers.begin(), partyMembers.end(), playerComponent.Id) == partyMembers.end())
                continue;

            const auto& formIdComponent = remoteView.get<FormIdComponent>(entity);
            Actor* pRemotePlayer = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (!pRemotePlayer || !pRemotePlayer->actorState.IsBleedingOut())
                continue;

            const float dx = pLocalPlayer->position.x - pRemotePlayer->position.x;
            const float dy = pLocalPlayer->position.y - pRemotePlayer->position.y;
            const float dz = pLocalPlayer->position.z - pRemotePlayer->position.z;
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            if (distanceSquared > rangeSquared)
                continue;

            DownedMemberInfo info{};
            info.PlayerId = playerComponent.Id;
            info.PositionX = pRemotePlayer->position.x;
            info.PositionY = pRemotePlayer->position.y;
            info.PositionZ = pRemotePlayer->position.z;
            m_downedPartyMembers[remoteView.get<RemoteComponent>(entity).Id] = info;
            return true;
        }

        return false;
    }

    PlayerCharacter* pLocalPlayer = PlayerCharacter::Get();
    if (!pLocalPlayer)
        return false;

    const float rangeSquared = aRange * aRange;
    const auto localServerId = GetLocalServerId();

    for (auto& [serverId, info] : m_downedPartyMembers)
    {
        if (localServerId && serverId == *localServerId)
            continue;

        if (Actor* pActor = FindActorByServerId(serverId))
        {
            info.PositionX = pActor->position.x;
            info.PositionY = pActor->position.y;
            info.PositionZ = pActor->position.z;
        }

        const float dx = pLocalPlayer->position.x - info.PositionX;
        const float dy = pLocalPlayer->position.y - info.PositionY;
        const float dz = pLocalPlayer->position.z - info.PositionZ;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;

        if (distanceSquared <= rangeSquared)
            return true;
    }

    // Re-check live actors in case the cached downed positions are stale.
    const auto& partyMembers = m_world.GetPartyService().GetPartyMembers();
    if (!partyMembers.empty())
    {
        auto remoteView = m_world.view<FormIdComponent, PlayerComponent, RemoteComponent>();
        for (auto entity : remoteView)
        {
            const auto& playerComponent = remoteView.get<PlayerComponent>(entity);
            if (std::find(partyMembers.begin(), partyMembers.end(), playerComponent.Id) == partyMembers.end())
                continue;

            const auto& formIdComponent = remoteView.get<FormIdComponent>(entity);
            Actor* pRemotePlayer = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
            if (!pRemotePlayer || !pRemotePlayer->actorState.IsBleedingOut())
                continue;

            const float dx = pLocalPlayer->position.x - pRemotePlayer->position.x;
            const float dy = pLocalPlayer->position.y - pRemotePlayer->position.y;
            const float dz = pLocalPlayer->position.z - pRemotePlayer->position.z;
            const float distanceSquared = dx * dx + dy * dy + dz * dz;
            if (distanceSquared > rangeSquared)
                continue;

            DownedMemberInfo info{};
            info.PlayerId = playerComponent.Id;
            info.PositionX = pRemotePlayer->position.x;
            info.PositionY = pRemotePlayer->position.y;
            info.PositionZ = pRemotePlayer->position.z;
            m_downedPartyMembers[remoteView.get<RemoteComponent>(entity).Id] = info;
            return true;
        }
    }

    return false;
}

std::string MagicService::ResolvePlayerName(uint32_t aServerId) const
{
    const auto resolveByPlayerId = [&](uint32_t playerId) -> std::string
    {
        const auto& players = m_world.GetPartyService().GetPlayers();
        if (auto it = players.find(playerId); it != players.end())
            return it->second.Name.c_str();
        return {};
    };

    auto localView = m_world.view<PlayerComponent, LocalComponent>();
    for (auto entity : localView)
    {
        const auto& localComponent = localView.get<LocalComponent>(entity);
        if (localComponent.Id == aServerId)
            return resolveByPlayerId(localView.get<PlayerComponent>(entity).Id);
    }

    auto remoteView = m_world.view<PlayerComponent, RemoteComponent>();
    for (auto entity : remoteView)
    {
        const auto& remoteComponent = remoteView.get<RemoteComponent>(entity);
        if (remoteComponent.Id == aServerId)
            return resolveByPlayerId(remoteView.get<PlayerComponent>(entity).Id);
    }

    return {};
}

std::optional<uint32_t> MagicService::GetLocalServerId() const noexcept
{
    auto view = m_world.view<LocalComponent>();
    for (auto entity : view)
        return view.get<LocalComponent>(entity).Id;

    return std::nullopt;
}

void MagicService::UpdateHealingHandsBroadcast(double aDeltaSeconds) noexcept
{
    if (!m_isLocalHealingHandsActive || m_activeHealingHandsSpellId == 0)
        return;

    m_healingHandsPingAccumulator -= aDeltaSeconds;
    if (m_healingHandsPingAccumulator > 0.0)
        return;

    if (!SendHealingProximityPing(m_activeHealingHandsSpellId))
    {
        spdlog::warn("UpdateHealingHandsBroadcast: failed to send healing ping, aborting local channel");
        ResetLocalHealingHandsState();
        return;
    }

    m_healingHandsPingAccumulator = cHealingHandsPingInterval;
}

bool MagicService::SendHealingProximityPing(uint32_t aSpellFormId) noexcept
{
    PlayerCharacter* pCaster = PlayerCharacter::Get();
    if (!pCaster)
        return false;

    auto view = m_world.view<FormIdComponent, LocalComponent>();
    const auto casterIt = std::find_if(std::begin(view), std::end(view),
        [formId = pCaster->formID, view](entt::entity entity) {
            return view.get<FormIdComponent>(entity).Id == formId;
        });

    if (casterIt == std::end(view))
        return false;

    auto& localComponent = view.get<LocalComponent>(*casterIt);

    HealingProximityRequest healRequest{};
    healRequest.CasterId = localComponent.Id;
    healRequest.CasterX = pCaster->position.x;
    healRequest.CasterY = pCaster->position.y;
    healRequest.CasterZ = pCaster->position.z;

    const float restorationLevel = pCaster->GetActorValue(ActorValueInfo::kRestoration);
    healRequest.CasterRestorationLevel = static_cast<uint16_t>(std::clamp(restorationLevel, 0.f, 1000.f));

    if (!m_world.GetModSystem().GetServerModId(aSpellFormId, healRequest.SpellFormId))
    {
        spdlog::error("SendHealingProximityPing: server spell id not found for spell {:X}", aSpellFormId);
        return false;
    }

    spdlog::debug("SendHealingProximityPing: spell {:X} at ({:.1f}, {:.1f}, {:.1f})",
                  aSpellFormId, healRequest.CasterX, healRequest.CasterY, healRequest.CasterZ);
    m_transport.Send(healRequest);
    return true;
}

void MagicService::ResetLocalHealingHandsState() noexcept
{
    StopHealerUi();
    m_isLocalHealingHandsActive = false;
    m_activeHealingHandsSpellId = 0;
    m_healingHandsPingAccumulator = 0.0;
    m_localHealingHandsSources.fill(false);
}

void MagicService::HandleHealingHandsInterrupt(MagicSystem::CastingSource aSource) noexcept
{
    if (aSource < 0 || aSource >= MagicSystem::CastingSource::CASTING_SOURCE_COUNT)
        return;

    if (!m_localHealingHandsSources[aSource])
        return;

    m_localHealingHandsSources[aSource] = false;

    const bool anyActive = std::any_of(
        m_localHealingHandsSources.begin(),
        m_localHealingHandsSources.end(),
        [](bool active) { return active; });

    if (!anyActive)
        ResetLocalHealingHandsState();
}

bool MagicService::IsHealingHandsSpell(uint32_t aSpellFormId, const SpellItem* apSpell) const noexcept
{
    if ((aSpellFormId & cFormIdMask) == cHealingHandsBaseId)
        return true;

    if (!apSpell)
        return false;

    if (!apSpell->IsHealingSpell())
        return false;

    if (apSpell->eCastingType != MagicSystem::CastingType::CONCENTRATION)
        return false;

    if (apSpell->eDelivery == MagicSystem::Delivery::SELF)
        return false;

    for (EffectItem* pEffect : apSpell->listOfEffects)
    {
        if (!pEffect || !pEffect->pEffectSetting)
            continue;

        const auto delivery = static_cast<MagicSystem::Delivery>(pEffect->pEffectSetting->deliveryType);
        if (delivery == MagicSystem::Delivery::AIMED || delivery == MagicSystem::Delivery::TARGET_ACTOR || delivery == MagicSystem::Delivery::TOUCH)
            return true;
    }

    return false;
}

void MagicService::OnNotifyHealingProximity(const NotifyHealingProximity& acMessage) noexcept
{
    PlayerCharacter* pLocalPlayer = PlayerCharacter::Get();
    if (!pLocalPlayer)
        return;

    if (!IsHealingHandsSpell(acMessage.SpellFormId.BaseId))
        return;

    const auto now = std::chrono::steady_clock::now();
    const auto localServerId = GetLocalServerId();
    const bool isCaster = localServerId.has_value() && acMessage.CasterId == localServerId.value();

    if (pLocalPlayer->actorState.IsBleedingOut())
    {
        const float distance = std::sqrt(
            std::pow(pLocalPlayer->position.x - acMessage.CasterX, 2.0f) +
            std::pow(pLocalPlayer->position.y - acMessage.CasterY, 2.0f) +
            std::pow(pLocalPlayer->position.z - acMessage.CasterZ, 2.0f));

        if (distance <= cHealingHandsRange)
        {
            const float requiredSeconds = GetRequiredReviveDuration(static_cast<float>(acMessage.CasterRestorationLevel));

            if (!m_victimReviveState || m_victimReviveState->CasterServerId != acMessage.CasterId)
            {
                m_victimReviveState = ReviveChannelState{};
                m_victimReviveState->CasterServerId = acMessage.CasterId;
                m_victimReviveState->AccumulatedSeconds = 0.f;
                m_victimReviveState->HealerName = ResolvePlayerName(acMessage.CasterId);
            }

            auto& state = *m_victimReviveState;
            state.RequiredSeconds = requiredSeconds;
            state.LastPingAt = now;

            if (state.HealerName.empty())
                state.HealerName = ResolvePlayerName(acMessage.CasterId);

            UpdateVictimReviveUi(state);
        }
        else
        {
            StopVictimReviveUi();
        }
    }

    if (isCaster)
    {
        if (HasDownedPartyMemberInRange(cHealingHandsRange))
        {
            if (!m_healerChannelState.Active)
            {
                m_healerChannelState.AccumulatedSeconds = 0.f;
                m_healerChannelState.Active = true;
            }

            m_healerChannelState.RequiredSeconds = GetRequiredReviveDuration(static_cast<float>(acMessage.CasterRestorationLevel));
            m_healerChannelState.LastUpdate = now;
            UpdateHealerUi();
        }
        else
        {
            StopHealerUi();
        }
    }
}
