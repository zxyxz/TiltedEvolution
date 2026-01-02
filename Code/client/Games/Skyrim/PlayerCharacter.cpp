#include <PlayerCharacter.h>
#include <Games/ActorExtension.h>

#include <Structs/Skyrim/AnimationGraphDescriptor_Master_Behavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_VampireLordBehavior.h>

#include <Games/Overrides.h>

#include <Events/InventoryChangeEvent.h>
#include <Events/PickupDroppedItemEvent.h>
#include <Events/BeastFormChangeEvent.h>
#include <Events/AddExperienceEvent.h>
#include <Events/SetWaypointEvent.h>
#include <Events/RemoveWaypointEvent.h>

#include <World.h>

#include <Games/Skyrim/Forms/ActorValueInfo.h>
#include <Games/ActorExtension.h>
#include <Games/TES.h>
#include <Games/References.h>
#include <Forms/TESWorldSpace.h>

#include <Forms/TESObjectCELL.h>

#include <ModCompat/BehaviorVar.h>
#include <Sync/DropManager.h>
#include <Sync/DropExecutionContext.h>
#include <array>
#include <atomic>
#include <optional>
#include <cmath>
#include <utility>

int32_t PlayerCharacter::LastUsedCombatSkill = -1;

TP_THIS_FUNCTION(TPickUpObject, char, PlayerCharacter, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, bool aUnk2);
TP_THIS_FUNCTION(TSetBeastForm, void, void, void* apUnk1, void* apUnk2, bool aEntering);
TP_THIS_FUNCTION(TAddSkillExperience, void, PlayerCharacter, int32_t aSkill, float aExperience);
TP_THIS_FUNCTION(TCalculateExperience, bool, int32_t, float* aFactor, float* aBonus, float* aUnk1, float* aUnk2);
TP_THIS_FUNCTION(TSetWaypoint, void, PlayerCharacter, NiPoint3* apPosition, TESWorldSpace* apWorldSpace);
TP_THIS_FUNCTION(TRemoveWaypoint, void, PlayerCharacter);

static TPickUpObject* RealPickUpObject = nullptr;
static TSetBeastForm* RealSetBeastForm = nullptr;
static TAddSkillExperience* RealAddSkillExperience = nullptr;
static TCalculateExperience* RealCalculateExperience = nullptr;
static TSetWaypoint* RealSetWaypoint = nullptr;
static TRemoveWaypoint* RealRemoveWaypoint = nullptr;

