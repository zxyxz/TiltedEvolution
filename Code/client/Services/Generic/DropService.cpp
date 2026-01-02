#include "DropService.h"

#include <World.h>
#include <Components.h>
#include <Services/TransportService.h>
#include <Services/RunnerService.h>
#include <Events/UpdateEvent.h>
#include <Events/DropItemEvent.h>
#include <Events/PickupDroppedItemEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/GridCellChangeEvent.h>
#include <Games/Skyrim/Events/EventDispatcher.h>

#include <Messages/NotifyDroppedItems.h>
#include <Messages/RequestDroppedItems.h>

#include <Sync/DropManager.h>
#include <Sync/DropExecutionContext.h>

#include <Games/Overrides.h>
#include <Games/TES.h>
#include <Games/SaveGameUtils.h>

#include <Actor.h>
#include <Games/ActorExtension.h>
#include <PlayerCharacter.h>

#include <TESObjectREFR.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESForm.h>
#include <Forms/AlchemyItem.h>
#include <Forms/EnchantmentItem.h>
#include <NetImmerse/NiAVObject.h>

#include <Utils.h>
#include <ExtraData/ExtraContainerChanges.h>
#include <Games/Primitives.h>
#include <type_traits>
#include <cmath>

#include <cmath>

#include <spdlog/spdlog.h>
#include <TiltedCore/Stl.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <filesystem>

namespace
{
constexpr float kDropSearchRadiusSquared = 400.f * 400.f;
constexpr float kPickupRemovalRadiusSquared = 2500.f * 2500.f;
constexpr float kMaterializeGraceSeconds = 0.5f;
constexpr float kDropPhysicsHoldSeconds = 5.0f;
constexpr float kDropMoveSyncIntervalSeconds = 0.25f;
constexpr float kDropMoveInterpolationSeconds = 0.25f;
constexpr float kDropMovePredictionSeconds = 0.1f;
constexpr float kDropRotationNetScale = 1000.0f;
constexpr double kPeriodicPlayerCellSyncSeconds = 5.0;
constexpr double kDropSyncQueueIntervalSeconds = 0.5;
constexpr double kGrabEventSuppressSeconds = 2.0;
constexpr float kDropPhysicsDisableSuppressSeconds = 1.0f;
constexpr double kLoadSuspendSeconds = 3.0;
constexpr double kCellPhysicsGraceSeconds = 1.0;
constexpr uint32_t kMaxPendingDropRetries = 600;
constexpr uint32_t kMaxPendingPickupRetries = 120;
TiltedPhoques::Map<uint64_t, float> g_materializeGrace;

bool HasDropLocation(const NiPoint3& aLocation) noexcept
{
    return std::abs(aLocation.x) > std::numeric_limits<float>::epsilon() ||
        std::abs(aLocation.y) > std::numeric_limits<float>::epsilon() ||
        std::abs(aLocation.z) > std::numeric_limits<float>::epsilon();
}

bool IsServerItemFormType(FormType aType) noexcept
{
    switch (aType)
    {
    case FormType::Weapon:
    case FormType::Armor:
    case FormType::Ammo:
    case FormType::Ingredient:
    case FormType::Alchemy:
    case FormType::Book:
    case FormType::Scroll:
    case FormType::Note:
    case FormType::SoulGem:
    case FormType::KeyMaster:
    case FormType::Light:
    case FormType::Misc:
    case FormType::Apparatus:
    case FormType::LeveledItem:
        return true;
    default:
        return false;
    }
}

bool IsEligibleServerItemRef(TESObjectREFR* apReference) noexcept
{
    if (!apReference || !apReference->baseForm)
        return false;

    if (apReference->IsDisabled())
        return false;

    if (ExtraDataList* pExtra = apReference->GetExtraDataList(); pExtra && pExtra->HasQuestObjectAlias())
        return false;

    return IsServerItemFormType(apReference->baseForm->formType);
}

bool ShouldDeferPhysicsLock(double aRemainingGraceSeconds) noexcept
{
    return aRemainingGraceSeconds > 0.0;
}

enum class MotionType : uint8_t
{
    kInvalid = 0,
    kDynamic = 1,
    kSphereInertia = 2,
    kBoxInertia = 3,
    kKeyframed = 4,
    kFixed = 5,
    kThinBoxInertia = 6,
    kCharacter = 7
};

bool SetNodeMotionType(NiAVObject* apNode, MotionType aMotionType, bool aAllowActivate) noexcept
{
    if (!apNode)
        return false;

    using TSetMotionType = bool(NiAVObject*, uint8_t, bool, bool, bool);
    POINTER_SKYRIMSE(TSetMotionType, s_setMotionType, 77866);
    return TiltedPhoques::ThisCall(s_setMotionType.Get(), apNode, static_cast<uint8_t>(aMotionType), true, false, aAllowActivate);
}

bool SetReferenceMotionType(TESObjectREFR* apReference, MotionType aMotionType, bool aAllowActivate) noexcept
{
    if (!apReference)
        return false;

    NiAVObject* pNode = apReference->GetNiNode();
    const bool updated = SetNodeMotionType(pNode, aMotionType, aAllowActivate);
    apReference->Update3DPosition(false);
    return updated;
}

NiPoint3 ToPoint(const Vector3_NetQuantize& aVector)
{
    return NiPoint3(aVector);
}

NiPoint3 ToDropRotation(const Vector3_NetQuantize& aVector) noexcept
{
    NiPoint3 rotation{};
    rotation.x = aVector.x / kDropRotationNetScale;
    rotation.y = aVector.y / kDropRotationNetScale;
    rotation.z = aVector.z / kDropRotationNetScale;
    return rotation;
}

Vector3_NetQuantize ToNetVector(const NiPoint3& aVector)
{
    Vector3_NetQuantize value;
    value.x = aVector.x;
    value.y = aVector.y;
    value.z = aVector.z;
    return value;
}

Vector3_NetQuantize ToNetDropRotation(const NiPoint3& aRotation) noexcept
{
    Vector3_NetQuantize value;
    value.x = aRotation.x * kDropRotationNetScale;
    value.y = aRotation.y * kDropRotationNetScale;
    value.z = aRotation.z * kDropRotationNetScale;
    return value;
}

float NormalizeAngleDelta(float aDelta) noexcept
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    aDelta = std::fmod(aDelta, kTwoPi);
    if (aDelta > kPi)
        aDelta -= kTwoPi;
    else if (aDelta < -kPi)
        aDelta += kTwoPi;
    return aDelta;
}

TESBoundObject* ResolveDroppedObject(const Inventory::Entry& acEntry)
{
    auto& modSystem = World::Get().GetModSystem();
    const uint32_t objectId = modSystem.GetGameId(acEntry.BaseId);
    return Cast<TESBoundObject>(TESForm::GetById(objectId));
}

uint16_t ClampExtraCount(int32_t aCount) noexcept
{
    int32_t count = aCount;
    if (count < 0)
        count = -count;
    if (count <= 0)
        count = 1;

    constexpr int32_t kMaxExtraCount = std::numeric_limits<int16_t>::max();
    if (count > kMaxExtraCount)
        count = kMaxExtraCount;

    return static_cast<uint16_t>(count);
}

void ApplyDropExtraData(TESObjectREFR* apReference, const Inventory::Entry& acItem, uint16_t aCount) noexcept
{
    if (!apReference)
        return;

    ExtraDataList* pExtraDataList = apReference->GetExtraDataList();
    if (!pExtraDataList)
        return;

    pExtraDataList->SetCount(aCount);

    if (acItem.ExtraCharge > 0.f)
        pExtraDataList->SetChargeData(acItem.ExtraCharge);

    if (acItem.ExtraEnchantId != 0)
    {
        auto& modSystem = World::Get().GetModSystem();

        EnchantmentItem* pEnchantment = nullptr;
        if (acItem.ExtraEnchantId.ModId == 0xFFFFFFFF)
        {
            pEnchantment = EnchantmentItem::Create(acItem.EnchantData);
        }
        else
        {
            const uint32_t enchantId = modSystem.GetGameId(acItem.ExtraEnchantId);
            pEnchantment = Cast<EnchantmentItem>(TESForm::GetById(enchantId));
        }

        TP_ASSERT(pEnchantment, "No Enchantment created or found.");
        if (pEnchantment)
            pExtraDataList->SetEnchantmentData(pEnchantment, acItem.ExtraEnchantCharge, acItem.ExtraEnchantRemoveUnequip);
    }

    if (acItem.ExtraPoisonId != 0)
    {
        auto& modSystem = World::Get().GetModSystem();
        const uint32_t poisonId = modSystem.GetGameId(acItem.ExtraPoisonId);
        if (AlchemyItem* pPoison = Cast<AlchemyItem>(TESForm::GetById(poisonId)))
            pExtraDataList->SetPoison(pPoison, acItem.ExtraPoisonCount);
    }

    if (acItem.ExtraHealth > 0.f)
        pExtraDataList->SetHealth(acItem.ExtraHealth);

    if (acItem.ExtraSoulLevel > 0 && acItem.ExtraSoulLevel <= 5)
        pExtraDataList->SetSoulData(static_cast<SOUL_LEVEL>(acItem.ExtraSoulLevel));

    if (acItem.ExtraWorn)
        pExtraDataList->SetWorn(false);
    if (acItem.ExtraWornLeft)
        pExtraDataList->SetWorn(true);
}

TESObjectREFR* FindReferenceNear(TESBoundObject* apObject, const NiPoint3& acCenter, float aRadiusSq = kDropSearchRadiusSquared)
{
    if (!apObject)
        return nullptr;

    TES* pTes = TES::Get();
    if (!pTes || !pTes->cells || !pTes->cells->arr)
        return nullptr;

    const int dimension = pTes->cells->dimension;
    if (dimension <= 0)
        return nullptr;

    TESObjectREFR* pClosest = nullptr;
    float closestDistanceSq = aRadiusSq;

    const int cellCount = dimension * dimension;
    for (int i = 0; i < cellCount; ++i)
    {
        TESObjectCELL* pCell = pTes->cells->arr[i];
        if (!pCell || !pCell->IsValid())
            continue;

        auto* pReferences = pCell->refData.refArray;
        if (!pReferences)
            continue;

        const uint32_t referenceCount = pCell->refData.Count();
        for (uint32_t j = 0; j < referenceCount; ++j)
        {
            TESObjectREFR* pCandidate = pReferences[j].Get();
            if (!pCandidate || pCandidate->baseForm != apObject || pCandidate->formType == Actor::Type)
                continue;

            const float diffX = pCandidate->position.x - acCenter.x;
            const float diffY = pCandidate->position.y - acCenter.y;
            const float diffZ = pCandidate->position.z - acCenter.z;
            const float distanceSq = diffX * diffX + diffY * diffY + diffZ * diffZ;

            if (distanceSq < closestDistanceSq)
            {
                closestDistanceSq = distanceSq;
                pClosest = pCandidate;
            }
        }
    }

    return pClosest;
}

std::pair<GameId, GameId> ResolveCellMetadata(World& aWorld, Actor* apActor)
{
    GameId cell{};
    GameId world{};

    if (!apActor)
        return {cell, world};

    TESObjectCELL* pCell = apActor->parentCell ? apActor->parentCell : apActor->GetParentCell();
    if (pCell)
    {
        aWorld.GetModSystem().GetServerModId(pCell->formID, cell);
        if (pCell->worldspace)
            aWorld.GetModSystem().GetServerModId(pCell->worldspace->formID, world);
    }

    return {cell, world};
}
} // namespace

DropService::DropService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
    , m_coSaveService(aWorld.ctx().at<CoSaveService>())
    , m_dropStorage(m_coSaveService.GetDropStorage())
{
    m_dropEventConnection = m_dispatcher.sink<DropItemEvent>().connect<&DropService::OnDropEvent>(this);
    m_pickupEventConnection = m_dispatcher.sink<PickupDroppedItemEvent>().connect<&DropService::OnPickupEvent>(this);
    m_notifyDropConnection = m_dispatcher.sink<NotifyActorDrop>().connect<&DropService::OnNotifyDrop>(this);
    m_notifyPickupConnection = m_dispatcher.sink<NotifyDroppedItemPickedUp>().connect<&DropService::OnNotifyPickup>(this);
    m_notifyDroppedItemsConnection = m_dispatcher.sink<NotifyDroppedItems>().connect<&DropService::OnNotifyDroppedItems>(this);
    m_notifyDropMoveConnection = m_dispatcher.sink<NotifyDroppedItemMove>().connect<&DropService::OnNotifyDropMove>(this);
    m_notifyDropPhysicsDisabledConnection = m_dispatcher.sink<NotifyDroppedItemPhysicsDisabled>().connect<&DropService::OnNotifyDropPhysicsDisabled>(this);
    m_connectedEventConnection = m_dispatcher.sink<ConnectedEvent>().connect<&DropService::OnConnected>(this);
    m_cellChangeConnection = m_dispatcher.sink<CellChangeEvent>().connect<&DropService::OnCellChange>(this);
    m_gridCellChangeConnection = m_dispatcher.sink<GridCellChangeEvent>().connect<&DropService::OnGridCellChange>(this);
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&DropService::OnUpdate>(this);

    DropManager::SetStorageListener(&m_dropStorage);

    if (auto* pDispatcher = EventDispatcherManager::Get())
    {
        pDispatcher->grabReleaseEvent.RegisterSink(this);
        pDispatcher->loadGameEvent.RegisterSink(this);
    }
}

