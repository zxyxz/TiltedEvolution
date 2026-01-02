#include <Services/InventoryService.h>

#include <cstdlib>
#include <TiltedCore/Stl.hpp>

#include <Messages/RequestObjectInventoryChanges.h>
#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Services/SyncModeService.h>

#include <Events/UpdateEvent.h>
#include <Events/InventoryChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>

#include <Components.h>
#include <World.h>
#include <Games/Skyrim/Interface/UI.h>
#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Actor.h>
#include <Structs/ObjectData.h>
#include <Forms/TESWorldSpace.h>
#include <Games/TES.h>
#include <Games/Overrides.h>
#include <EquipManager.h>
#include <Games/ActorExtension.h>
#include <Forms/TESNPC.h>
#include <DefaultObjectManager.h>
#include <Games/Primitives.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <TESObjectREFR.h>

InventoryService::InventoryService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&InventoryService::OnUpdate>(this);
    m_inventoryConnection = m_dispatcher.sink<InventoryChangeEvent>().connect<&InventoryService::OnInventoryChangeEvent>(this);
    m_equipmentConnection = m_dispatcher.sink<EquipmentChangeEvent>().connect<&InventoryService::OnEquipmentChangeEvent>(this);
    m_inventoryChangeConnection = m_dispatcher.sink<NotifyInventoryChanges>().connect<&InventoryService::OnNotifyInventoryChanges>(this);
    m_equipmentChangeConnection = m_dispatcher.sink<NotifyEquipmentChanges>().connect<&InventoryService::OnNotifyEquipmentChanges>(this);
}

void InventoryService::OnUpdate(const UpdateEvent& acUpdateEvent) noexcept
{
    ProcessPendingInventoryChanges();
    ProcessPendingEquipment();
    ProcessPendingEquipmentChanges();
    ProcessPendingEquipmentRequests();
    RunWeaponStateUpdates();
    RunNakedNPCBugChecks();
}