namespace
{
constexpr float kDropSearchRadiusSquared = 200.f * 200.f;

GameId ResolveReferenceId(TESObjectREFR* apReference) noexcept
{
    GameId reference{};
    if (!apReference)
        return reference;

    World::Get().GetModSystem().GetServerModId(apReference->formID, reference);
    return reference;
}

std::pair<GameId, GameId> ResolveReferenceCellMetadata(TESObjectREFR* apReference) noexcept
{
    GameId cell{};
    GameId world{};

    if (!apReference)
        return {cell, world};

    auto* pCell = apReference->parentCell ? apReference->parentCell : apReference->GetParentCell();
    if (pCell)
    {
        auto& modSystem = World::Get().GetModSystem();
        modSystem.GetServerModId(pCell->formID, cell);
        if (pCell->worldspace)
            modSystem.GetServerModId(pCell->worldspace->formID, world);
    }

    return {cell, world};
}

void PopulatePickupEventFromDrop(uint64_t aDropId, PickupDroppedItemEvent& aEvent) noexcept
{
    if (const auto dropOpt = DropManager::GetServerDrop(aDropId); dropOpt)
    {
        aEvent.HasItemData = true;
        aEvent.Item = dropOpt->Item;
        aEvent.HasLocation = true;
        aEvent.Location = dropOpt->Location;
        aEvent.HasRotation = true;
        aEvent.Rotation = dropOpt->Rotation;
        aEvent.CellId = dropOpt->CellId;
        aEvent.WorldSpaceId = dropOpt->WorldSpaceId;
    }
}

void PopulatePickupEventFromReference(TESObjectREFR* apReference, const Inventory::Entry& acItem, PickupDroppedItemEvent& aEvent) noexcept
{
    if (!apReference)
        return;

    aEvent.HasItemData = true;
    aEvent.Item = acItem;
    aEvent.HasLocation = true;
    aEvent.Location = apReference->position;
    aEvent.HasRotation = true;
    aEvent.Rotation = apReference->rotation;

    const auto cellMeta = ResolveReferenceCellMetadata(apReference);
    aEvent.CellId = cellMeta.first;
    aEvent.WorldSpaceId = cellMeta.second;
    aEvent.ReferenceId = ResolveReferenceId(apReference);
}

constexpr uint32_t kMaxCurrentMapMarkers = 4096;

constexpr std::array<std::ptrdiff_t, 2> kCurrentMapMarkerOffsets = {
    0x4F8,
    0x500,
};

std::atomic<std::ptrdiff_t> s_currentMapMarkersOffset{0};

const GameArray<BSPointerHandle<TESObjectREFR>>* GetCurrentMapMarkersAt(const PlayerCharacter* apPlayer, const std::ptrdiff_t aOffset) noexcept
{
    return reinterpret_cast<const GameArray<BSPointerHandle<TESObjectREFR>>*>(
        reinterpret_cast<const uint8_t*>(apPlayer) + aOffset);
}

bool IsPlausibleCurrentMapMarkersArray(const GameArray<BSPointerHandle<TESObjectREFR>>& acMarkers) noexcept
{
    if (acMarkers.length > acMarkers.capacity)
        return false;

    if (acMarkers.capacity > kMaxCurrentMapMarkers)
        return false;

    if (acMarkers.length > kMaxCurrentMapMarkers)
        return false;

    if (acMarkers.length == 0)
        return true;

    if (!acMarkers.data)
        return false;

    const auto dataPtr = reinterpret_cast<uintptr_t>(acMarkers.data);
    if (dataPtr < 0x10000)
        return false;

    if (dataPtr % alignof(BSPointerHandle<TESObjectREFR>) != 0)
        return false;

    return true;
}

const GameArray<BSPointerHandle<TESObjectREFR>>* ResolveCurrentMapMarkersArray(const PlayerCharacter* apPlayer) noexcept
{
    if (!apPlayer)
        return nullptr;

    if (const auto cached = s_currentMapMarkersOffset.load(std::memory_order_relaxed); cached != 0)
    {
        const auto* pMarkers = GetCurrentMapMarkersAt(apPlayer, cached);
        if (IsPlausibleCurrentMapMarkersArray(*pMarkers))
            return pMarkers;

        s_currentMapMarkersOffset.store(0, std::memory_order_relaxed);
    }

    for (const auto offset : kCurrentMapMarkerOffsets)
    {
        const auto* pMarkers = GetCurrentMapMarkersAt(apPlayer, offset);
        if (!IsPlausibleCurrentMapMarkersArray(*pMarkers))
            continue;

        const bool shouldCache = pMarkers->length != 0 || pMarkers->capacity != 0 || pMarkers->data != nullptr;
        if (shouldCache)
        {
            s_currentMapMarkersOffset.store(offset, std::memory_order_relaxed);

            static std::atomic_bool s_logged{false};
            if (!s_logged.exchange(true))
                spdlog::info("Using currentMapMarkers offset 0x{:X} (len={}, cap={})", offset, pMarkers->length, pMarkers->capacity);
        }

        return pMarkers;
    }

    static std::atomic_bool s_loggedFailure{false};
    if (!s_loggedFailure.exchange(true))
        spdlog::warn("Failed to resolve currentMapMarkers array (fast travel sync will be limited)");

    return nullptr;
}
}