DropService::~DropService()
{
    DropManager::SetStorageListener(nullptr);

    if (auto* pDispatcher = EventDispatcherManager::Get())
    {
        pDispatcher->grabReleaseEvent.UnRegisterSink(this);
        pDispatcher->loadGameEvent.UnRegisterSink(this);
    }
}


BSTEventResult DropService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    spdlog::info("DropService: LoadGame event received");
    m_pendingActions.clear();
    m_pendingDropSyncs.clear();
    m_dropSyncQueue.clear();
    m_dropSyncQueuedCells.clear();
    m_materializingDrops.clear();
    m_localDrops.clear();
    m_knownSpawnEpochs.clear();
    m_grabbedDrops.clear();
    m_dropPhysicsCooldowns.clear();
    m_dropMoveSyncTimers.clear();
    m_dropPhysicsDisableSuppressions.clear();
    m_grabbedReferences.clear();
    m_referencePhysicsCooldowns.clear();
    m_referenceMoveSyncTimers.clear();
    m_pendingCreationEngineRemovals.clear();
    m_dropSyncWorldSpace = {};
    m_dropSyncQueueAccumulator = 0.0;
    m_periodicPlayerCellSyncAccumulator = 0.0;
    m_nextDropSyncRequestId = 1;
    m_grabEventSuppressionRemaining = kGrabEventSuppressSeconds;
    m_cachedUsername.clear();
    m_suspendProcessing = true;
    m_requestResyncAfterSuspend = true;
    m_suspendProcessingAccumulator = 0.0;
    m_pendingDiscoveryResyncs = 0;
    m_cellPhysicsGraceRemaining = kCellPhysicsGraceSeconds;
    m_requestPhysicsLockAfterGrace = true;

    return BSTEventResult::kOk;
}

void DropService::OnDropEvent(const DropItemEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    auto serverIdRes = ResolveServerId(acEvent.ActorFormId);
    if (!serverIdRes)
    {
        spdlog::warn("{}: failed to resolve server id for actor {:X}", __FUNCTION__, acEvent.ActorFormId);
        return;
    }

    RequestActorDrop request{};
    request.ServerId = *serverIdRes;
    request.ActorFormId = acEvent.ActorFormId;
    request.Item = acEvent.Item;
    request.ClientDropId = acEvent.ClientDropId;
    request.HasLocation = true;
    request.Location = ToNetVector(acEvent.Location);
    request.HasRotation = true;
    request.Rotation = ToNetDropRotation(acEvent.Rotation);
    request.CellId = acEvent.CellId;
    request.WorldSpaceId = acEvent.WorldSpaceId;
    request.ReferenceId = acEvent.ReferenceId;

    spdlog::debug("DropService: requesting drop for actor {:X}, server {:X}, item {:X}:{:X}", acEvent.ActorFormId, *serverIdRes, acEvent.Item.BaseId.ModId, acEvent.Item.BaseId.BaseId);
    m_transport.Send(request);

    // Client drop payload is captured in the request; mapping cache is server-authoritative.
    DropManager::ConsumeLocalDrop(acEvent.ClientDropId);
}

void DropService::OnPickupEvent(const PickupDroppedItemEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    uint64_t resolvedDropId = 0;
    if (acEvent.ReferenceFormId)
    {
        EnsureStorageReady();
        if (const auto mapped = m_dropStorage.FindDropIdByRefFormId(acEvent.ReferenceFormId, acEvent.CellId, acEvent.WorldSpaceId); mapped)
        {
            resolvedDropId = *mapped;
            spdlog::debug("DropService: resolved pickup ref {:X} to drop {} via cache", acEvent.ReferenceFormId, resolvedDropId);
        }
    }
    if (resolvedDropId == 0)
        resolvedDropId = acEvent.DropId;

    spdlog::debug("DropService: local pickup event actor {:X} drop {}", acEvent.ActorFormId, resolvedDropId);

    auto serverIdRes = ResolveServerId(acEvent.ActorFormId);
    if (!serverIdRes)
    {
        spdlog::warn("{}: failed to resolve server id for actor {:X}", __FUNCTION__, acEvent.ActorFormId);
        return;
    }

    RequestPickupDroppedItem request{};
    request.ServerId = *serverIdRes;
    request.DropId = resolvedDropId;
    if (resolvedDropId)
    {
        if (const auto dropOpt = DropManager::GetServerDrop(resolvedDropId); dropOpt)
        {
            request.Item = dropOpt->Item;
            request.HasLocation = true;
            request.Location = ToNetVector(dropOpt->Location);
            request.HasRotation = true;
            request.Rotation = ToNetDropRotation(dropOpt->Rotation);
            request.CellId = dropOpt->CellId;
            request.WorldSpaceId = dropOpt->WorldSpaceId;
            request.ReferenceId = dropOpt->ReferenceId ? dropOpt->ReferenceId : acEvent.ReferenceId;
        }
        else if (acEvent.HasItemData)
        {
            request.Item = acEvent.Item;
            request.HasLocation = acEvent.HasLocation;
            if (acEvent.HasLocation)
                request.Location = ToNetVector(acEvent.Location);
            request.HasRotation = acEvent.HasRotation;
            if (acEvent.HasRotation)
                request.Rotation = ToNetDropRotation(acEvent.Rotation);
            request.CellId = acEvent.CellId;
            request.WorldSpaceId = acEvent.WorldSpaceId;
            request.ReferenceId = acEvent.ReferenceId;
        }
    }
    else
    {
        if (acEvent.HasItemData)
            request.Item = acEvent.Item;

        request.HasLocation = acEvent.HasLocation;
        if (acEvent.HasLocation)
            request.Location = ToNetVector(acEvent.Location);

        request.HasRotation = acEvent.HasRotation;
        if (acEvent.HasRotation)
            request.Rotation = ToNetDropRotation(acEvent.Rotation);

        request.CellId = acEvent.CellId;
        request.WorldSpaceId = acEvent.WorldSpaceId;
        request.ReferenceId = acEvent.ReferenceId;
    }

    spdlog::debug("DropService: requesting pickup for actor {:X} (server {:X}), drop {}", acEvent.ActorFormId, *serverIdRes, resolvedDropId);
    m_transport.Send(request);
}

void DropService::OnNotifyDrop(const NotifyActorDrop& acMessage) noexcept
{
    if (m_suspendProcessing)
    {
        m_requestResyncAfterSuspend = true;
        return;
    }

    if (!ApplyDrop(acMessage))
    {
        PendingAction pending{};
        pending.Type = PendingType::Drop;
        pending.DropMessage = acMessage;
        m_pendingActions.push_back(std::move(pending));
        spdlog::debug("DropService: queued drop {} for later", acMessage.DropId);
    }
    else
    {
        spdlog::debug("DropService: processed drop {} immediately", acMessage.DropId);
    }
}

bool DropService::ApplyDrop(const NotifyActorDrop& acMessage) noexcept
{
    if (const auto itEpoch = m_knownSpawnEpochs.find(acMessage.DropId);
        itEpoch != std::end(m_knownSpawnEpochs) && acMessage.SpawnEpoch < itEpoch->second)
    {
        spdlog::debug("DropService: ignoring stale drop {} epoch {} (known {})", acMessage.DropId, acMessage.SpawnEpoch, itEpoch->second);
        return true;
    }

    if (const auto itEpoch = m_knownSpawnEpochs.find(acMessage.DropId);
        itEpoch == std::end(m_knownSpawnEpochs) || acMessage.SpawnEpoch > itEpoch->second)
    {
        m_knownSpawnEpochs[acMessage.DropId] = acMessage.SpawnEpoch;
    }

    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    DropManager::ServerDropData serverData{};
    serverData.ServerId = acMessage.ServerId;
    serverData.ActorFormId = acMessage.ActorFormId ? acMessage.ActorFormId : (pActor ? pActor->formID : 0);
    serverData.Type = ServerItemType::Dropped;
    serverData.CellId = acMessage.CellId;
    serverData.WorldSpaceId = acMessage.WorldSpaceId;
    serverData.ReferenceId = acMessage.ReferenceId;
    serverData.Item = acMessage.Item;

    auto [fallbackCellId, fallbackWorldId] = ResolveCellMetadata(m_world, pActor ? pActor : static_cast<Actor*>(pPlayer));
    if (!serverData.CellId)
        serverData.CellId = fallbackCellId;
    if (!serverData.WorldSpaceId)
        serverData.WorldSpaceId = fallbackWorldId;

    if (acMessage.HasLocation)
        serverData.Location = ToPoint(acMessage.Location);
    else if (pActor)
        serverData.Location = pActor->position;
    else if (pPlayer)
        serverData.Location = pPlayer->position;

    if (acMessage.HasRotation)
        serverData.Rotation = ToDropRotation(acMessage.Rotation);
    else if (pActor)
        serverData.Rotation = pActor->rotation;
    else if (pPlayer)
        serverData.Rotation = pPlayer->rotation;

    DropManager::TrackServerDrop(acMessage.DropId, serverData);

    if (const auto handleOpt = DropManager::GetHandleForDrop(acMessage.DropId); handleOpt)
    {
        if (TESObjectREFR::GetByHandle(*handleOpt))
        {
            m_localDrops.insert(acMessage.DropId);
            spdlog::debug("DropService: drop {} already materialized, skipping spawn", acMessage.DropId);
            return true;
        }

        DropManager::ClearHandleBinding(acMessage.DropId);
    }

    if (TryBindExistingReference(acMessage.DropId, serverData))
    {
        spdlog::debug("DropService: bound existing reference for drop {}, skipping spawn", acMessage.DropId);
        return true;
    }

    if (!IsDropCellLoaded(serverData.CellId, serverData.WorldSpaceId))
    {
        spdlog::debug("DropService: drop {} not in loaded cells (cell {:X}:{:X}), skipping spawn", acMessage.DropId, serverData.CellId.ModId, serverData.CellId.BaseId);
        return true;
    }

    if (!MaterializeDrop(acMessage.DropId, serverData, true))
        return false;

    m_localDrops.insert(acMessage.DropId);
    m_grabbedDrops.erase(acMessage.DropId);
    m_dropPhysicsCooldowns.erase(acMessage.DropId);
    m_dropMoveSyncTimers.erase(acMessage.DropId);
    spdlog::debug("DropService: applied drop {} for actor {:X}", acMessage.DropId, serverData.ActorFormId);
    return true;
}

bool CanTouchReference3D(TESObjectREFR* apReference) noexcept
{
    if (!apReference)
        return false;
    if (!apReference->GetNiNode())
        return false;
    if (!apReference->GetParentCellEx() && !apReference->GetParentCell())
        return false;
    return true;
}

void SafeSetReferenceMotionType(TESObjectREFR* apReference, MotionType aMotionType, bool aAllowActivate) noexcept
{
    if (!apReference || !apReference->GetNiNode())
        return;
    SetReferenceMotionType(apReference, aMotionType, aAllowActivate);
}

void SafeUpdateReference3D(TESObjectREFR* apReference, bool aWarp) noexcept
{
    if (!CanTouchReference3D(apReference))
        return;
    apReference->Update3DPosition(aWarp);
}

void DropService::OnNotifyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept
{
    if (m_suspendProcessing)
    {
        m_requestResyncAfterSuspend = true;
        return;
    }

    spdlog::debug("DropService: received pickup notify drop {} from actor {:X}", acMessage.DropId, acMessage.ServerId);
    if (!ApplyPickup(acMessage))
    {
        PendingAction pending{};
        pending.Type = PendingType::Pickup;
        pending.PickupMessage = acMessage;
        m_pendingActions.push_back(std::move(pending));
        spdlog::debug("DropService: queued pickup {} for later", acMessage.DropId);
    }
    else
    {
        spdlog::debug("DropService: processed pickup {} immediately", acMessage.DropId);
    }
}

bool DropService::ApplyPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept
{
    if (acMessage.DropId == 0)
        return HandleUntrackedPickup(acMessage);

    EnsureStorageReady();

    const uint64_t dropId = acMessage.DropId;
    const uint32_t localPlayerId = m_transport.GetLocalPlayerId();
    const bool isLocalPicker = localPlayerId != 0 && acMessage.ServerId == localPlayerId;

    if (!IsPickupRelevant(acMessage) && !isLocalPicker)
    {
        DropManager::RemoveServerDrop(dropId);
        ForgetLocalDrop(dropId);
        spdlog::debug("DropService: ignored pickup {} (out of range)", dropId);
        return true;
    }

    bool removed = false;

    if (const auto handleOpt = DropManager::GetHandleForDrop(dropId); handleOpt)
    {
        if (TESObjectREFR* pDroppedRef = TESObjectREFR::GetByHandle(*handleOpt))
        {
            if (pDroppedRef->IsTemporary())
                pDroppedRef->Delete();
            else
                pDroppedRef->Disable();
            removed = true;
        }
        else
        {
            DropManager::ClearHandleBinding(dropId);
        }
    }

    if (!removed)
    {
        if (const auto refFormId = m_dropStorage.GetRefFormId(dropId); refFormId)
        {
            if (TESForm* pForm = TESForm::GetById(*refFormId))
            {
                if (TESObjectREFR* pRef = Cast<TESObjectREFR>(pForm))
                {
                    if (pRef->IsTemporary())
                        pRef->Delete();
                    else
                        pRef->Disable();
                    removed = true;
                }
            }
        }
    }

    if (!removed && acMessage.HasLocation)
        removed = RemoveReferenceByLocation(acMessage.Item, acMessage.Location, "pickup notify location fallback", kPickupRemovalRadiusSquared);
    if (!removed)
        removed = RemoveNearbyReference(dropId, "pickup notify nearby fallback", kPickupRemovalRadiusSquared);

    DropManager::RemoveServerDrop(dropId);
    ForgetLocalDrop(dropId);

    if (isLocalPicker || removed)
        m_dropStorage.RemoveCachedDrop(dropId);

    spdlog::debug("DropService: applied pickup {} (removed={}, localPicker={})", dropId, removed, isLocalPicker);
    return true;
}