void InventoryService::OnInventoryChangeEvent(const InventoryChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    // Ignore inventory sync while in ghost-only mode (quest isolation).
    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
        return;

    auto view = m_world.view<FormIdComponent>();

    const auto iter = std::find_if(std::begin(view), std::end(view), [view, formId = acEvent.FormId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (iter == std::end(view))
        return;

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*iter);
    if (!serverIdRes.has_value())
    {
        spdlog::debug("{}: failed to find server id, target form id: {:X}, item id: {:X}, count: {}", __FUNCTION__, acEvent.FormId, acEvent.Item.BaseId.BaseId, acEvent.Item.Count);
        return;
    }

    RequestInventoryChanges request;
    request.ServerId = serverIdRes.value();
    request.Item = acEvent.Item;
    request.UpdateClients = acEvent.UpdateClients;

    m_transport.Send(request);

    spdlog::info("Sending item request, item: {:X}, count: {}, target object: {:X}", acEvent.Item.BaseId.BaseId, acEvent.Item.Count, acEvent.FormId);
}

void InventoryService::OnEquipmentChangeEvent(const EquipmentChangeEvent& acEvent) noexcept
{
    m_pendingEquipmentRequests.push_back(acEvent);
}

void InventoryService::OnNotifyInventoryChanges(const NotifyInventoryChanges& acMessage) noexcept
{
    if (TryApplyInventoryChange(acMessage))
        return;

    m_pendingInventoryChanges.push_back(acMessage);
    spdlog::debug("{}: queued inventory change for server id {:X}", __FUNCTION__, acMessage.ServerId);
}

void InventoryService::OnNotifyEquipmentChanges(const NotifyEquipmentChanges& acMessage) noexcept
{
    if (TryApplyEquipmentChange(acMessage))
        return;

    m_pendingEquipmentChanges.push_back(acMessage);
    spdlog::debug("{}: queued equipment change for server id {:X}", __FUNCTION__, acMessage.ServerId);
}

void InventoryService::ApplyEquipmentChange(Actor* pActor, const NotifyEquipmentChanges& acMessage) noexcept
{
    auto& modSystem = World::Get().GetModSystem();

    uint32_t itemId = modSystem.GetGameId(acMessage.ItemId);
    TESForm* pItem = TESForm::GetById(itemId);

    if (!pItem)
    {
        spdlog::error("Could not find inventory item {:X}:{:X}", acMessage.ItemId.ModId, acMessage.ItemId.BaseId);
        return;
    }

    uint32_t equipSlotId = modSystem.GetGameId(acMessage.EquipSlotId);
    TESForm* pEquipSlot = TESForm::GetById(equipSlotId);

    uint32_t slotId = 0;
    if (pEquipSlot == DefaultObjectManager::Get().rightEquipSlot)
        slotId = 1;

    auto* pEquipManager = EquipManager::Get();

    if (acMessage.IsSpell)
    {
        if (acMessage.Unequip)
            pEquipManager->UnEquipSpell(pActor, pItem, slotId);
        else
            pEquipManager->EquipSpell(pActor, pItem, slotId);

        return;
    }
    else if (acMessage.IsShout)
    {
        if (acMessage.Unequip)
            pEquipManager->UnEquipShout(pActor, pItem);
        else
            pEquipManager->EquipShout(pActor, pItem);

        return;
    }

    auto* pObject = Cast<TESBoundObject>(pItem);

    // TODO: ExtraData necessary? probably
    const int32_t count = acMessage.Count == 0 ? 1 : acMessage.Count;

    // Quest isolation: remote player ghosts may not have full inventory state.
    // Ensure the item exists locally before attempting to equip, purely for visuals.
    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost && !acMessage.Unequip && !acMessage.IsSpell && !acMessage.IsShout)
    {
        auto* pExt = pActor->GetExtension();
        if (pExt && pExt->IsRemotePlayer() && !pActor->IsItemInInventory(itemId))
        {
            Inventory::Entry add{};
            add.BaseId = acMessage.ItemId;
            add.Count = count;

            ScopedInventoryOverride sio;
            pActor->AddOrRemoveItem(add, true);
        }
    }

    if (acMessage.Unequip)
    {
        pEquipManager->UnEquip(pActor, pItem, nullptr, count, pEquipSlot, false, true, false, false, nullptr);
    }
    else
    {
        // Unequip all armor first, since the game won't auto unequip armor
        Inventory wornArmor{};
        if (pItem->formType == FormType::Armor)
        {
            wornArmor = pActor->GetWornArmor();
            for (const auto& armor : wornArmor.Entries)
            {
                uint32_t armorId = modSystem.GetGameId(armor.BaseId);
                TESForm* pArmor = TESForm::GetById(armorId);
                if (pArmor)
                    pEquipManager->UnEquip(pActor, pArmor, nullptr, 1, pEquipSlot, false, true, false, false, nullptr);
            }
        }

        pEquipManager->Equip(pActor, pItem, nullptr, count, pEquipSlot, false, true, false, false);

        for (const auto& armor : wornArmor.Entries)
        {
            uint32_t armorId = modSystem.GetGameId(armor.BaseId);
            TESForm* pArmor = TESForm::GetById(armorId);
            if (pArmor)
                pEquipManager->Equip(pActor, pArmor, nullptr, 1, pEquipSlot, false, true, false, false);
        }
    }
}

void InventoryService::ProcessPendingEquipment() noexcept
{
    auto view = m_world.view<FormIdComponent, PendingEquipmentComponent>();
    TiltedPhoques::Vector<entt::entity> toClear;

    for (auto entity : view)
    {
        auto& formIdComponent = view.get<FormIdComponent>(entity);
        auto& pending = view.get<PendingEquipmentComponent>(entity);

        Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        const auto readiness = EvaluateActorReadiness(pActor);
        if (readiness != ActorReadinessStatus::Ready)
        {
            if (readiness == ActorReadinessStatus::MissingActor)
                toClear.push_back(entity);
            continue;
        }

        for (const auto& change : pending.PendingChanges)
            ApplyEquipmentChange(pActor, change);

        toClear.push_back(entity);
    }

    for (auto entity : toClear)
        m_world.remove<PendingEquipmentComponent>(entity);
}

void InventoryService::ProcessPendingInventoryChanges() noexcept
{
    if (m_pendingInventoryChanges.empty())
        return;

    TiltedPhoques::Vector<NotifyInventoryChanges> remaining;
    remaining.reserve(m_pendingInventoryChanges.size());

    for (const auto& change : m_pendingInventoryChanges)
    {
        if (!TryApplyInventoryChange(change))
            remaining.push_back(change);
    }

    m_pendingInventoryChanges = std::move(remaining);
}

