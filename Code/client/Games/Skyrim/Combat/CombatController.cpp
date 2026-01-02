#include "CombatController.h"
#include "CombatTargetSelector.h"

#include <World.h>
#include <Components.h>
#include <Services/SyncModeService.h>
#include <Games/Skyrim/Actor.h>
#include <Games/ActorExtension.h>

namespace
{
bool IsRemoteGhostActor(Actor* apActor)
{
    if (!apActor || !entt::locator<World>::has_value())
        return false;

    auto* pExt = apActor->GetExtension();
    if (!pExt || !pExt->IsRemotePlayer())
        return false;

    auto& world = World::Get();

    if (world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
        return true;

    auto view = world.view<FormIdComponent, GhostComponent>();
    const auto it = std::find_if(view.begin(), view.end(),
        [view, formId = apActor->formID](entt::entity e)
        { return view.get<FormIdComponent>(e).Id == formId && view.get<GhostComponent>(e).IsGhost; });

    return it != view.end();
}
} // namespace

void ArrayQuickSortRecursiveCombatTargets(GameArray<CombatTargetSelector*>* apArray, uint32_t aiLowIndex,
                                          uint32_t aiHighIndex)
{
    using TArrayQuickSort = void(GameArray<CombatTargetSelector*>* apArray, void* apFunction, uint32_t aiLowIndex, uint32_t aiHighIndex);
    POINTER_SKYRIMSE(TArrayQuickSort, arrayQuickSort, 33285);

    using TSortTargetSelectors = int64_t(int64_t, int64_t);
    POINTER_SKYRIMSE(TSortTargetSelectors, sortTargetSelectors, 33282);

    arrayQuickSort(apArray, sortTargetSelectors, aiLowIndex, aiHighIndex);
}

void CombatController::UpdateTarget()
{
    for (auto* pTargetSelector : targetSelectors)
    {
        if ((pTargetSelector->flags & 2) == 0)
            pTargetSelector->Update();
    }

    if (targetSelectors.length > 1)
        ArrayQuickSortRecursiveCombatTargets(&targetSelectors, 0, targetSelectors.length - 1);

    pActiveTargetSelector = nullptr;
    BSPointerHandle<Actor> newTarget{};

    if (targetSelectors.length)
    {
        for (auto* pTargetSelector : targetSelectors)
        {
            if ((pTargetSelector->flags & 1) != 0 && (pTargetSelector->flags & 2) == 0)
            {
                newTarget = pTargetSelector->SelectTarget();
                if (newTarget)
                {
                    pActiveTargetSelector = pTargetSelector;
                    break;
                }
            }
        }
    }

    // If CombatComponent is attached, don't try to fetch a new target.
    if (Actor* pAttacker = Cast<Actor>(TESObjectREFR::GetByHandle(attackerHandle)))
    {
        const auto view = World::Get().view<FormIdComponent, CombatComponent>();
        const auto it = std::find_if(view.begin(), view.end(), [view, pAttacker](auto entity) { return view.get<FormIdComponent>(entity).Id == pAttacker->formID; });
        if (it != view.end())
            return;
    }

    if (newTarget.handle.iBits == targetHandle)
        return;

    Actor* pNewTarget = Cast<Actor>(TESObjectREFR::GetByHandle(newTarget.handle.iBits));
    SetTarget(pNewTarget);
}

TP_THIS_FUNCTION(TUpdateTarget, void, CombatController);
static TUpdateTarget* RealUpdateTarget = nullptr;

void TP_MAKE_THISCALL(HookUpdateTarget, CombatController)
{
    // Drop ghosted remote targets before running target selection.
    if (Actor* pCurrentTarget = Cast<Actor>(TESObjectREFR::GetByHandle(apThis->targetHandle)))
    {
        if (IsRemoteGhostActor(pCurrentTarget))
        {
            apThis->SetTarget(nullptr);

            if (Actor* pAttacker = Cast<Actor>(TESObjectREFR::GetByHandle(apThis->attackerHandle)))
                pAttacker->SetCombatTargetEx(nullptr);
        }
    }

    TiltedPhoques::ThisCall(RealUpdateTarget, apThis);

    // Ensure any newly chosen target isn't a ghosted remote player.
    if (Actor* pNewTarget = Cast<Actor>(TESObjectREFR::GetByHandle(apThis->targetHandle)))
    {
        if (IsRemoteGhostActor(pNewTarget))
        {
            apThis->SetTarget(nullptr);

            if (Actor* pAttacker = Cast<Actor>(TESObjectREFR::GetByHandle(apThis->attackerHandle)))
                pAttacker->SetCombatTargetEx(nullptr);
        }
    }
}

void CombatController::SetTarget(Actor* apTarget)
{
    TP_THIS_FUNCTION(TSetTarget, void, CombatController, Actor*);
    POINTER_SKYRIMSE(TSetTarget, setTarget, 33235);
    TiltedPhoques::ThisCall(setTarget, this, apTarget);
}

static TiltedPhoques::Initializer s_combatControllerHooks(
    []()
    {
        POINTER_SKYRIMSE(TUpdateTarget, s_updateTarget, 33236);

        RealUpdateTarget = s_updateTarget.Get();

        TP_HOOK(&RealUpdateTarget, HookUpdateTarget);
    });