bool DropService::HandleUntrackedPickup(const NotifyDroppedItemPickedUp& acMessage) noexcept
{
    const GameId playerWorld = GetPlayerWorldId();

    if (acMessage.CellId && !IsDropCellLoaded(acMessage.CellId, acMessage.WorldSpaceId))
    {
        spdlog::debug("DropService: ignoring untracked pickup in non-loaded cell {:X}:{:X}", acMessage.CellId.ModId, acMessage.CellId.BaseId);
        return true;
    }

    if (acMessage.WorldSpaceId && playerWorld && acMessage.WorldSpaceId != playerWorld)
    {
        spdlog::debug("DropService: ignoring pickup in world {:X}:{:X}, player world {:X}:{:X}", acMessage.WorldSpaceId.ModId, acMessage.WorldSpaceId.BaseId, playerWorld.ModId, playerWorld.BaseId);
        return true;
    }

    bool removed = RemoveReferenceById(acMessage.ReferenceId, "untracked pickup via reference id");
    if (!removed && acMessage.HasLocation)
        removed = RemoveReferenceByLocation(acMessage.Item, acMessage.Location, "untracked pickup via location", kPickupRemovalRadiusSquared);
    if (!removed)
    {
        if (auto* pPlayer = PlayerCharacter::Get())
        {
            Vector3_NetQuantize approximate = ToNetVector(pPlayer->position);
            removed = RemoveReferenceByLocation(acMessage.Item, approximate, "untracked pickup player proximity", kPickupRemovalRadiusSquared);
        }
    }

    if (!removed)
        spdlog::warn("DropService: pickup notify without drop id could not find reference ({:X}:{:X})", acMessage.Item.BaseId.ModId, acMessage.Item.BaseId.BaseId);

    return true;
}

bool DropService::IsPickupRelevant(const NotifyDroppedItemPickedUp& acMessage) noexcept
{
    const GameId playerWorld = GetPlayerWorldId();

    if (acMessage.CellId)
        return IsDropCellLoaded(acMessage.CellId, acMessage.WorldSpaceId);

    if (acMessage.WorldSpaceId && playerWorld && acMessage.WorldSpaceId != playerWorld)
        return false;

    return true;
}

void DropService::OnNotifyDroppedItems(const NotifyDroppedItems& acMessage) noexcept
{
    if (m_suspendProcessing)
    {
        m_requestResyncAfterSuspend = true;
        return;
    }

    spdlog::debug("DropService: received {} drops and {} creation-engine pickups from server (request {})", acMessage.Entries.size(), acMessage.CreationEnginePickedUpReferences.size(), acMessage.RequestId);
    HandleDropSyncResponse(acMessage);
}

void DropService::OnNotifyDropMove(const NotifyDroppedItemMove& acMessage) noexcept
{
    if (!m_transport.IsConnected())
        return;
    if (m_suspendProcessing)
    {
        m_requestResyncAfterSuspend = true;
        return;
    }

    uint64_t resolvedDropId = acMessage.DropId;
    if (resolvedDropId == 0 && acMessage.ReferenceId)
    {
        if (const auto mappedDropId = DropManager::GetDropIdForReference(acMessage.ReferenceId))
            resolvedDropId = *mappedDropId;
    }

    TESObjectREFR* pReference = nullptr;
    NiPoint3 location{};
    NiPoint3 rotation{};
    NiPoint3 velocity{};

    if (resolvedDropId != 0)
    {
        const auto dropOpt = DropManager::GetServerDrop(resolvedDropId);
        if (!dropOpt)
            return;

        location = acMessage.HasLocation ? ToPoint(acMessage.Location) : dropOpt->Location;
        rotation = acMessage.HasRotation ? ToDropRotation(acMessage.Rotation) : dropOpt->Rotation;
        if (acMessage.HasVelocity)
            velocity = ToPoint(acMessage.Velocity);

        DropManager::UpdateServerDropTransform(resolvedDropId, location, rotation, acMessage.CellId, acMessage.WorldSpaceId, acMessage.ReferenceId, acMessage.HasVelocity, velocity);

        const auto updatedOpt = DropManager::GetServerDrop(resolvedDropId);
        if (!updatedOpt)
            return;

        if (const auto handleOpt = DropManager::GetHandleForDrop(resolvedDropId); handleOpt)
            pReference = TESObjectREFR::GetByHandle(*handleOpt);

        if (!pReference && updatedOpt->ReferenceId)
        {
            pReference = GetReferenceById(updatedOpt->ReferenceId);
            if (pReference)
            {
                auto handle = pReference->GetHandle();
                if (handle && handle.handle.iBits != 0)
                    DropManager::BindHandleToServerDrop(resolvedDropId, updatedOpt->ActorFormId, handle.handle.iBits);
            }
        }

        if (!pReference && updatedOpt->Type == ServerItemType::CreationEngine)
        {
            DropManager::ServerDropData rebound = *updatedOpt;
            rebound.Location = location;
            rebound.Rotation = rotation;
            TryBindExistingReference(resolvedDropId, rebound);
            if (const auto handleOpt = DropManager::GetHandleForDrop(resolvedDropId); handleOpt)
                pReference = TESObjectREFR::GetByHandle(*handleOpt);
        }

        if (!pReference)
            return;

        m_localDrops.insert(resolvedDropId);

        if (IsDropLocallyActive(resolvedDropId, updatedOpt->ReferenceId))
            return;
    }
    else if (acMessage.ReferenceId)
    {
        pReference = GetReferenceById(acMessage.ReferenceId);
        if (!pReference)
            return;

        location = acMessage.HasLocation ? ToPoint(acMessage.Location) : pReference->position;
        rotation = acMessage.HasRotation ? ToDropRotation(acMessage.Rotation) : pReference->rotation;

        if (IsDropLocallyActive(0, acMessage.ReferenceId))
            return;
    }
    else
    {
        return;
    }

    if (acMessage.HasLocation || acMessage.HasRotation)
    {
        SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);

        if (!CanTouchReference3D(pReference))
            return;

        MoveInterpolation interpolation{};
        interpolation.DropId = resolvedDropId;
        interpolation.ReferenceId = acMessage.ReferenceId;
        interpolation.HasLocation = acMessage.HasLocation;
        interpolation.HasRotation = acMessage.HasRotation;
        interpolation.HasVelocity = acMessage.HasVelocity;
        interpolation.HasAngularVelocity = acMessage.HasAngularVelocity;
        interpolation.Duration = kDropMoveInterpolationSeconds;
        interpolation.StartLocation = pReference->position;
        interpolation.StartRotation = pReference->rotation;
        interpolation.TargetLocation = location;
        interpolation.TargetRotation = rotation;
        if (interpolation.HasVelocity)
        {
            interpolation.Velocity = velocity;
            if (interpolation.HasLocation)
            {
                interpolation.TargetLocation.x += velocity.x * kDropMovePredictionSeconds;
                interpolation.TargetLocation.y += velocity.y * kDropMovePredictionSeconds;
                interpolation.TargetLocation.z += velocity.z * kDropMovePredictionSeconds;
            }
        }
        if (interpolation.HasAngularVelocity)
        {
            interpolation.AngularVelocity = ToPoint(acMessage.AngularVelocity);
            if (interpolation.HasRotation)
            {
                interpolation.TargetRotation.x += interpolation.AngularVelocity.x * kDropMovePredictionSeconds;
                interpolation.TargetRotation.y += interpolation.AngularVelocity.y * kDropMovePredictionSeconds;
                interpolation.TargetRotation.z += interpolation.AngularVelocity.z * kDropMovePredictionSeconds;
            }
        }

        if (resolvedDropId != 0)
            m_dropMoveInterpolations[resolvedDropId] = interpolation;
        else if (acMessage.ReferenceId)
            m_referenceMoveInterpolations[acMessage.ReferenceId] = interpolation;
    }
}

void DropService::OnNotifyDropPhysicsDisabled(const NotifyDroppedItemPhysicsDisabled& acMessage) noexcept
{
    if (!m_transport.IsConnected())
        return;
    if (m_suspendProcessing)
    {
        m_requestResyncAfterSuspend = true;
        return;
    }

    if (acMessage.DropId == 0)
        return;

    const auto dropOpt = DropManager::GetServerDrop(acMessage.DropId);
    if (!dropOpt)
        return;

    const NiPoint3 location = acMessage.HasLocation ? ToPoint(acMessage.Location) : dropOpt->Location;
    const NiPoint3 rotation = acMessage.HasRotation ? ToDropRotation(acMessage.Rotation) : dropOpt->Rotation;

    DropManager::UpdateServerDropTransform(acMessage.DropId, location, rotation, acMessage.CellId, acMessage.WorldSpaceId, acMessage.ReferenceId);

    const auto updatedOpt = DropManager::GetServerDrop(acMessage.DropId);
    if (!updatedOpt)
        return;

    const auto& data = *updatedOpt;
    const bool suppress = m_dropPhysicsDisableSuppressions.erase(acMessage.DropId) > 0;
    if (IsDropLocallyActive(acMessage.DropId, data.ReferenceId))
    {
        m_localDrops.insert(acMessage.DropId);
        return;
    }
    if (data.Type == ServerItemType::CreationEngine)
    {
        TESObjectREFR* pReference = nullptr;
        if (const auto handleOpt = DropManager::GetHandleForDrop(acMessage.DropId); handleOpt)
            pReference = TESObjectREFR::GetByHandle(*handleOpt);

        if (!pReference && data.ReferenceId)
        {
            pReference = GetReferenceById(data.ReferenceId);
            if (pReference)
            {
                auto handle = pReference->GetHandle();
                if (handle && handle.handle.iBits != 0)
                    DropManager::BindHandleToServerDrop(acMessage.DropId, data.ActorFormId, handle.handle.iBits);
            }
        }

        if (!pReference)
        {
            DropManager::ServerDropData rebound = data;
            rebound.Location = location;
            rebound.Rotation = rotation;
            TryBindExistingReference(acMessage.DropId, rebound);
            if (const auto handleOpt = DropManager::GetHandleForDrop(acMessage.DropId); handleOpt)
                pReference = TESObjectREFR::GetByHandle(*handleOpt);
        }

        if (!pReference)
            return;

        if (suppress)
        {
            if (ShouldDeferPhysicsLock(m_cellPhysicsGraceRemaining))
            {
                m_requestPhysicsLockAfterGrace = true;
            }
            else
            {
                SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);
                SafeUpdateReference3D(pReference, true);
            }
            m_localDrops.insert(acMessage.DropId);
            return;
        }

        if (acMessage.HasLocation)
        {
            if (TESObjectCELL* pCell = pReference->GetParentCellEx())
                pReference->SetPosition(location);
        }

        if (acMessage.HasRotation)
        {
            if (pReference->GetParentCellEx() || pReference->GetParentCell())
                pReference->SetRotation(rotation);
        }

        const bool locallyActive = IsDropLocallyActive(acMessage.DropId, data.ReferenceId);
        if (!locallyActive)
        {
            if (ShouldDeferPhysicsLock(m_cellPhysicsGraceRemaining))
            {
                m_requestPhysicsLockAfterGrace = true;
            }
            else
            {
                SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);
                SafeUpdateReference3D(pReference, true);
            }
        }

        m_localDrops.insert(acMessage.DropId);
        return;
    }

    if (!IsDropCellLoaded(data.CellId, data.WorldSpaceId))
        return;

    if (const auto handleOpt = DropManager::GetHandleForDrop(acMessage.DropId); handleOpt)
    {
        if (TESObjectREFR* pExisting = TESObjectREFR::GetByHandle(*handleOpt))
        {
            if (suppress)
            {
                SafeSetReferenceMotionType(pExisting, MotionType::kKeyframed, true);
                SafeUpdateReference3D(pExisting, true);
                m_localDrops.insert(acMessage.DropId);
                return;
            }

            if (pExisting->IsTemporary())
                pExisting->Delete();
            else
                pExisting->Disable();
        }
        DropManager::ClearHandleBinding(acMessage.DropId);
    }

    if (!SpawnLocalDrop(data, acMessage.DropId))
        return;

    m_localDrops.insert(acMessage.DropId);

    if (const auto newHandleOpt = DropManager::GetHandleForDrop(acMessage.DropId); newHandleOpt)
    {
        if (TESObjectREFR* pNewRef = TESObjectREFR::GetByHandle(*newHandleOpt))
        {
            SafeUpdateReference3D(pNewRef, true);
            SafeSetReferenceMotionType(pNewRef, MotionType::kKeyframed, true);
            SafeUpdateReference3D(pNewRef, true);
            GameId newRefId{};
            m_world.GetModSystem().GetServerModId(pNewRef->formID, newRefId);
            if (newRefId)
                DropManager::SetReferenceForDrop(acMessage.DropId, newRefId);
        }
    }
}