void InventoryService::ProcessPendingEquipmentChanges() noexcept
{
    if (m_pendingEquipmentChanges.empty())
        return;

    TiltedPhoques::Vector<NotifyEquipmentChanges> remaining;
    remaining.reserve(m_pendingEquipmentChanges.size());

    for (const auto& change : m_pendingEquipmentChanges)
    {
        if (!TryApplyEquipmentChange(change))
            remaining.push_back(change);
    }

    m_pendingEquipmentChanges = std::move(remaining);
}

void InventoryService::ProcessPendingEquipmentRequests() noexcept
{
    if (m_pendingEquipmentRequests.empty())
        return;

    TiltedPhoques::Vector<EquipmentChangeEvent> remaining;
    remaining.reserve(m_pendingEquipmentRequests.size());

    for (const auto& request : m_pendingEquipmentRequests)
    {
        if (!SendEquipmentChange(request))
            remaining.push_back(request);
    }

    m_pendingEquipmentRequests = std::move(remaining);
}

bool InventoryService::SendEquipmentChange(const EquipmentChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return false;

    // Quest isolation: allow equipment sync only for the local player (so others see your ghost correctly).
    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
    {
        // Player reference form is always 0x14; keep isolation strict and avoid relying on extension flags being initialized.
        if (acEvent.ActorId != 0x14)
            return true; // Drop anything else to avoid retries/log spam.
    }

    auto view = m_world.view<FormIdComponent>();

    const auto iter = std::find_if(std::begin(view), std::end(view), [view, formId = acEvent.ActorId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });

    if (iter == std::end(view))
    {
        spdlog::debug("{}: form id {:X} not found yet, postponing equipment sync", __FUNCTION__, acEvent.ActorId);
        return false;
    }

    std::optional<uint32_t> serverIdRes = Utils::GetServerId(*iter);
    if (!serverIdRes.has_value())
        return false;

    Actor* pActor = Cast<Actor>(TESForm::GetById(acEvent.ActorId));
    const auto readiness = EvaluateActorReadiness(pActor);
    if (readiness != ActorReadinessStatus::Ready)
    {
        spdlog::debug("{}: actor {:X} not ready (waiting for {}), postponing equipment sync", __FUNCTION__, acEvent.ActorId, DescribeReadiness(readiness));
        return false;
    }

    auto& modSystem = World::Get().GetModSystem();

    RequestEquipmentChanges request;
    request.ServerId = serverIdRes.value();

    if (!modSystem.GetServerModId(acEvent.EquipSlotId, request.EquipSlotId))
        return true;
    if (!modSystem.GetServerModId(acEvent.ItemId, request.ItemId))
        return true;

    const int32_t cEffectiveCount = acEvent.Count == 0 ? 1 : acEvent.Count;
    request.Count = cEffectiveCount;
    request.Unequip = acEvent.Unequip;
    request.IsSpell = acEvent.IsSpell;
    request.IsShout = acEvent.IsShout;
    request.IsAmmo = acEvent.IsAmmo;
    request.CurrentInventory = pActor->GetEquipment();

    m_transport.Send(request);

    spdlog::info("Sending equipment request, item: {:X}, count: {}, target object: {:X}", acEvent.ItemId, cEffectiveCount, acEvent.ActorId);

    return true;
}

bool InventoryService::TryApplyInventoryChange(const NotifyInventoryChanges& acMessage) noexcept
{
    TESObjectREFR* pObject = Utils::GetByServerId<TESObjectREFR>(acMessage.ServerId);
    if (!pObject)
        return false;

    if (acMessage.Silent)
    {
        ScopedInventoryOverride _;
        pObject->AddOrRemoveItem(acMessage.Item);
        return true;
    }

    Actor* pActor = Cast<Actor>(pObject);

    ScopedInventoryOverride _;

    if (pActor)
    {
        const auto readiness = EvaluateActorReadiness(pActor);
        if (readiness != ActorReadinessStatus::Ready)
        {
            spdlog::debug("{}: actor {:X} not ready (waiting for {}) while applying inventory delta", __FUNCTION__, pActor->formID, DescribeReadiness(readiness));
            return false;
        }

        pActor->AddOrRemoveItem(acMessage.Item);
        return true;
    }

    pObject->AddOrRemoveItem(acMessage.Item);
    return true;
}