PlayerCharacter* PlayerCharacter::Get() noexcept
{
    POINTER_SKYRIMSE(PlayerCharacter*, s_character, 401069);

    return *s_character.Get();
}

const GameArray<TintMask*>& PlayerCharacter::GetTints() const noexcept
{
    if (overlayTints)
        return *overlayTints;

    return baseTints;
}

void PlayerCharacter::SetGodMode(bool aSet) noexcept
{
    POINTER_SKYRIMSE(bool, bGodMode, 404238);
    *bGodMode.Get() = aSet;
}

void PlayerCharacter::SetDifficulty(const int32_t aDifficulty, bool aForceUpdate, bool aExpectGameDataLoaded) noexcept
{
    if (aDifficulty > 5 || aDifficulty < 0)
        return;

    int32_t* difficultySetting = Settings::GetDifficulty();
    *difficultySetting = difficulty = aDifficulty;
}

void PlayerCharacter::AddSkillExperience(int32_t aSkill, float aExperience) noexcept
{
    Skills::Skill skill = Skills::GetSkillFromActorValue(aSkill);
    float oldExperience = GetSkillExperience(skill);

    ScopedExperienceOverride _;

    TiltedPhoques::ThisCall(RealAddSkillExperience, this, aSkill, aExperience);

    float newExperience = GetSkillExperience(skill);
    float deltaExperience = newExperience - oldExperience;

    spdlog::debug("Added {} experience to skill {}", deltaExperience, aSkill);
}

NiPoint3 PlayerCharacter::RespawnPlayer() noexcept
{
    // Make bleedout state recoverable
    SetNoBleedoutRecovery(false);

    DispelAllSpells();

    // Reset health to max
    // TODO(cosideci): there's a cleaner way to do this
    ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, 1000000);

    TESObjectCELL* pCell = nullptr;

    if (GetWorldSpace())
    {
        // TP to Whiterun temple when killed in world space
        TES* pTes = TES::Get();
        pCell = ModManager::Get()->GetCellFromCoordinates(pTes->centerGridX, pTes->centerGridY, GetWorldSpace(), false);
    }
    else
    {
        // TP to start of cell when killed in an interior
        pCell = GetParentCell();
    }

    NiPoint3 pos{};
    NiPoint3 rot{};
    pCell->GetCOCPlacementInfo(&pos, &rot, true);

    MoveTo(pCell, pos);

    // Make bleedout state unrecoverable again for when the player goes down the next time
    SetNoBleedoutRecovery(true);

    return pos;
}

void PlayerCharacter::PayCrimeGoldToAllFactions() noexcept
{
    // Yes, yes, this isn't great, but there's no "pay fines everywhere" function
    const uint32_t crimeFactionIds[]{0x28170, 0x267E3, 0x29DB0, 0x2816D, 0x2816e, 0x2816C, 0x2816B, 0x267EA, 0x2816F, 0x4018279};

    for (uint32_t crimeFactionId : crimeFactionIds)
    {
        TESFaction* pCrimeFaction = Cast<TESFaction>(TESForm::GetById(crimeFactionId));
        if (!pCrimeFaction)
        {
            spdlog::error("This isn't a crime faction! {:X}", crimeFactionId);
            continue;
        }

        PayFine(pCrimeFaction, false, false);
    }
}

void PlayerCharacter::SetWaypoint(NiPoint3* apPosition, TESWorldSpace* apWorldSpace) noexcept
{
    return TiltedPhoques::ThisCall(RealSetWaypoint, this, apPosition, apWorldSpace);
}

void PlayerCharacter::RemoveWaypoint() noexcept
{
    return TiltedPhoques::ThisCall(RealRemoveWaypoint, this);
}

GameArray<BSPointerHandle<TESObjectREFR>>* PlayerCharacter::GetCurrentMapMarkers() noexcept
{
    return const_cast<GameArray<BSPointerHandle<TESObjectREFR>>*>(
        static_cast<const PlayerCharacter*>(this)->GetCurrentMapMarkers());
}