void DropService::OnConnected(const ConnectedEvent&) noexcept
{
    EnsureStorageReady();
    if (m_suspendProcessing)
    {
        m_requestResyncAfterSuspend = true;
        return;
    }

    m_pendingCreationEngineRemovals.clear();
    m_dropSyncQueue.clear();
    m_dropSyncQueuedCells.clear();
    m_grabbedDrops.clear();
    m_dropPhysicsCooldowns.clear();
    m_dropMoveSyncTimers.clear();
    m_dropPhysicsDisableSuppressions.clear();
    m_grabbedReferences.clear();
    m_referencePhysicsCooldowns.clear();
    m_referenceMoveSyncTimers.clear();
    m_dropSyncWorldSpace = GetPlayerWorldId();
    m_dropSyncQueueAccumulator = 0.0;
    m_periodicPlayerCellSyncAccumulator = 0.0;
    m_grabEventSuppressionRemaining = kGrabEventSuppressSeconds;
    RequestCellSync(true);

    if (m_dropSyncWorldSpace)
        QueueLoadedExteriorCells(m_dropSyncWorldSpace);
}

void DropService::OnCellChange(const CellChangeEvent& acEvent) noexcept
{
    if (m_suspendProcessing)
    {
        m_requestResyncAfterSuspend = true;
        return;
    }

    m_pendingCreationEngineRemovals.clear();
    m_grabEventSuppressionRemaining = kGrabEventSuppressSeconds;
    m_grabbedDrops.clear();
    m_dropPhysicsCooldowns.clear();
    m_dropMoveSyncTimers.clear();
    m_dropPhysicsDisableSuppressions.clear();
    m_grabbedReferences.clear();
    m_referencePhysicsCooldowns.clear();
    m_referenceMoveSyncTimers.clear();

    if (m_dropSyncWorldSpace != acEvent.WorldSpaceId)
    {
        m_dropSyncWorldSpace = acEvent.WorldSpaceId;
        m_dropSyncQueue.clear();
        m_dropSyncQueuedCells.clear();
        m_dropSyncQueueAccumulator = 0.0;
    }

    m_periodicPlayerCellSyncAccumulator = 0.0;
    m_pendingDiscoveryResyncs = 2;
    m_cellPhysicsGraceRemaining = kCellPhysicsGraceSeconds;
    m_requestPhysicsLockAfterGrace = true;

    QueueDropSync(acEvent.CellId, acEvent.WorldSpaceId, true);
}

void DropService::OnGridCellChange(const GridCellChangeEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;
    if (m_suspendProcessing)
    {
        m_requestResyncAfterSuspend = true;
        return;
    }

    TESWorldSpace* pWorldSpace = nullptr;
    if (acEvent.WorldSpaceId != 0)
    {
        if (TESForm* pForm = TESForm::GetById(acEvent.WorldSpaceId))
            pWorldSpace = Cast<TESWorldSpace>(pForm);
    }

    if (!pWorldSpace)
    {
        if (auto* pPlayer = PlayerCharacter::Get())
            pWorldSpace = pPlayer->GetWorldSpace();
    }

    if (!pWorldSpace)
        return;

    GameId worldId{};
    m_world.GetModSystem().GetServerModId(pWorldSpace->formID, worldId);
    if (!worldId)
        return;

    if (m_dropSyncWorldSpace != worldId)
    {
        m_dropSyncWorldSpace = worldId;
        m_dropSyncQueue.clear();
        m_dropSyncQueuedCells.clear();
        m_dropSyncQueueAccumulator = 0.0;
    }

    // Sync newly loaded cells so drops appear as soon as the cell is loaded, not only after walking into it.
    for (const auto& cellId : acEvent.Cells)
    {
        if (!cellId)
            continue;
        QueueDropSync(cellId, worldId, true);
    }
}

void DropService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_suspendProcessing)
    {
        m_suspendProcessingAccumulator += acEvent.Delta;
        auto* pPlayer = PlayerCharacter::Get();
        const bool ready = pPlayer && (pPlayer->GetParentCell() || pPlayer->parentCell) && pPlayer->GetNiNode();
        if (ready && m_suspendProcessingAccumulator >= kLoadSuspendSeconds)
        {
            m_suspendProcessing = false;
            m_suspendProcessingAccumulator = 0.0;
            m_cellPhysicsGraceRemaining = kCellPhysicsGraceSeconds;
            m_requestPhysicsLockAfterGrace = true;

            if (m_requestResyncAfterSuspend && m_transport.IsConnected())
            {
                RequestCellSync(true);
                const GameId worldId = GetPlayerWorldId();
                if (worldId)
                    QueueLoadedExteriorCells(worldId);
            }
            m_requestResyncAfterSuspend = false;
        }
        return;
    }

    const float delta = static_cast<float>(acEvent.Delta);
    if (m_cellPhysicsGraceRemaining > 0.0)
    {
        m_cellPhysicsGraceRemaining = std::max(0.0, m_cellPhysicsGraceRemaining - acEvent.Delta);
        if (m_cellPhysicsGraceRemaining <= 0.0 && m_requestPhysicsLockAfterGrace)
        {
            TiltedPhoques::Vector<uint64_t> dropIds(m_localDrops.begin(), m_localDrops.end());
            for (const auto dropId : dropIds)
            {
                const auto dropOpt = DropManager::GetServerDrop(dropId);
                if (!dropOpt)
                    continue;

                const auto& data = *dropOpt;
                if (IsDropLocallyActive(dropId, data.ReferenceId))
                    continue;

                TESObjectREFR* pReference = nullptr;
                if (const auto handleOpt = DropManager::GetHandleForDrop(dropId); handleOpt)
                    pReference = TESObjectREFR::GetByHandle(*handleOpt);

                if (!pReference && data.ReferenceId)
                    pReference = GetReferenceById(data.ReferenceId);

                if (!pReference || !pReference->GetNiNode())
                    continue;

                SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);
                SafeUpdateReference3D(pReference, true);
            }

            m_requestPhysicsLockAfterGrace = false;
        }
    }
    if (m_grabEventSuppressionRemaining > 0.0)
    {
        m_grabEventSuppressionRemaining = std::max(0.0, m_grabEventSuppressionRemaining - acEvent.Delta);
    }
    if (!m_dropPhysicsDisableSuppressions.empty())
    {
        TiltedPhoques::Vector<uint64_t> toErase;
        toErase.reserve(m_dropPhysicsDisableSuppressions.size());
        for (const auto& entry : m_dropPhysicsDisableSuppressions)
        {
            const float remaining = entry.second - delta;
            if (remaining <= 0.f)
                toErase.push_back(entry.first);
            else
                m_dropPhysicsDisableSuppressions[entry.first] = remaining;
        }
        for (const auto id : toErase)
            m_dropPhysicsDisableSuppressions.erase(id);
    }

    // Decrease grace timers for materialization gating
    if (!g_materializeGrace.empty())
    {
        TiltedPhoques::Vector<uint64_t> toErase;
        toErase.reserve(g_materializeGrace.size());
        for (const auto& kv : g_materializeGrace)
        {
            const uint64_t id = kv.first;
            const float newValue = kv.second - delta;
            if (newValue <= 0.f)
            {
                toErase.push_back(id);
            }
            else
            {
                g_materializeGrace[id] = newValue;
            }
        }
        for (const auto id : toErase)
        {
            g_materializeGrace.erase(id);
        }
    }


    ProcessPendingCreationEngineRemovals();

    if (m_transport.IsConnected())
    {
        m_periodicPlayerCellSyncAccumulator += acEvent.Delta;

        if (m_periodicPlayerCellSyncAccumulator >= kPeriodicPlayerCellSyncSeconds)
        {
            m_periodicPlayerCellSyncAccumulator = 0.0;
            if (m_pendingDiscoveryResyncs > 0)
            {
                --m_pendingDiscoveryResyncs;
                RequestCellSync(true);
            }
            else
            {
                RequestCellSync(false);
            }
        }

        m_dropSyncQueueAccumulator += acEvent.Delta;
        while (m_dropSyncQueueAccumulator >= kDropSyncQueueIntervalSeconds && !m_dropSyncQueue.empty())
        {
            m_dropSyncQueueAccumulator -= kDropSyncQueueIntervalSeconds;
            const auto next = m_dropSyncQueue.front();
            m_dropSyncQueue.pop_front();
            m_dropSyncQueuedCells.erase(next.CellId);
            auto discoveries = next.IncludeDiscovery ? BuildDiscoveryEntries(next.CellId, next.WorldSpaceId) : TiltedPhoques::Vector<RequestDroppedItems::DiscoveryEntry>{};
            SendDropSyncRequest(false, true, next.CellId, static_cast<bool>(next.WorldSpaceId), next.WorldSpaceId, std::move(discoveries));
        }
    }

    if (!m_dropMoveInterpolations.empty() || !m_referenceMoveInterpolations.empty())
    {
        auto updateInterpolation = [&](auto& map, auto&& resolveReference) {
            using MapType = std::decay_t<decltype(map)>;
            using KeyType = typename MapType::key_type;
            TiltedPhoques::Vector<KeyType> keys;
            keys.reserve(map.size());
            for (const auto& entry : map)
                keys.push_back(entry.first);

            TiltedPhoques::Vector<KeyType> toErase;
            toErase.reserve(map.size());

            for (const auto& key : keys)
            {
                auto& interpolation = map[key];
                TESObjectREFR* pReference = resolveReference(interpolation);
                if (!pReference)
                {
                    toErase.push_back(key);
                    continue;
                }

                if (!CanTouchReference3D(pReference))
                {
                    toErase.push_back(key);
                    continue;
                }

                if (interpolation.DropId != 0 && IsDropLocallyActive(interpolation.DropId, interpolation.ReferenceId))
                {
                    toErase.push_back(key);
                    continue;
                }

                if (interpolation.Duration <= 0.f)
                {
                    toErase.push_back(key);
                    continue;
                }

                interpolation.Elapsed += delta;
                const float duration = interpolation.Duration > 0.f ? interpolation.Duration : kDropMoveInterpolationSeconds;
                const float rawT = std::min(interpolation.Elapsed / duration, 1.f);
                const float t = rawT * rawT * (3.f - 2.f * rawT);

                if (interpolation.HasLocation)
                {
                    NiPoint3 blended{};
                    blended.x = interpolation.StartLocation.x + (interpolation.TargetLocation.x - interpolation.StartLocation.x) * t;
                    blended.y = interpolation.StartLocation.y + (interpolation.TargetLocation.y - interpolation.StartLocation.y) * t;
                    blended.z = interpolation.StartLocation.z + (interpolation.TargetLocation.z - interpolation.StartLocation.z) * t;
                    if (std::isfinite(blended.x) && std::isfinite(blended.y) && std::isfinite(blended.z) && pReference->GetParentCellEx())
                        pReference->SetPosition(blended);
                }

                if (interpolation.HasRotation)
                {
                    NiPoint3 blended{};
                    const float dx = NormalizeAngleDelta(interpolation.TargetRotation.x - interpolation.StartRotation.x);
                    const float dy = NormalizeAngleDelta(interpolation.TargetRotation.y - interpolation.StartRotation.y);
                    const float dz = NormalizeAngleDelta(interpolation.TargetRotation.z - interpolation.StartRotation.z);
                    blended.x = interpolation.StartRotation.x + dx * t;
                    blended.y = interpolation.StartRotation.y + dy * t;
                    blended.z = interpolation.StartRotation.z + dz * t;
                    if (std::isfinite(blended.x) && std::isfinite(blended.y) && std::isfinite(blended.z) &&
                        (pReference->GetParentCellEx() || pReference->GetParentCell()))
                        pReference->SetRotation(blended);
                }

                if (rawT >= 1.f)
                {
                    if (ShouldDeferPhysicsLock(m_cellPhysicsGraceRemaining))
                    {
                        m_requestPhysicsLockAfterGrace = true;
                    }
                    else
                    {
                        SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);
                        SafeUpdateReference3D(pReference, true);
                    }

                    toErase.push_back(key);
                }
            }

            for (const auto& key : toErase)
                map.erase(key);
        };

        if (!m_dropMoveInterpolations.empty())
        {
            updateInterpolation(m_dropMoveInterpolations, [&](const MoveInterpolation& interpolation) -> TESObjectREFR* {
                if (const auto handleOpt = DropManager::GetHandleForDrop(interpolation.DropId); handleOpt)
                    return TESObjectREFR::GetByHandle(*handleOpt);
                if (interpolation.ReferenceId)
                    return GetReferenceById(interpolation.ReferenceId);
                return nullptr;
            });
        }

        if (!m_referenceMoveInterpolations.empty())
        {
            updateInterpolation(m_referenceMoveInterpolations, [&](const MoveInterpolation& interpolation) -> TESObjectREFR* {
                if (interpolation.ReferenceId)
                    return GetReferenceById(interpolation.ReferenceId);
                return nullptr;
            });
        }
    }

    UpdateDropPhysics(acEvent);

    if (m_pendingActions.empty())
        return;

    TiltedPhoques::Vector<PendingAction> remaining;
    remaining.reserve(m_pendingActions.size());
    for (auto& action : m_pendingActions)
    {
        bool applied = false;
        if (action.Type == PendingType::Drop)
        {
            // Respect grace wait for materialization
            auto it = g_materializeGrace.find(action.DropMessage.DropId);
            if (it != g_materializeGrace.end() && it->second > 0.f)
            {
                remaining.push_back(std::move(action));
                continue;
            }

            applied = ApplyDrop(action.DropMessage);
            if (!applied)
                ++action.RetryCounter;
        }
        else
        {
            applied = ApplyPickup(action.PickupMessage);
            if (!applied)
                ++action.RetryCounter;
        }

        const uint32_t maxRetries = action.Type == PendingType::Drop ? kMaxPendingDropRetries : kMaxPendingPickupRetries;
        if (!applied && action.RetryCounter < maxRetries)
            remaining.push_back(std::move(action));
        else if (!applied)
            spdlog::warn("DropService: dropping pending {} after {} retries (limit {})", action.Type == PendingType::Drop ? "drop" : "pickup", action.RetryCounter, maxRetries);
    }

    m_pendingActions = std::move(remaining);
    if (!m_pendingActions.empty())
        spdlog::debug("DropService: {} pending drop actions remain", m_pendingActions.size());
}