bool InventoryService::TryApplyEquipmentChange(const NotifyEquipmentChanges& acMessage) noexcept
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    if (!pActor)
        return false;

    // Quest isolation: only apply equipment changes to remote player ghost actors.
    if (m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
    {
        auto remotePlayerView = m_world.view<RemoteComponent, PlayerComponent, FormIdComponent>();
        const auto it = std::find_if(remotePlayerView.begin(), remotePlayerView.end(),
            [remotePlayerView, serverId = acMessage.ServerId](entt::entity e) { return remotePlayerView.get<RemoteComponent>(e).Id == serverId; });

        if (it == remotePlayerView.end())
            return true;
    }

    const auto readiness = EvaluateActorReadiness(pActor);
    if (readiness != ActorReadinessStatus::Ready)
    {
        auto view = m_world.view<FormIdComponent>();
        const auto itor = std::find_if(std::begin(view), std::end(view), [formId = pActor->formID, view](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == formId; });

        if (itor != std::end(view))
        {
            auto* pPending = m_world.try_get<PendingEquipmentComponent>(*itor);
            if (!pPending)
                pPending = &m_world.emplace<PendingEquipmentComponent>(*itor);

            pPending->PendingChanges.push_back(acMessage);
            spdlog::debug("Queued equipment change for actor {:X} (waiting for {})", pActor->formID, DescribeReadiness(readiness));
            return true;
        }

        spdlog::warn("{}: could not queue equipment change, entity not found for form id {:X}", __FUNCTION__, pActor->formID);
        return false;
    }

    ApplyEquipmentChange(pActor, acMessage);
    return true;
}

InventoryService::ActorReadinessStatus InventoryService::EvaluateActorReadiness(Actor* pActor) const noexcept
{
    if (!pActor)
        return ActorReadinessStatus::MissingActor;

    if (!pActor->GetNiNode())
        return ActorReadinessStatus::Missing3D;

    if (ExtraContainerChanges::Data* pContainerChanges = pActor->GetContainerChanges();
        !pContainerChanges || !pContainerChanges->entries)
        return ActorReadinessStatus::MissingContainerData;

    return ActorReadinessStatus::Ready;
}

const char* InventoryService::DescribeReadiness(ActorReadinessStatus aStatus) noexcept
{
    switch (aStatus)
    {
    case ActorReadinessStatus::MissingActor:
        return "actor reference";
    case ActorReadinessStatus::Missing3D:
        return "3D data";
    case ActorReadinessStatus::MissingContainerData:
        return "inventory data";
    default:
        return "ready state";
    }
}

void InventoryService::RunWeaponStateUpdates() noexcept
{
    if (!m_transport.IsConnected())
        return;

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 500ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto view = m_world.view<FormIdComponent, LocalComponent>();

    for (auto entity : view)
    {
        const auto& formIdComponent = view.get<FormIdComponent>(entity);
        Actor* const pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        auto& localComponent = view.get<LocalComponent>(entity);

        bool isWeaponDrawn = pActor->actorState.IsWeaponDrawn();
        if (isWeaponDrawn != localComponent.IsWeaponDrawn)
        {
            localComponent.IsWeaponDrawn = isWeaponDrawn;

            DrawWeaponRequest request;
            request.Id = localComponent.Id;
            request.IsWeaponDrawn = isWeaponDrawn;

            m_transport.Send(request);
        }
    }
}

void InventoryService::RunNakedNPCBugChecks() noexcept
{
    if (!m_transport.IsConnected())
        return;

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto view = m_world.view<FormIdComponent>();

    for (auto entity : view)
    {
        const auto& formIdComponent = view.get<FormIdComponent>(entity);
        Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
        if (!pActor)
            continue;

        if (pActor->GetExtension()->IsPlayer())
            continue;

        if (pActor->IsDead())
            continue;

        if (pActor->IsWearingBodyPiece())
            continue;

        if (!pActor->ShouldWearBodyPiece())
            continue;

        // Don't broadcast changes, it'll just make things messier.
        // If all clients have this problem, they'll all fix it individually.
        ScopedEquipOverride seo;
        ScopedInventoryOverride sio;

        pActor->ResetInventory(false);
    }
}