const GameArray<BSPointerHandle<TESObjectREFR>>* PlayerCharacter::GetCurrentMapMarkers() const noexcept
{
    return ResolveCurrentMapMarkersArray(this);
}

char TP_MAKE_THISCALL(HookPickUpObject, PlayerCharacter, TESObjectREFR* apObject, int32_t aCount, bool aUnk1, bool aUnk2)
{
    const bool isRemotePickup = DropExecution::GetCurrentMode() == DropExecution::Mode::RemotePickup;
    const bool isConnected = World::Get().GetTransport().IsConnected();
    std::optional<uint64_t> dropId{};

    if (apObject)
    {
        auto handle = apObject->GetHandle();
        if (handle && handle.handle.iBits)
            dropId = DropManager::GetDropIdForHandle(handle.handle.iBits);

        if (!dropId)
        {
            GameId objectId{};
            World::Get().GetModSystem().GetServerModId(apObject->baseForm->formID, objectId);
            if (objectId.ModId || objectId.BaseId)
                dropId = DropManager::FindDropBySignature(objectId, apObject->position, kDropSearchRadiusSquared);
        }
    }

    Inventory::Entry fallbackItem{};
    const bool hasReferenceObject = apObject != nullptr;
    if (!dropId && apObject)
    {
        auto& modSystem = World::Get().GetModSystem();
        modSystem.GetServerModId(apObject->baseForm->formID, fallbackItem.BaseId);
        fallbackItem.Count = aCount;

        if (apObject->GetExtraDataList() && !ScopedExtraDataOverride::IsOverriden())
        {
            ScopedExtraDataOverride _;
            const int32_t engineCount = fallbackItem.Count;
            apThis->GetItemFromExtraData(fallbackItem, apObject->GetExtraDataList());
            fallbackItem.Count = engineCount;
        }
    }

    if (!isRemotePickup && isConnected)
    {
        std::optional<PickupDroppedItemEvent> pickupEvent{};

        if (dropId)
        {
            pickupEvent.emplace(apThis->formID, *dropId);
            PopulatePickupEventFromDrop(*dropId, *pickupEvent);
        }
        else if (hasReferenceObject)
        {
            pickupEvent.emplace(apThis->formID, 0);
            PopulatePickupEventFromReference(apObject, fallbackItem, *pickupEvent);
        }

        if (pickupEvent && apObject)
            pickupEvent->ReferenceFormId = apObject->formID;

        if (pickupEvent)
            World::Get().GetRunner().Trigger(*pickupEvent);
    }

    ScopedInventoryOverride _;

    if (!isRemotePickup && isConnected)
    {
        DropExecution::Scope scope(DropExecution::Mode::LocalPickup, apThis->formID, dropId.value_or(0));
        return TiltedPhoques::ThisCall(RealPickUpObject, apThis, apObject, aCount, aUnk1, aUnk2);
    }

    return TiltedPhoques::ThisCall(RealPickUpObject, apThis, apObject, aCount, aUnk1, aUnk2);
}

void TP_MAKE_THISCALL(HookSetBeastForm, void, void* apUnk1, void* apUnk2, bool aEntering)
{
    if (!aEntering)
    {
        PlayerCharacter::Get()->GetExtension()->GraphDescriptorHash = BehaviorVar::GetHumanoidHash();
        World::Get().GetRunner().Trigger(BeastFormChangeEvent());
    }

    TiltedPhoques::ThisCall(RealSetBeastForm, apThis, apUnk1, apUnk2, aEntering);
}