BSTEventResult DropService::OnEvent(const TESGrabReleaseEvent* apEvent, const EventDispatcher<TESGrabReleaseEvent>*)
{
    if (!apEvent || !m_transport.IsConnected() || m_grabEventSuppressionRemaining > 0.0)
        return BSTEventResult::kOk;
    if (m_suspendProcessing)
        return BSTEventResult::kOk;

    TESObjectREFR* pReference = apEvent->reference;
    if (!pReference)
        return BSTEventResult::kOk;

    const auto handle = pReference->GetHandle();
    if (!handle)
        return BSTEventResult::kOk;

    TESObjectREFR* pResolved = TESObjectREFR::GetByHandle(handle.handle.iBits);
    if (!pResolved)
        return BSTEventResult::kOk;

    pReference = pResolved;

    GameId referenceId{};
    auto& modSystem = m_world.GetModSystem();
    modSystem.GetServerModId(pReference->formID, referenceId);

    const uint32_t handleBits = handle.handle.iBits;
    auto bindDrop = [&](uint64_t dropId) -> bool {
        const auto dropOpt = DropManager::GetServerDrop(dropId);
        if (!dropOpt)
            return false;

        if (const auto existingHandleOpt = DropManager::GetHandleForDrop(dropId); existingHandleOpt)
        {
            if (*existingHandleOpt != handleBits)
            {
                if (TESObjectREFR::GetByHandle(*existingHandleOpt))
                    return false;

                DropManager::ClearHandleBinding(dropId);
            }
        }

        if (!DropManager::BindHandleToServerDrop(dropId, dropOpt->ActorFormId, handleBits))
            return false;

        if (referenceId)
            DropManager::SetReferenceForDrop(dropId, referenceId);

        m_localDrops.insert(dropId);
        return true;
    };

    auto dropIdOpt = DropManager::GetDropIdForHandle(handleBits);
    if (dropIdOpt)
    {
        if (referenceId)
            DropManager::SetReferenceForDrop(*dropIdOpt, referenceId);
        m_localDrops.insert(*dropIdOpt);
    }

    if (!dropIdOpt && referenceId)
    {
        if (const auto mappedDropId = DropManager::GetDropIdForReference(referenceId))
        {
            if (bindDrop(*mappedDropId))
                dropIdOpt = mappedDropId;
        }
    }

    if (!dropIdOpt && pReference->baseForm && IsServerItemFormType(pReference->baseForm->formType))
    {
        GameId baseId{};
        modSystem.GetServerModId(pReference->baseForm->formID, baseId);
        if (baseId)
        {
            pReference->Update3DPosition(false);
            if (const auto matchedDropId = DropManager::FindDropBySignature(baseId, pReference->position, kDropSearchRadiusSquared))
            {
                if (bindDrop(*matchedDropId))
                    dropIdOpt = matchedDropId;
            }
        }
    }

    if (!dropIdOpt)
    {
        if (!referenceId)
            return BSTEventResult::kOk;

        if (apEvent->grabbed && IsEligibleServerItemRef(pReference))
        {
            if (!DropManager::GetDropIdForReference(referenceId))
            {
                auto& modSystem = m_world.GetModSystem();
                GameId cellId{};
                GameId worldId{};
                if (TESObjectCELL* pCell = pReference->GetParentCellEx())
                    modSystem.GetServerModId(pCell->formID, cellId);
                if (TESWorldSpace* pWorld = pReference->GetWorldSpace())
                    modSystem.GetServerModId(pWorld->formID, worldId);
                if (cellId && !m_dropStorage.FindDropIdByRefFormId(pReference->formID, cellId, worldId))
                {
                    RequestDroppedItems::DiscoveryEntry entry{};
                    entry.ReferenceId = referenceId;
                    entry.CellId = cellId;
                    entry.WorldSpaceId = worldId;
                    entry.HasLocation = true;
                    entry.Location = ToNetVector(pReference->position);
                    entry.HasRotation = true;
                    entry.Rotation = ToNetDropRotation(pReference->rotation);
                    entry.Item.Count = 1;
                    m_world.GetModSystem().GetServerModId(pReference->baseForm->formID, entry.Item.BaseId);
                    if (entry.Item.BaseId)
                    {
                        TESObjectREFR::GetItemFromExtraData(entry.Item, pReference->GetExtraDataList());
                        if (entry.Item.Count == 0)
                            entry.Item.Count = 1;

                        TiltedPhoques::Vector<RequestDroppedItems::DiscoveryEntry> discoveries;
                        discoveries.push_back(entry);
                        SendDropSyncRequest(false, true, cellId, static_cast<bool>(worldId), worldId, std::move(discoveries));
                    }
                }
            }
        }

        if (apEvent->grabbed)
        {
            m_grabbedReferences.insert(referenceId);
            m_referencePhysicsCooldowns.erase(referenceId);
            SafeSetReferenceMotionType(pReference, MotionType::kDynamic, true);
        }
        else
        {
            m_grabbedReferences.erase(referenceId);
            m_referencePhysicsCooldowns[referenceId] = kDropPhysicsHoldSeconds;
            SafeSetReferenceMotionType(pReference, MotionType::kDynamic, true);
        }

        return BSTEventResult::kOk;
    }

    if (!referenceId)
    {
        modSystem.GetServerModId(pReference->formID, referenceId);
    }
    if (referenceId)
    {
        m_grabbedReferences.erase(referenceId);
        m_referencePhysicsCooldowns.erase(referenceId);
        m_referenceMoveSyncTimers.erase(referenceId);
    }

    m_localDrops.insert(*dropIdOpt);
    if (apEvent->grabbed)
    {
        m_grabbedDrops.insert(*dropIdOpt);
        m_dropPhysicsCooldowns.erase(*dropIdOpt);
        SafeSetReferenceMotionType(pReference, MotionType::kDynamic, true);
    }
    else
    {
        m_grabbedDrops.erase(*dropIdOpt);
        m_dropPhysicsCooldowns[*dropIdOpt] = kDropPhysicsHoldSeconds;
        SafeSetReferenceMotionType(pReference, MotionType::kDynamic, true);
    }

    return BSTEventResult::kOk;
}

void DropService::UpdateDropPhysics(const UpdateEvent& acEvent) noexcept
{
    if (!m_transport.IsConnected())
        return;

    const float delta = static_cast<float>(acEvent.Delta);

    for (const auto dropId : m_localDrops)
    {
        const auto handleOpt = DropManager::GetHandleForDrop(dropId);
        if (!handleOpt)
            continue;

        TESObjectREFR* pReference = TESObjectREFR::GetByHandle(*handleOpt);
        if (!pReference)
            continue;

        if (m_grabbedDrops.find(dropId) != m_grabbedDrops.end())
        {
            SafeSetReferenceMotionType(pReference, MotionType::kDynamic, true);
            m_dropMoveSyncTimers[dropId] += delta;
            if (m_dropMoveSyncTimers[dropId] >= kDropMoveSyncIntervalSeconds)
            {
                const float elapsed = m_dropMoveSyncTimers[dropId];
                m_dropMoveSyncTimers[dropId] = 0.f;
                SendDropMoveRequest(dropId, pReference, true, elapsed);
            }
            continue;
        }

        if (auto cooldownIt = m_dropPhysicsCooldowns.find(dropId); cooldownIt != m_dropPhysicsCooldowns.end())
        {
            const float remaining = cooldownIt->second - delta;
            if (remaining <= 0.f)
            {
                m_dropPhysicsCooldowns.erase(cooldownIt);
                float elapsed = 0.f;
                if (const auto it = m_dropMoveSyncTimers.find(dropId); it != m_dropMoveSyncTimers.end())
                    elapsed = it->second;
                m_dropMoveSyncTimers.erase(dropId);
                SendDropMoveRequest(dropId, pReference, false, elapsed);
                SendDropPhysicsDisabledRequest(dropId, pReference);
                m_dropMoveLastLocations.erase(dropId);
                m_dropMoveLastRotations.erase(dropId);
                SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);
                SafeUpdateReference3D(pReference, true);
            }
            else
            {
                m_dropPhysicsCooldowns[dropId] = remaining;
                SafeSetReferenceMotionType(pReference, MotionType::kDynamic, true);
                m_dropMoveSyncTimers[dropId] += delta;
                if (m_dropMoveSyncTimers[dropId] >= kDropMoveSyncIntervalSeconds)
                {
                    const float elapsed = m_dropMoveSyncTimers[dropId];
                    m_dropMoveSyncTimers[dropId] = 0.f;
                    SendDropMoveRequest(dropId, pReference, true, elapsed);
                }
            }

            continue;
        }

        m_dropMoveSyncTimers.erase(dropId);
        m_dropMoveLastLocations.erase(dropId);
        m_dropMoveLastRotations.erase(dropId);
        SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);
        continue;
    }

    if (!m_grabbedReferences.empty())
    {
        TiltedPhoques::Vector<GameId> grabbedRefs;
        grabbedRefs.reserve(m_grabbedReferences.size());
        for (const auto& refId : m_grabbedReferences)
            grabbedRefs.push_back(refId);

        for (const auto& referenceId : grabbedRefs)
        {
            if (TESObjectREFR* pReference = GetReferenceById(referenceId))
            {
                SafeSetReferenceMotionType(pReference, MotionType::kDynamic, true);
                m_referenceMoveSyncTimers[referenceId] += delta;
                if (m_referenceMoveSyncTimers[referenceId] >= kDropMoveSyncIntervalSeconds)
                {
                    const float elapsed = m_referenceMoveSyncTimers[referenceId];
                    m_referenceMoveSyncTimers[referenceId] = 0.f;
                    SendReferenceMoveRequest(referenceId, pReference, elapsed);
                }
            }
        }
    }

    if (m_referencePhysicsCooldowns.empty())
        return;

    TiltedPhoques::Vector<GameId> readyReferences;
    readyReferences.reserve(m_referencePhysicsCooldowns.size());
    TiltedPhoques::Vector<std::pair<GameId, float>> pendingUpdates;
    pendingUpdates.reserve(m_referencePhysicsCooldowns.size());
    for (const auto& entry : m_referencePhysicsCooldowns)
    {
        const float remaining = entry.second - delta;
        if (remaining <= 0.f)
            readyReferences.push_back(entry.first);
        else
        {
            pendingUpdates.emplace_back(entry.first, remaining);
            if (TESObjectREFR* pReference = GetReferenceById(entry.first))
            {
                SafeSetReferenceMotionType(pReference, MotionType::kDynamic, true);
                m_referenceMoveSyncTimers[entry.first] += delta;
                if (m_referenceMoveSyncTimers[entry.first] >= kDropMoveSyncIntervalSeconds)
                {
                    const float elapsed = m_referenceMoveSyncTimers[entry.first];
                    m_referenceMoveSyncTimers[entry.first] = 0.f;
                    SendReferenceMoveRequest(entry.first, pReference, elapsed);
                }
            }
        }
    }

    for (const auto& update : pendingUpdates)
    {
        m_referencePhysicsCooldowns[update.first] = update.second;
    }

    for (const auto& referenceId : readyReferences)
    {
        m_referencePhysicsCooldowns.erase(referenceId);
        float elapsed = 0.f;
        if (const auto it = m_referenceMoveSyncTimers.find(referenceId); it != m_referenceMoveSyncTimers.end())
            elapsed = it->second;
        m_referenceMoveSyncTimers.erase(referenceId);
        if (TESObjectREFR* pReference = GetReferenceById(referenceId))
        {
            SendReferenceMoveRequest(referenceId, pReference, elapsed);
            SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);
            SafeUpdateReference3D(pReference, true);
        }

        m_referenceMoveLastLocations.erase(referenceId);
        m_referenceMoveLastRotations.erase(referenceId);
    }
}

void DropService::SendDropMoveRequest(uint64_t aDropId, TESObjectREFR* apReference, bool aForce, float aElapsedSeconds) noexcept
{
    if (!m_transport.IsConnected() || !apReference)
        return;

    const auto dropOpt = DropManager::GetServerDrop(aDropId);
    if (!dropOpt)
        return;

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const auto serverIdRes = ResolveServerId(pPlayer->formID);
    if (!serverIdRes)
        return;

    SafeUpdateReference3D(apReference, false);

    auto& modSystem = m_world.GetModSystem();
    GameId cellId{};
    GameId worldId{};
    if (TESObjectCELL* pCell = apReference->GetParentCellEx())
        modSystem.GetServerModId(pCell->formID, cellId);
    if (TESWorldSpace* pWorld = apReference->GetWorldSpace())
        modSystem.GetServerModId(pWorld->formID, worldId);

    GameId referenceId{};
    modSystem.GetServerModId(apReference->formID, referenceId);

    const NiPoint3 location = apReference->position;
    const NiPoint3 rotation = apReference->rotation;
    const float dx = location.x - dropOpt->Location.x;
    const float dy = location.y - dropOpt->Location.y;
    const float dz = location.z - dropOpt->Location.z;
    const float distSq = dx * dx + dy * dy + dz * dz;
    constexpr float kMoveEpsilonSq = 0.5f * 0.5f;
    const float rotDx = rotation.x - dropOpt->Rotation.x;
    const float rotDy = rotation.y - dropOpt->Rotation.y;
    const float rotDz = rotation.z - dropOpt->Rotation.z;
    const float rotDistSq = rotDx * rotDx + rotDy * rotDy + rotDz * rotDz;
    constexpr float kRotEpsilonSq = 0.5f * 0.5f;

    if (!aForce && distSq <= kMoveEpsilonSq && rotDistSq <= kRotEpsilonSq)
        return;

    bool hasVelocity = false;
    NiPoint3 velocity{};
    bool hasAngularVelocity = false;
    NiPoint3 angularVelocity{};
    if (aElapsedSeconds > 0.f)
    {
        if (const auto it = m_dropMoveLastLocations.find(aDropId); it != m_dropMoveLastLocations.end())
        {
            velocity.x = (location.x - it->second.x) / aElapsedSeconds;
            velocity.y = (location.y - it->second.y) / aElapsedSeconds;
            velocity.z = (location.z - it->second.z) / aElapsedSeconds;
            hasVelocity = true;
        }
        if (const auto it = m_dropMoveLastRotations.find(aDropId); it != m_dropMoveLastRotations.end())
        {
            angularVelocity.x = NormalizeAngleDelta(rotation.x - it->second.x) / aElapsedSeconds;
            angularVelocity.y = NormalizeAngleDelta(rotation.y - it->second.y) / aElapsedSeconds;
            angularVelocity.z = NormalizeAngleDelta(rotation.z - it->second.z) / aElapsedSeconds;
            hasAngularVelocity = true;
        }
    }

    m_dropMoveLastLocations[aDropId] = location;
    m_dropMoveLastRotations[aDropId] = rotation;

    DropManager::UpdateServerDropTransform(aDropId, location, rotation, cellId, worldId, referenceId, hasVelocity, velocity);

    RequestDroppedItemMove request{};
    request.ServerId = *serverIdRes;
    request.DropId = aDropId;
    request.HasLocation = true;
    request.Location = ToNetVector(location);
    request.HasRotation = true;
    request.Rotation = ToNetDropRotation(rotation);
    request.HasVelocity = hasVelocity;
    if (hasVelocity)
        request.Velocity = ToNetVector(velocity);
    request.HasAngularVelocity = hasAngularVelocity;
    if (hasAngularVelocity)
        request.AngularVelocity = ToNetVector(angularVelocity);
    request.CellId = cellId;
    request.WorldSpaceId = worldId;
    request.ReferenceId = referenceId;

    m_transport.Send(request);
}

void DropService::SendDropPhysicsDisabledRequest(uint64_t aDropId, TESObjectREFR* apReference) noexcept
{
    if (!m_transport.IsConnected() || !apReference)
        return;

    const auto dropOpt = DropManager::GetServerDrop(aDropId);
    if (!dropOpt)
        return;

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const auto serverIdRes = ResolveServerId(pPlayer->formID);
    if (!serverIdRes)
        return;

    apReference->Update3DPosition(false);

    auto& modSystem = m_world.GetModSystem();
    GameId cellId{};
    GameId worldId{};
    if (TESObjectCELL* pCell = apReference->GetParentCellEx())
        modSystem.GetServerModId(pCell->formID, cellId);
    if (TESWorldSpace* pWorld = apReference->GetWorldSpace())
        modSystem.GetServerModId(pWorld->formID, worldId);

    GameId referenceId{};
    modSystem.GetServerModId(apReference->formID, referenceId);

    RequestDroppedItemPhysicsDisabled request{};
    request.ServerId = *serverIdRes;
    request.DropId = aDropId;
    request.HasLocation = true;
    request.Location = ToNetVector(apReference->position);
    request.HasRotation = true;
    request.Rotation = ToNetDropRotation(apReference->rotation);
    request.CellId = cellId;
    request.WorldSpaceId = worldId;
    request.ReferenceId = referenceId;

    m_transport.Send(request);
}

void DropService::SendReferenceMoveRequest(const GameId& acReferenceId, TESObjectREFR* apReference, float aElapsedSeconds) noexcept
{
    if (!m_transport.IsConnected() || !apReference)
        return;

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const auto serverIdRes = ResolveServerId(pPlayer->formID);
    if (!serverIdRes)
        return;

    apReference->Update3DPosition(false);

    auto& modSystem = m_world.GetModSystem();
    GameId cellId{};
    GameId worldId{};
    if (TESObjectCELL* pCell = apReference->GetParentCellEx())
        modSystem.GetServerModId(pCell->formID, cellId);
    if (TESWorldSpace* pWorld = apReference->GetWorldSpace())
        modSystem.GetServerModId(pWorld->formID, worldId);

    const NiPoint3 location = apReference->position;
    const NiPoint3 rotation = apReference->rotation;
    bool hasVelocity = false;
    NiPoint3 velocity{};
    bool hasAngularVelocity = false;
    NiPoint3 angularVelocity{};
    if (aElapsedSeconds > 0.f)
    {
        if (const auto it = m_referenceMoveLastLocations.find(acReferenceId); it != m_referenceMoveLastLocations.end())
        {
            velocity.x = (location.x - it->second.x) / aElapsedSeconds;
            velocity.y = (location.y - it->second.y) / aElapsedSeconds;
            velocity.z = (location.z - it->second.z) / aElapsedSeconds;
            hasVelocity = true;
        }
        if (const auto it = m_referenceMoveLastRotations.find(acReferenceId); it != m_referenceMoveLastRotations.end())
        {
            angularVelocity.x = NormalizeAngleDelta(rotation.x - it->second.x) / aElapsedSeconds;
            angularVelocity.y = NormalizeAngleDelta(rotation.y - it->second.y) / aElapsedSeconds;
            angularVelocity.z = NormalizeAngleDelta(rotation.z - it->second.z) / aElapsedSeconds;
            hasAngularVelocity = true;
        }
    }

    m_referenceMoveLastLocations[acReferenceId] = location;
    m_referenceMoveLastRotations[acReferenceId] = rotation;

    RequestDroppedItemMove request{};
    request.ServerId = *serverIdRes;
    request.DropId = 0;
    request.HasLocation = true;
    request.Location = ToNetVector(location);
    request.HasRotation = true;
    request.Rotation = ToNetDropRotation(rotation);
    request.HasVelocity = hasVelocity;
    if (hasVelocity)
        request.Velocity = ToNetVector(velocity);
    request.HasAngularVelocity = hasAngularVelocity;
    if (hasAngularVelocity)
        request.AngularVelocity = ToNetVector(angularVelocity);
    request.CellId = cellId;
    request.WorldSpaceId = worldId;
    request.ReferenceId = acReferenceId;

    m_transport.Send(request);
}

std::optional<uint32_t> DropService::ResolveServerId(uint32_t aFormId) const noexcept
{
    auto view = m_world.view<FormIdComponent>();

    const auto it = std::find_if(std::begin(view), std::end(view), [view, formId = aFormId](auto entity) { return view.get<FormIdComponent>(entity).Id == formId; });
    if (it != std::end(view))
        return Utils::GetServerId(*it);

    if (auto* pPlayer = PlayerCharacter::Get(); pPlayer && pPlayer->formID == aFormId)
    {
        const uint32_t playerId = m_transport.GetLocalPlayerId();
        if (playerId)
            return playerId;
    }

    return std::nullopt;
}

bool DropService::EnsureActorReady(Actor* apActor, const char* apContext) const noexcept
{
    if (!apActor)
        return false;

    if (!apActor->GetNiNode())
    {
        spdlog::debug("{}: actor {:X} missing 3D during {}", __FUNCTION__, apActor->formID, apContext);
        return false;
    }

    if (ExtraContainerChanges::Data* pContainerChanges = apActor->GetContainerChanges();
        !pContainerChanges || !pContainerChanges->entries)
    {
        spdlog::debug("{}: actor {:X} missing container data during {}", __FUNCTION__, apActor->formID, apContext);
        return false;
    }

    return true;
}

bool DropService::EnsureStorageReady() noexcept
{
    std::string username = m_transport.GetLoginUsername();
    if (username.empty())
        username = "default";

    if (username != m_cachedUsername)
    {
        m_cachedUsername = username;
        m_coSaveService.PrepareForUser(username);
    }

    m_dropStorage.PrepareInMemory();
    spdlog::info("DropService: storage ready=true (user='{}')", username);
    return true;
}

uint32_t DropService::SendDropSyncRequest(bool aRequestAll, bool aHasCellFilter, const GameId& acCellId, bool aHasWorldFilter, const GameId& acWorldId, TiltedPhoques::Vector<RequestDroppedItems::DiscoveryEntry> aDiscoveries) noexcept
{
    if (!m_transport.IsConnected())
        return 0;
    if (m_suspendProcessing)
    {
        m_requestResyncAfterSuspend = true;
        return 0;
    }

    RequestDroppedItems request{};
    request.RequestId = m_nextDropSyncRequestId++;
    if (request.RequestId == 0)
        request.RequestId = m_nextDropSyncRequestId++;

    request.RequestAll = aRequestAll;
    request.HasCellFilter = aHasCellFilter;
    if (aHasCellFilter)
        request.CellId = acCellId;
    request.HasWorldSpaceFilter = aHasWorldFilter;
    if (aHasWorldFilter)
        request.WorldSpaceId = acWorldId;
    request.Discoveries = std::move(aDiscoveries);

    m_transport.Send(request);

    DropSyncContext context{};
    context.IsFullSync = aRequestAll;
    if (aHasCellFilter)
        context.CellId = acCellId;
    if (aHasWorldFilter)
        context.WorldSpaceId = acWorldId;

    if (request.RequestId != 0)
        m_pendingDropSyncs[request.RequestId] = context;

    spdlog::debug("DropService: requested drop sync {}, all={}, cell {:X}:{:X}", request.RequestId, aRequestAll, context.CellId.ModId, context.CellId.BaseId);
    return request.RequestId;
}

void DropService::QueueDropSync(const GameId& acCellId, const GameId& acWorldId, bool aIncludeDiscovery) noexcept
{
    if (!acCellId)
        return;

    if (m_dropSyncQueuedCells.find(acCellId) != std::end(m_dropSyncQueuedCells))
    {
        if (aIncludeDiscovery)
        {
            for (auto& queued : m_dropSyncQueue)
            {
                if (queued.CellId == acCellId)
                {
                    queued.IncludeDiscovery = true;
                    break;
                }
            }
        }
        return;
    }

    QueuedDropSync queued{};
    queued.CellId = acCellId;
    queued.WorldSpaceId = acWorldId;
    queued.IncludeDiscovery = aIncludeDiscovery;

    m_dropSyncQueue.push_back(queued);
    m_dropSyncQueuedCells.insert(acCellId);
}

void DropService::QueueLoadedExteriorCells(const GameId& acWorldId) noexcept
{
    if (!acWorldId)
        return;

    TES* pTes = TES::Get();
    if (!pTes || !pTes->cells || !pTes->cells->arr)
        return;

    const int dimension = pTes->cells->dimension;
    if (dimension <= 0)
        return;

    const int cellCount = dimension * dimension;
    for (int i = 0; i < cellCount; ++i)
    {
        TESObjectCELL* pCell = pTes->cells->arr[i];
        if (!pCell)
            continue;

        GameId cellId{};
        if (!m_world.GetModSystem().GetServerModId(pCell->formID, cellId) || !cellId)
            continue;

        QueueDropSync(cellId, acWorldId, true);
    }
}

void DropService::HandleDropSyncResponse(const NotifyDroppedItems& acMessage) noexcept
{
    std::optional<DropSyncContext> context{};
    if (acMessage.RequestId != 0)
    {
        auto it = m_pendingDropSyncs.find(acMessage.RequestId);
        if (it != m_pendingDropSyncs.end())
        {
            context = it->second;
            m_pendingDropSyncs.erase(it);
        }
    }

    TiltedPhoques::Vector<uint64_t> serverDropIds;
    serverDropIds.reserve(acMessage.Entries.size());

    for (const auto& entry : acMessage.Entries)
    {
        const bool forceMaterialize = context && !context->IsFullSync;
        auto& knownEpoch = m_knownSpawnEpochs[entry.DropId];
        if (entry.SpawnEpoch > knownEpoch)
            knownEpoch = entry.SpawnEpoch;
        ProcessDropEntry(entry, forceMaterialize);
        serverDropIds.push_back(entry.DropId);
    }

    if (context && !context->IsFullSync)
    {
        ReconcileCachedDrops(context->CellId, context->WorldSpaceId, serverDropIds);
        ApplyCreationEngineCellSync(*context, acMessage.CreationEnginePickedUpReferences);
    }
    else if (context && context->IsFullSync)
    {
        TiltedPhoques::Map<uint64_t, bool> liveIds;
        for (auto id : serverDropIds)
            liveIds[id] = true;

        auto cached = m_dropStorage.GetAllDrops();
        for (const auto& entry : cached)
        {
            if (liveIds.find(entry.DropId) != std::end(liveIds))
                continue;

            bool removed = false;
            if (entry.ReferenceId)
                removed = RemoveReferenceById(entry.ReferenceId, "full sync stale reference");
            if (entry.RefFormId && !removed)
            {
                if (auto* pForm = TESForm::GetById(entry.RefFormId))
                {
                    if (auto* pRef = Cast<TESObjectREFR>(pForm))
                    {
                        pRef->Delete();
                        removed = true;
                    }
                }
            }

            if (!removed)
            {
                if (TESBoundObject* pObject = ResolveDroppedObject(entry.Item))
                {
                    if (TESObjectREFR* pRef = FindReferenceNear(pObject, entry.Location))
                    {
                        pRef->Delete();
                        removed = true;
                    }
                }
            }

            if (removed)
                spdlog::warn("DropService: removed stale cached drop {} during full sync", entry.DropId);

            m_dropStorage.RemoveCachedDrop(entry.DropId);
        }
    }
}