void TP_MAKE_THISCALL(HookAddSkillExperience, PlayerCharacter, int32_t aSkill, float aExperience)
{
    // TODO: armor skills? sneak?
    static const Set<int32_t> combatSkills{ActorValueInfo::kAlteration, ActorValueInfo::kConjuration, ActorValueInfo::kDestruction, ActorValueInfo::kIllusion, ActorValueInfo::kRestoration, ActorValueInfo::kOneHanded, ActorValueInfo::kTwoHanded, ActorValueInfo::kMarksman, ActorValueInfo::kBlock};

    Skills::Skill skill = Skills::GetSkillFromActorValue(aSkill);
    float oldExperience = apThis->GetSkillExperience(skill);

    TiltedPhoques::ThisCall(RealAddSkillExperience, apThis, aSkill, aExperience);

    float newExperience = apThis->GetSkillExperience(skill);
    float deltaExperience = newExperience - oldExperience;

    spdlog::debug("Skill (AVI): {}, experience: {}", aSkill, deltaExperience);

    if (combatSkills.contains(aSkill))
    {
        spdlog::debug("Set new last used combat skill to {}.", aSkill);
        PlayerCharacter::LastUsedCombatSkill = aSkill;

        World::Get().GetRunner().Trigger(AddExperienceEvent(deltaExperience));
    }
}

bool TP_MAKE_THISCALL(HookCalculateExperience, int32_t, float* aFactor, float* aBonus, float* aUnk1, float* aUnk2)
{
    bool result = TiltedPhoques::ThisCall(RealCalculateExperience, apThis, aFactor, aBonus, aUnk1, aUnk2);

    if (ScopedExperienceOverride::IsOverriden())
    {
        *aFactor = 1.f;
        *aBonus = 0.f;
    }

    return result;
}

void TP_MAKE_THISCALL(HookSetWaypoint, PlayerCharacter, NiPoint3* apPosition, TESWorldSpace* apWorldSpace)
{
    Vector3_NetQuantize position{};
    position.x = apPosition->x;
    position.y = apPosition->y;
    position.z = apPosition->z;

    World::Get().GetRunner().Trigger(SetWaypointEvent(position, apWorldSpace ? apWorldSpace->formID : 0));

    return TiltedPhoques::ThisCall(RealSetWaypoint, apThis, apPosition, apWorldSpace);
}

void TP_MAKE_THISCALL(HookRemoveWaypoint, PlayerCharacter)
{
    World::Get().GetRunner().Trigger(RemoveWaypointEvent());

    return TiltedPhoques::ThisCall(RealRemoveWaypoint, apThis);
}

static TiltedPhoques::Initializer s_playerCharacterHooks(
    []()
    {
        POINTER_SKYRIMSE(TPickUpObject, s_pickUpObject, 40533);
        POINTER_SKYRIMSE(TSetBeastForm, s_setBeastForm, 55497);
        POINTER_SKYRIMSE(TAddSkillExperience, s_addSkillExperience, 40488);
        POINTER_SKYRIMSE(TCalculateExperience, s_calculateExperience, 27244);
        POINTER_SKYRIMSE(TSetWaypoint, s_setWaypoint, 40535);
        POINTER_SKYRIMSE(TRemoveWaypoint, s_removeWaypoint, 40536);

        RealPickUpObject = s_pickUpObject.Get();
        RealSetBeastForm = s_setBeastForm.Get();
        RealAddSkillExperience = s_addSkillExperience.Get();
        RealCalculateExperience = s_calculateExperience.Get();
        RealSetWaypoint = s_setWaypoint.Get();
        RealRemoveWaypoint = s_removeWaypoint.Get();

        TP_HOOK(&RealPickUpObject, HookPickUpObject);
        TP_HOOK(&RealSetBeastForm, HookSetBeastForm);
        TP_HOOK(&RealAddSkillExperience, HookAddSkillExperience);
        TP_HOOK(&RealCalculateExperience, HookCalculateExperience);
        TP_HOOK(&RealSetWaypoint, HookSetWaypoint);
        TP_HOOK(&RealRemoveWaypoint, HookRemoveWaypoint);
    });