void DropService::ProcessDropEntry(const NotifyDroppedItems::Entry& acEntry, bool aForceMaterialize) noexcept
{
    DropManager::ServerDropData data{};
    data.ServerId = acEntry.ServerId;
    data.ActorFormId = acEntry.ActorFormId;
    data.Type = acEntry.Type;
    data.Item = acEntry.Item;
    data.Location = acEntry.HasLocation ? ToPoint(acEntry.Location) : NiPoint3{};
    data.Rotation = acEntry.HasRotation ? ToDropRotation(acEntry.Rotation) : NiPoint3{};
    data.HandleBits = 0;
    data.CellId = acEntry.CellId;
    data.WorldSpaceId = acEntry.WorldSpaceId;
    data.ReferenceId = acEntry.ReferenceId;

    DropManager::TrackServerDrop(acEntry.DropId, data);

    if (const auto handleOpt = DropManager::GetHandleForDrop(acEntry.DropId); handleOpt && TESObjectREFR::GetByHandle(*handleOpt))
    {
        if (TESObjectREFR* pReference = TESObjectREFR::GetByHandle(*handleOpt))
        {
            const bool locallyActive = IsDropLocallyActive(acEntry.DropId, data.ReferenceId);
            if (!locallyActive && HasDropLocation(data.Location) && IsDropCellLoaded(data.CellId, data.WorldSpaceId))
            {
                TESObjectCELL* pCell = nullptr;
                if (data.CellId)
                {
                    const uint32_t cellFormId = m_world.GetModSystem().GetGameId(data.CellId);
                    if (cellFormId)
                        pCell = Cast<TESObjectCELL>(TESForm::GetById(cellFormId));
                }
                if (!pCell)
                    pCell = pReference->GetParentCellEx();
                const auto* pCurrentCell = pReference->GetParentCellEx();
                if (!pCell && !pCurrentCell)
                {
                    spdlog::debug("DropService: skip transform for drop {} (reference missing parent cell)", acEntry.DropId);
                }
                else
                {
                    if (pCell && (!pCurrentCell || pCell != pCurrentCell))
                        pReference->MoveTo(pCell, data.Location);
                    else if (pCurrentCell)
                        pReference->SetPosition(data.Location);
                    pReference->SetRotation(data.Rotation);
                    if (pReference->GetNiNode())
                        SafeUpdateReference3D(pReference, true);
                }
        }
            if (!locallyActive && pReference->GetNiNode())
            {
                if (ShouldDeferPhysicsLock(m_cellPhysicsGraceRemaining))
                    m_requestPhysicsLockAfterGrace = true;
                else
                {
                    SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);
                    SafeUpdateReference3D(pReference, true);
                }
            }
        }
        m_localDrops.insert(acEntry.DropId);
        spdlog::debug("DropService: skip drop {} from sync (already present locally)", acEntry.DropId);
        return;
    }

    if (!MaterializeDrop(acEntry.DropId, data, aForceMaterialize))
    {
        if (data.Type == ServerItemType::CreationEngine)
        {
            spdlog::debug("DropService: creation-engine item {} missing locally, deferring bind", acEntry.DropId);
            return;
        }

        if (!aForceMaterialize && g_materializeGrace.find(acEntry.DropId) == std::end(g_materializeGrace))
            g_materializeGrace[acEntry.DropId] = kMaterializeGraceSeconds;

        PendingAction pending{};
        pending.Type = PendingType::Drop;
        pending.DropMessage.DropId = acEntry.DropId;
        pending.DropMessage.ServerId = acEntry.ServerId;
        pending.DropMessage.ActorFormId = acEntry.ActorFormId;
        pending.DropMessage.Item = acEntry.Item;
        pending.DropMessage.HasLocation = acEntry.HasLocation;
        if (acEntry.HasLocation)
            pending.DropMessage.Location = acEntry.Location;
        pending.DropMessage.HasRotation = acEntry.HasRotation;
        if (acEntry.HasRotation)
            pending.DropMessage.Rotation = acEntry.Rotation;
        pending.DropMessage.CellId = acEntry.CellId;
        pending.DropMessage.WorldSpaceId = acEntry.WorldSpaceId;
        pending.DropMessage.SpawnEpoch = acEntry.SpawnEpoch;
        m_pendingActions.push_back(std::move(pending));
        spdlog::debug("DropService: queued drop {} for delayed materialization", acEntry.DropId);
    }
}

bool DropService::MaterializeDrop(uint64_t aDropId, const DropManager::ServerDropData& acData, bool aForce) noexcept
{
    if (const auto handleOpt = DropManager::GetHandleForDrop(aDropId); handleOpt)
    {
        if (TESObjectREFR::GetByHandle(*handleOpt))
            return true;

        spdlog::debug("DropService: stale handle {:X} for drop {}, clearing and re-materializing", *handleOpt, aDropId);
        DropManager::ClearHandleBinding(aDropId);
        m_localDrops.erase(aDropId);
    }

    if (TryBindExistingReference(aDropId, acData))
        return true;

    if (acData.Type == ServerItemType::CreationEngine)
        return false;

    if (m_materializingDrops.find(aDropId) != std::end(m_materializingDrops))
    {
        spdlog::debug("DropService: drop {} already materializing, skipping duplicate spawn", aDropId);
        return true;
    }

    const bool hasLocation = HasDropLocation(acData.Location);
    if (!hasLocation)
        return false;

    if (!IsDropCellLoaded(acData.CellId, acData.WorldSpaceId))
        return false;

    if (!aForce)
    {
        auto itTimer = g_materializeGrace.find(aDropId);
        if (itTimer == std::end(g_materializeGrace))
        {
            g_materializeGrace[aDropId] = kMaterializeGraceSeconds;

            PendingAction pending{};
            pending.Type = PendingType::Drop;
            pending.DropMessage.DropId = aDropId;
            pending.DropMessage.ServerId = acData.ServerId;
            pending.DropMessage.ActorFormId = acData.ActorFormId;
            pending.DropMessage.Item = acData.Item;
            pending.DropMessage.HasLocation = true;
            pending.DropMessage.Location = ToNetVector(acData.Location);
            pending.DropMessage.HasRotation = true;
            pending.DropMessage.Rotation = ToNetDropRotation(acData.Rotation);
            pending.DropMessage.CellId = acData.CellId;
            pending.DropMessage.WorldSpaceId = acData.WorldSpaceId;
            pending.DropMessage.ReferenceId = acData.ReferenceId;
            pending.DropMessage.SpawnEpoch = (m_knownSpawnEpochs.find(aDropId) != std::end(m_knownSpawnEpochs)) ? m_knownSpawnEpochs[aDropId] : 0;
            m_pendingActions.push_back(std::move(pending));

            spdlog::debug("DropService: scheduled grace wait before spawning drop {}", aDropId);
            return false;
        }

        if (itTimer->second > 0.f)
            return false;
    }

    m_materializingDrops.insert(aDropId);
    const bool spawned = SpawnLocalDrop(acData, aDropId);
    m_materializingDrops.erase(aDropId);

    if (!spawned)
    {
        spdlog::warn("DropService: failed to materialize drop {} in cell {:X}:{:X}", aDropId, acData.CellId.ModId, acData.CellId.BaseId);
        return false;
    }

    m_localDrops.insert(aDropId);
    return true;
}

bool DropService::SpawnLocalDrop(const DropManager::ServerDropData& acData, uint64_t aDropId) noexcept
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    if (!EnsureActorReady(pPlayer, "materialize drop"))
        return false;

    TESBoundObject* pObject = ResolveDroppedObject(acData.Item);
    if (!pObject)
        return false;

    TESObjectCELL* pCell = nullptr;
    if (acData.CellId)
    {
        const uint32_t cellFormId = m_world.GetModSystem().GetGameId(acData.CellId);
        if (cellFormId)
        {
            TES* pTes = TES::Get();
            if (pTes && pTes->cells && pTes->cells->arr)
            {
                const int dimension = pTes->cells->dimension;
                if (dimension > 0)
                {
                    const int cellCount = dimension * dimension;
                    for (int i = 0; i < cellCount; ++i)
                    {
                        TESObjectCELL* pCandidate = pTes->cells->arr[i];
                        if (pCandidate && pCandidate->formID == cellFormId)
                        {
                            pCell = pCandidate;
                            break;
                        }
                    }
                }
            }

            if (!pCell)
            {
                TESObjectCELL* pInteriorCell = pTes ? pTes->interiorCell : nullptr;
                if (pInteriorCell && pInteriorCell->formID == cellFormId)
                    pCell = pInteriorCell;
            }
        }
    }

    if (!pCell)
    {
        TESObjectCELL* pPlayerCell = pPlayer->parentCell ? pPlayer->parentCell : pPlayer->GetParentCell();
        if (pPlayerCell)
        {
            GameId playerCellId{};
            m_world.GetModSystem().GetServerModId(pPlayerCell->formID, playerCellId);
            if (playerCellId == acData.CellId)
                pCell = pPlayerCell;
        }
    }

    if (!pCell)
        return false;

    TESWorldSpace* pWorldSpace = pPlayer->GetWorldSpace();
    if (acData.WorldSpaceId)
    {
        const uint32_t worldFormId = m_world.GetModSystem().GetGameId(acData.WorldSpaceId);
        if (worldFormId)
        {
            if (TESForm* pWorldForm = TESForm::GetById(worldFormId))
            {
                if (TESWorldSpace* pResolvedWorld = Cast<TESWorldSpace>(pWorldForm))
                    pWorldSpace = pResolvedWorld;
            }
        }
    }
    NiPoint3 dropLocation = acData.Location;
    NiPoint3 dropRotation = acData.Rotation;

    const uint32_t handleBits = ModManager::Get()->SpawnReference(pObject, dropLocation, dropRotation, pCell, pWorldSpace, nullptr, false);
    if (handleBits == 0)
        return false;

    TESObjectREFR* pReference = TESObjectREFR::GetByHandle(handleBits);
    if (!pReference)
        return false;

    Inventory::Entry extraEntry = acData.Item;
    extraEntry.ExtraWorn = false;
    extraEntry.ExtraWornLeft = false;
    const uint16_t dropCount = ClampExtraCount(extraEntry.Count);
    ApplyDropExtraData(pReference, extraEntry, dropCount);
    if (ShouldDeferPhysicsLock(m_cellPhysicsGraceRemaining))
    {
        m_requestPhysicsLockAfterGrace = true;
    }
    else
    {
        SafeSetReferenceMotionType(pReference, MotionType::kKeyframed, true);
        SafeUpdateReference3D(pReference, true);
    }

    DropManager::BindHandleToServerDrop(aDropId, acData.ActorFormId, handleBits);

    spdlog::debug("DropService: spawned local drop {} at ({:.2f}, {:.2f}, {:.2f}) handle {:X}", aDropId, dropLocation.x, dropLocation.y, dropLocation.z, handleBits);
    return true;
}

bool DropService::RemoveNearbyReference(uint64_t aDropId, const char* apReason, float aRadiusSq) noexcept
{
    const auto dropOpt = DropManager::GetServerDrop(aDropId);
    if (!dropOpt)
        return false;

    TESBoundObject* pObject = ResolveDroppedObject(dropOpt->Item);
    if (!pObject)
        return false;

    if (TESObjectREFR* pRef = FindReferenceNear(pObject, dropOpt->Location, aRadiusSq))
    {
        if (pRef->IsTemporary())
            pRef->Delete();
        else
            pRef->Disable();
        spdlog::warn("DropService: {} -> removed fallback drop {} ({:X}:{:X})", apReason ? apReason : "cleanup", aDropId, dropOpt->Item.BaseId.ModId, dropOpt->Item.BaseId.BaseId);
        return true;
    }

    spdlog::debug("DropService: {} fallback failed for drop {}", apReason ? apReason : "cleanup", aDropId);
    return false;
}

TESObjectREFR* DropService::GetReferenceById(const GameId& acReferenceId) noexcept
{
    if (!acReferenceId)
        return nullptr;

    auto& modSystem = m_world.GetModSystem();
    const uint32_t formId = modSystem.GetGameId(acReferenceId);
    if (!formId)
        return nullptr;

    TESForm* pForm = TESForm::GetById(formId);
    return Cast<TESObjectREFR>(pForm);
}

bool DropService::RemoveReferenceById(const GameId& acReferenceId, const char* apReason) noexcept
{
    TESObjectREFR* pReference = GetReferenceById(acReferenceId);
    if (!pReference)
        return false;

    if (pReference->IsTemporary())
        pReference->Delete();
    else
        pReference->Disable();
    spdlog::debug("DropService: {} -> removed reference {:X}:{:X}", apReason ? apReason : "cleanup", acReferenceId.ModId, acReferenceId.BaseId);
    return true;
}

bool DropService::RemoveReferenceByLocation(const Inventory::Entry& acItem, const Vector3_NetQuantize& acLocation, const char* apReason, float aRadiusSq) noexcept
{
    TESBoundObject* pObject = ResolveDroppedObject(acItem);
    if (!pObject)
        return false;

    const NiPoint3 location = ToPoint(acLocation);
    if (TESObjectREFR* pRef = FindReferenceNear(pObject, location, aRadiusSq))
    {
        if (pRef->IsTemporary())
            pRef->Delete();
        else
            pRef->Disable();
        spdlog::debug("DropService: {} -> removed reference near ({:.2f}, {:.2f}, {:.2f}) for item {:X}:{:X}", apReason ? apReason : "cleanup", location.x, location.y, location.z, acItem.BaseId.ModId,
                      acItem.BaseId.BaseId);
        return true;
    }

    spdlog::debug("DropService: {} failed to remove reference by location for item {:X}:{:X}", apReason ? apReason : "cleanup", acItem.BaseId.ModId, acItem.BaseId.BaseId);
    return false;
}

bool DropService::TryBindExistingReference(uint64_t aDropId, const DropManager::ServerDropData& acData) noexcept
{
    auto tryBind = [&](TESObjectREFR* apReference) -> bool {
        if (!apReference)
            return false;

        auto handle = apReference->GetHandle();
        if (!handle || handle.handle.iBits == 0)
            return false;

        if (!DropManager::BindHandleToServerDrop(aDropId, acData.ActorFormId, handle.handle.iBits))
            return false;

        GameId referenceId = acData.ReferenceId;
        if (!referenceId)
        {
            auto& modSystem = m_world.GetModSystem();
            modSystem.GetServerModId(apReference->formID, referenceId);
        }

        if (referenceId)
            DropManager::SetReferenceForDrop(aDropId, referenceId);

        m_localDrops.insert(aDropId);

        if (referenceId)
        {
            if (m_grabbedReferences.erase(referenceId) > 0)
            {
                m_referenceMoveSyncTimers.erase(referenceId);
                m_grabbedDrops.insert(aDropId);
            }

            if (const auto cooldownIt = m_referencePhysicsCooldowns.find(referenceId); cooldownIt != m_referencePhysicsCooldowns.end())
            {
                const float remaining = cooldownIt->second;
                m_referencePhysicsCooldowns.erase(cooldownIt);
                m_referenceMoveSyncTimers.erase(referenceId);
                m_dropPhysicsCooldowns[aDropId] = remaining;
            }
        }

        const bool locallyActive = IsDropLocallyActive(aDropId, referenceId);
        const bool cellLoaded = IsDropCellLoaded(acData.CellId, acData.WorldSpaceId);
        if (!locallyActive && HasDropLocation(acData.Location) && cellLoaded)
        {
            TESObjectCELL* pCell = nullptr;
            if (acData.CellId)
            {
                const uint32_t cellFormId = m_world.GetModSystem().GetGameId(acData.CellId);
                if (cellFormId)
                    pCell = Cast<TESObjectCELL>(TESForm::GetById(cellFormId));
            }
            if (!pCell)
                pCell = apReference->GetParentCellEx();
            const auto* pCurrentCell = apReference->GetParentCellEx();
            if (!pCell && !pCurrentCell)
            {
                spdlog::debug("DropService: skip transform for drop {} (reference missing parent cell)", aDropId);
            }
            else
            {
                if (pCell && (!pCurrentCell || pCell != pCurrentCell))
                    apReference->MoveTo(pCell, acData.Location);
                else if (pCurrentCell)
                    apReference->SetPosition(acData.Location);
                apReference->SetRotation(acData.Rotation);
                if (apReference->GetNiNode())
                    SafeUpdateReference3D(apReference, true);
            }
        }

        if (!locallyActive)
        {
            if (apReference->GetNiNode())
            {
                if (ShouldDeferPhysicsLock(m_cellPhysicsGraceRemaining))
                {
                    m_requestPhysicsLockAfterGrace = true;
                }
                else
                {
                    SafeSetReferenceMotionType(apReference, MotionType::kKeyframed, true);
                    SafeUpdateReference3D(apReference, true);
                }
            }
        }

        spdlog::debug("DropService: rebound existing reference {:X}:{:X} for drop {}", referenceId.ModId, referenceId.BaseId, aDropId);
        return true;
    };

    EnsureStorageReady();

    const auto cachedRefFormId = m_dropStorage.GetRefFormId(aDropId);
    TESBoundObject* pObject = ResolveDroppedObject(acData.Item);

    if (cachedRefFormId)
    {
        if (TESForm* pForm = TESForm::GetById(*cachedRefFormId))
        {
            if (TESObjectREFR* pRef = Cast<TESObjectREFR>(pForm))
            {
                if (pObject && pRef->baseForm != pObject)
                {
                    spdlog::warn("DropService: cached ref {:X} base mismatch for drop {}, ignoring", *cachedRefFormId, aDropId);
                }
                else if (tryBind(pRef))
                {
                    return true;
                }
            }
        }
    }

    if (acData.ReferenceId)
    {
        if (TESObjectREFR* pRef = GetReferenceById(acData.ReferenceId))
        {
            if (tryBind(pRef))
                return true;
        }
    }

    if (!pObject)
        return false;

    if (TESObjectREFR* pNearby = FindReferenceNear(pObject, acData.Location, kDropSearchRadiusSquared))
        return tryBind(pNearby);

    return false;
}

void DropService::ReconcileCachedDrops(const GameId& acCellId, const GameId& acWorldId, const TiltedPhoques::Vector<uint64_t>& acAuthoritativeDropIds) noexcept
{
    auto cachedDrops = m_dropStorage.GetDropsForCell(acCellId, acWorldId);
    TiltedPhoques::Map<uint64_t, bool> liveIds;
    for (auto id : acAuthoritativeDropIds)
        liveIds[id] = true;

    for (const auto& cached : cachedDrops)
    {
        if (liveIds.find(cached.DropId) != std::end(liveIds))
            continue;

        bool removed = false;
        if (cached.ReferenceId)
            removed = RemoveReferenceById(cached.ReferenceId, "cell sync stale reference id");
        if (cached.RefFormId && !removed)
        {
            if (auto* pForm = TESForm::GetById(cached.RefFormId))
            {
                if (auto* pRef = Cast<TESObjectREFR>(pForm))
                {
                    pRef->Delete();
                    removed = true;
                }
            }
        }

        if (!removed)
        {
            if (TESBoundObject* pObject = ResolveDroppedObject(cached.Item))
            {
                if (TESObjectREFR* pRef = FindReferenceNear(pObject, cached.Location))
                {
                    pRef->Delete();
                    removed = true;
                }
            }
        }

        if (removed)
            spdlog::warn("DropService: removed stale cached drop {} in cell {:X}:{:X}", cached.DropId, acCellId.ModId, acCellId.BaseId);
        else
            spdlog::warn("DropService: cached drop {} missing locally for cell {:X}:{:X}", cached.DropId, acCellId.ModId, acCellId.BaseId);

        m_dropStorage.RemoveCachedDrop(cached.DropId);
    }
}

void DropService::ApplyCreationEngineCellSync(const DropSyncContext& acContext, const TiltedPhoques::Vector<GameId>& acPickedUpRefs) noexcept
{
    if (acPickedUpRefs.empty())
        return;

    if (!IsDropCellLoaded(acContext.CellId, acContext.WorldSpaceId))
        return;

    for (const auto& refId : acPickedUpRefs)
    {
        if (!refId)
            continue;

        if (RemoveReferenceById(refId, "cell sync creation-engine pickup"))
        {
            m_pendingCreationEngineRemovals.erase(refId);
            continue;
        }

        auto& pending = m_pendingCreationEngineRemovals[refId];
        pending.CellId = acContext.CellId;
        pending.WorldSpaceId = acContext.WorldSpaceId;
        pending.RemainingRetries = 30;
    }
}

void DropService::ProcessPendingCreationEngineRemovals() noexcept
{
    if (m_pendingCreationEngineRemovals.empty())
        return;

    TiltedPhoques::Vector<GameId> toErase;
    toErase.reserve(m_pendingCreationEngineRemovals.size());

    for (auto& [refId, pending] : m_pendingCreationEngineRemovals)
    {
        if (!refId || pending.RemainingRetries == 0)
        {
            toErase.push_back(refId);
            continue;
        }

        if (pending.CellId && !IsDropCellLoaded(pending.CellId, pending.WorldSpaceId))
            continue;

        if (RemoveReferenceById(refId, "cell sync deferred creation-engine pickup"))
        {
            toErase.push_back(refId);
            continue;
        }

        --pending.RemainingRetries;
        if (pending.RemainingRetries == 0)
            toErase.push_back(refId);
    }

    for (const auto& refId : toErase)
        m_pendingCreationEngineRemovals.erase(refId);
}

GameId DropService::GetPlayerCellId() noexcept
{
    GameId cell{};
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return cell;

    if (auto* pCell = pPlayer->parentCell)
        m_world.GetModSystem().GetServerModId(pCell->formID, cell);

    return cell;
}

GameId DropService::GetPlayerWorldId() noexcept
{
    GameId world{};
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return world;

    if (auto* pWorld = pPlayer->GetWorldSpace())
        m_world.GetModSystem().GetServerModId(pWorld->formID, world);

    return world;
}

bool DropService::IsDropCellLoaded(const GameId& acCellId, const GameId& acWorldId) noexcept
{
    if (!acCellId)
        return false;

    const GameId playerCell = GetPlayerCellId();
    if (playerCell && playerCell == acCellId)
        return true;

    const GameId playerWorld = GetPlayerWorldId();

    // Interior: only the current cell is treated as loaded for drops.
    if (!playerWorld)
        return false;

    if (acWorldId && acWorldId != playerWorld)
        return false;

    const uint32_t cellFormId = m_world.GetModSystem().GetGameId(acCellId);
    if (!cellFormId)
        return false;

    TES* pTes = TES::Get();
    if (!pTes || !pTes->cells || !pTes->cells->arr)
        return false;

    const int dimension = pTes->cells->dimension;
    if (dimension <= 0)
        return false;

    const int cellCount = dimension * dimension;
    for (int i = 0; i < cellCount; ++i)
    {
        TESObjectCELL* pCell = pTes->cells->arr[i];
        if (pCell && pCell->formID == cellFormId)
            return true;
    }

    return false;
}

bool DropService::IsDropLocallyActive(uint64_t aDropId, const GameId& acReferenceId) const noexcept
{
    if (aDropId != 0)
    {
        if (m_grabbedDrops.find(aDropId) != m_grabbedDrops.end())
            return true;
        if (m_dropPhysicsCooldowns.find(aDropId) != m_dropPhysicsCooldowns.end())
            return true;
    }

    if (acReferenceId)
    {
        if (m_grabbedReferences.find(acReferenceId) != m_grabbedReferences.end())
            return true;
        if (m_referencePhysicsCooldowns.find(acReferenceId) != m_referencePhysicsCooldowns.end())
            return true;
    }

    return false;
}

void DropService::RequestCellSync(bool aIncludeDiscovery) noexcept
{
    const GameId cellId = GetPlayerCellId();
    if (!cellId)
        return;

    const GameId worldId = GetPlayerWorldId();
    QueueDropSync(cellId, worldId, aIncludeDiscovery);
}

TiltedPhoques::Vector<RequestDroppedItems::DiscoveryEntry> DropService::BuildDiscoveryEntries(const GameId& acCellId, const GameId& acWorldId) noexcept
{
    TiltedPhoques::Vector<RequestDroppedItems::DiscoveryEntry> entries;
    if (!acCellId)
        return entries;

    const uint32_t cellFormId = m_world.GetModSystem().GetGameId(acCellId);
    if (!cellFormId)
        return entries;

    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(cellFormId));
    if (!pCell)
        return entries;

    TiltedPhoques::Vector<FormType> formTypes = {
        FormType::Weapon,
        FormType::Armor,
        FormType::Ammo,
        FormType::Ingredient,
        FormType::Alchemy,
        FormType::Book,
        FormType::Scroll,
        FormType::Note,
        FormType::SoulGem,
        FormType::KeyMaster,
        FormType::Light,
        FormType::Misc,
        FormType::Apparatus
    };

    const auto references = pCell->GetRefsByFormTypes(formTypes);
    entries.reserve(references.size());

    for (TESObjectREFR* pRef : references)
    {
        if (!IsEligibleServerItemRef(pRef))
            continue;

        GameId referenceId{};
        m_world.GetModSystem().GetServerModId(pRef->formID, referenceId);
        if (!referenceId)
            continue;

        if (DropManager::GetDropIdForReference(referenceId))
            continue;

        if (m_dropStorage.FindDropIdByRefFormId(pRef->formID, acCellId, acWorldId))
            continue;

        RequestDroppedItems::DiscoveryEntry entry{};
        entry.ReferenceId = referenceId;
        entry.CellId = acCellId;
        entry.WorldSpaceId = acWorldId;
        entry.HasLocation = true;
        entry.Location = ToNetVector(pRef->position);
        entry.HasRotation = true;
        entry.Rotation = ToNetDropRotation(pRef->rotation);

        entry.Item.Count = 1;
        m_world.GetModSystem().GetServerModId(pRef->baseForm->formID, entry.Item.BaseId);
        if (!entry.Item.BaseId)
            continue;

        TESObjectREFR::GetItemFromExtraData(entry.Item, pRef->GetExtraDataList());
        if (entry.Item.Count == 0)
            entry.Item.Count = 1;

        entries.push_back(std::move(entry));
    }

    if (!entries.empty())
        spdlog::debug("DropService: queued {} creation-engine discoveries for cell {:X}:{:X}", entries.size(), acCellId.ModId, acCellId.BaseId);

    return entries;
}

void DropService::ForgetLocalDrop(uint64_t aDropId) noexcept
{
    if (aDropId == 0)
        return;

    m_localDrops.erase(aDropId);
    m_grabbedDrops.erase(aDropId);
    m_dropPhysicsCooldowns.erase(aDropId);
    m_dropMoveSyncTimers.erase(aDropId);
}
