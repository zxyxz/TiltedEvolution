#include <Services/PlayerService.h>

#include <World.h>
#include <OverlayApp.hpp>

#include <cmath>
#include <DInputHook.hpp>

#include <Events/UpdateEvent.h>
#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/GridCellChangeEvent.h>
#include <Events/CellChangeEvent.h>
#include <Events/PlayerDialogueEvent.h>
#include <Events/PlayerLevelEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/BeastFormChangeEvent.h>
#include <Events/EquipmentChangeEvent.h>

#include <Messages/PlayerRespawnRequest.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/ShiftGridCellRequest.h>
#include <Messages/EnterExteriorCellRequest.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/PlayerDialogueRequest.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/PartyMemberDownedRequest.h>

#include <Structs/ServerSettings.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESGlobal.h>
#include <Games/Overrides.h>
#include <Games/References.h>
#include <AI/AIProcess.h>
#include <EquipManager.h>
#include <DefaultObjectManager.h>
#include <Forms/TESRace.h>
#include <Services/SyncModeService.h>

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_dispatcher(aDispatcher)
    , m_transport(aTransport)
{
    m_updateConnection = m_dispatcher.sink<UpdateEvent>().connect<&PlayerService::OnUpdate>(this);
    m_connectedConnection = m_dispatcher.sink<ConnectedEvent>().connect<&PlayerService::OnConnected>(this);
    m_disconnectedConnection = m_dispatcher.sink<DisconnectedEvent>().connect<&PlayerService::OnDisconnected>(this);
    m_settingsConnection = m_dispatcher.sink<ServerSettings>().connect<&PlayerService::OnServerSettingsReceived>(this);
    m_notifyRespawnConnection = m_dispatcher.sink<NotifyPlayerRespawn>().connect<&PlayerService::OnNotifyPlayerRespawn>(this);
    m_gridCellChangeConnection = m_dispatcher.sink<GridCellChangeEvent>().connect<&PlayerService::OnGridCellChangeEvent>(this);
    m_cellChangeConnection = m_dispatcher.sink<CellChangeEvent>().connect<&PlayerService::OnCellChangeEvent>(this);
    m_playerDialogueConnection = m_dispatcher.sink<PlayerDialogueEvent>().connect<&PlayerService::OnPlayerDialogueEvent>(this);
    m_playerLevelConnection = m_dispatcher.sink<PlayerLevelEvent>().connect<&PlayerService::OnPlayerLevelEvent>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&PlayerService::OnPartyJoinedEvent>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&PlayerService::OnPartyLeftEvent>(this);
}

void PlayerService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunRespawnUpdates(acEvent.Delta);
    RunPostDeathUpdates(acEvent.Delta);
    RunDifficultyUpdates();
    RunLevelUpdates();
    RunBeastFormDetection();
}

void PlayerService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    // TODO: SkyrimTogether.esm
    TESGlobal* pKillMove = Cast<TESGlobal>(TESForm::GetById(0x100F19));
    pKillMove->f = 0.f;

    TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
    if (m_world.GetSyncModeService().GetLocalMode() != SyncMode::Ghost)
        pWorldEncountersEnabled->f = 0.f;
}

void PlayerService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    PlayerCharacter::Get()->SetDifficulty(m_previousDifficulty);
    m_serverDifficulty = m_previousDifficulty = 6;

    ToggleDeathSystem(false);

    TESGlobal* pKillMove = Cast<TESGlobal>(TESForm::GetById(0x100F19));
    pKillMove->f = 1.f;

    // Restore to the default value (150 in skyrim, 175 in fallout 4)
    float* greetDistance = Settings::GetGreetDistance();
    *greetDistance = 150.f;

    TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
    pWorldEncountersEnabled->f = 1.f;
}

void PlayerService::OnServerSettingsReceived(const ServerSettings& acSettings) noexcept
{
    m_previousDifficulty = *Settings::GetDifficulty();
    PlayerCharacter::Get()->SetDifficulty(acSettings.Difficulty);
    m_serverDifficulty = acSettings.Difficulty;

    if (!acSettings.GreetingsEnabled)
    {
        float* greetDistance = Settings::GetGreetDistance();
        *greetDistance = 0.f;
    }

    ToggleDeathSystem(acSettings.DeathSystemEnabled);
}

void PlayerService::OnNotifyPlayerRespawn(const NotifyPlayerRespawn& acMessage) const noexcept
{
    PlayerCharacter::Get()->PayGold(acMessage.GoldLost);

    std::string message = fmt::format("You died and lost {} gold.", acMessage.GoldLost);
    Utils::ShowHudMessage(String(message));
}

void PlayerService::OnGridCellChangeEvent(const GridCellChangeEvent& acEvent) const noexcept
{
    uint32_t baseId = 0;
    uint32_t modId = 0;

    if (m_world.GetModSystem().GetServerModId(acEvent.WorldSpaceId, modId, baseId))
    {
        ShiftGridCellRequest request;
        request.WorldSpaceId = GameId(modId, baseId);
        request.PlayerCell = acEvent.PlayerCell;
        request.CenterCoords = acEvent.CenterCoords;
        request.Cells = acEvent.Cells;

        m_transport.Send(request);
    }
}

void PlayerService::OnCellChangeEvent(const CellChangeEvent& acEvent) const noexcept
{
    if (acEvent.WorldSpaceId)
    {
        EnterExteriorCellRequest message;
        message.CellId = acEvent.CellId;
        message.WorldSpaceId = acEvent.WorldSpaceId;
        message.CurrentCoords = acEvent.CurrentCoords;

        m_transport.Send(message);
    }
    else
    {
        EnterInteriorCellRequest message;
        message.CellId = acEvent.CellId;

        m_transport.Send(message);
    }
}

void PlayerService::OnPlayerDialogueEvent(const PlayerDialogueEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected() || m_world.GetSyncModeService().GetLocalMode() == SyncMode::Ghost)
        return;

    const auto& partyService = m_world.GetPartyService();
    if (!partyService.IsInParty())
        return;

    PlayerDialogueRequest request{};
    request.Text = acEvent.Text;

    m_transport.Send(request);
}

void PlayerService::OnPlayerLevelEvent(const PlayerLevelEvent& acEvent) const noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerLevelRequest request{};
    request.NewLevel = PlayerCharacter::Get()->GetLevel();

    m_transport.Send(request);
}

void PlayerService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    // TODO: this can be done a bit prettier
    if (acEvent.IsLeader)
    {
        TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
        pWorldEncountersEnabled->f = 1.f;
    }
}

void PlayerService::OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept
{
    // TODO: this can be done a bit prettier
    if (World::Get().GetTransport().IsConnected())
    {
        TESGlobal* pWorldEncountersEnabled = Cast<TESGlobal>(TESForm::GetById(0xB8EC1));
        pWorldEncountersEnabled->f = 0.f;
    }
}

void PlayerService::RunRespawnUpdates(const double acDeltaTime) noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    static bool s_startTimer = false;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer->actorState.IsBleedingOut())
    {
        // Cache equipped items, spells, and shouts so we can restore them after respawn.
        m_cachedLeftHandSpellId = pPlayer->magicItems[0] ? pPlayer->magicItems[0]->formID : 0;
        m_cachedRightHandSpellId = pPlayer->magicItems[1] ? pPlayer->magicItems[1]->formID : 0;

        TESForm* pLeftEquipped = nullptr;
        TESForm* pRightEquipped = nullptr;

        if (m_cachedLeftHandSpellId == 0)
            pLeftEquipped = pPlayer->GetEquippedWeapon(0);
        if (m_cachedRightHandSpellId == 0)
            pRightEquipped = pPlayer->GetEquippedWeapon(1);

        m_cachedLeftHandItemId = pLeftEquipped ? pLeftEquipped->formID : 0;
        m_cachedRightHandItemId = pRightEquipped ? pRightEquipped->formID : 0;

        // Detect two-handed weapons so we can equip them via the either-hand slot.
        if (pLeftEquipped && pRightEquipped && pLeftEquipped == pRightEquipped)
            m_cachedTwoHandedItemId = pLeftEquipped->formID;
        else
            m_cachedTwoHandedItemId = 0;

        if (auto* pAmmo = pPlayer->GetEquippedAmmo())
            m_cachedAmmoId = pAmmo->formID;
        else
            m_cachedAmmoId = 0;

        m_cachedPowerId = pPlayer->equippedShout ? pPlayer->equippedShout->formID : 0;

        s_startTimer = false;
        m_waitingForRespawn = false;
        m_canRespawn = false;
        m_respawnTimer = 0.0;
        return;
    }

    if (!s_startTimer)
    {
        s_startTimer = true;
        m_respawnTimer = 5.0;
        m_waitingForRespawn = true;
        m_canRespawn = false;

        FadeOutGame(true, true, 3.0f, true, 2.0f);

        // If a player dies not by its health reaching 0, getting it up from its bleedout state isn't possible
        // just by setting its health back to max. Therefore, put it to 0.
        if (pPlayer->GetActorValue(ActorValueInfo::kHealth) > 0.f)
            pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, 0);

        pPlayer->PayCrimeGoldToAllFactions();

        // Notify the UI that the player died and show the death screen.
        if (auto* pOverlayApp = m_world.GetOverlayService().GetOverlayApp())
        {
            // Enable overlay input for death screen buttons to work
            TiltedPhoques::DInputHook::Get().SetEnabled(true);
            m_world.GetOverlayService().SetActive(true);
            
            // Show cursor for death screen
            if (auto* pClient = pOverlayApp->GetClient())
            {
                if (auto pRenderer = pClient->GetOverlayRenderHandler())
                {
                    pRenderer->SetCursorVisible(true);
                }
            }
            
            auto pArgs = CefListValue::Create();
            int32_t secondsRemaining = static_cast<int32_t>(std::ceil(m_respawnTimer));
            if (secondsRemaining < 0)
                secondsRemaining = 0;
            pArgs->SetInt(0, secondsRemaining);
            pOverlayApp->ExecuteAsync("showDeathScreen", pArgs);
        }

        PartyMemberDownedRequest downedRequest{};
        downedRequest.IsDowned = true;
        m_transport.Send(downedRequest);
    }

    if (!m_waitingForRespawn)
    {
        // Player has already been revived (e.g. via a healing spell).
        return;
    }

    m_respawnTimer -= acDeltaTime;
    if (m_respawnTimer < 0.0)
        m_respawnTimer = 0.0;

    // Update countdown on the death screen.
    if (auto* pOverlayApp = m_world.GetOverlayService().GetOverlayApp())
    {
        auto pArgs = CefListValue::Create();
        int32_t secondsRemaining = static_cast<int32_t>(std::ceil(m_respawnTimer));
        if (secondsRemaining < 0)
            secondsRemaining = 0;
        pArgs->SetInt(0, secondsRemaining);
        pOverlayApp->ExecuteAsync("updateDeathTimer", pArgs);
    }

    if (!m_canRespawn && m_respawnTimer <= 0.0)
    {
        m_canRespawn = true;

        // Enable the respawn button on the death screen.
        if (auto* pOverlayApp = m_world.GetOverlayService().GetOverlayApp())
        {
            auto pArgs = CefListValue::Create();
            pOverlayApp->ExecuteAsync("enableRespawnButton", pArgs);
        }
    }
    
    // Check if we need to respawn (triggered by button or heal)
    if (m_shouldRespawnAtEntrance || m_shouldRespawnInPlace)
    {
        if (m_shouldRespawnAtEntrance)
        {
            // Respawn at entrance
            pPlayer->RespawnPlayer();
        }
        else
        {
            // Respawn in place (no teleport)
            pPlayer->SetNoBleedoutRecovery(false);
            pPlayer->DispelAllSpells();
            pPlayer->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, 1000000);
            pPlayer->SetNoBleedoutRecovery(true);
        }

        m_knockdownTimer = 1.5;
        m_knockdownStart = true;

        // Restore cached equipment
        auto* pEquipManager = EquipManager::Get();
        auto& defaultObjects = DefaultObjectManager::Get();

        if (m_cachedLeftHandSpellId)
        {
            if (TESForm* pSpell = TESForm::GetById(m_cachedLeftHandSpellId))
                pEquipManager->EquipSpell(pPlayer, pSpell, 0);
        }
        else if (m_cachedLeftHandItemId && m_cachedTwoHandedItemId == 0)
        {
            if (TESForm* pItem = TESForm::GetById(m_cachedLeftHandItemId))
                pEquipManager->Equip(pPlayer, pItem, nullptr, 1, defaultObjects.leftEquipSlot, false, true, false, false);
        }

        if (m_cachedRightHandSpellId)
        {
            if (TESForm* pSpell = TESForm::GetById(m_cachedRightHandSpellId))
                pEquipManager->EquipSpell(pPlayer, pSpell, 1);
        }
        else if (m_cachedTwoHandedItemId)
        {
            if (TESForm* pItem = TESForm::GetById(m_cachedTwoHandedItemId))
                pEquipManager->Equip(pPlayer, pItem, nullptr, 1, defaultObjects.eitherEquipSlot, false, true, false, false);
        }
        else if (m_cachedRightHandItemId)
        {
            if (TESForm* pItem = TESForm::GetById(m_cachedRightHandItemId))
                pEquipManager->Equip(pPlayer, pItem, nullptr, 1, defaultObjects.rightEquipSlot, false, true, false, false);
        }

        if (m_cachedAmmoId)
        {
            if (TESForm* pAmmo = TESForm::GetById(m_cachedAmmoId))
                pEquipManager->Equip(pPlayer, pAmmo, nullptr, 1, defaultObjects.rightEquipSlot, false, true, false, false);
        }

        if (m_cachedPowerId)
        {
            if (TESForm* pShout = TESForm::GetById(m_cachedPowerId))
                pEquipManager->EquipShout(pPlayer, pShout);
        }

        SyncCachedEquipment(pPlayer);

        m_transport.Send(PlayerRespawnRequest());

        PartyMemberDownedRequest revivedRequest{};
        revivedRequest.IsDowned = false;
        m_transport.Send(revivedRequest);

        m_waitingForRespawn = false;
        m_canRespawn = false;
        m_respawnTimer = 0.0;
        m_shouldRespawnAtEntrance = false;
        m_shouldRespawnInPlace = false;
        s_startTimer = false;

        // Hide death screen
        if (auto* pOverlayApp = m_world.GetOverlayService().GetOverlayApp())
        {
            auto pArgs = CefListValue::Create();
            pOverlayApp->ExecuteAsync("hideDeathScreen", pArgs);
            
            // Disable overlay input and hide cursor after death screen closes
            TiltedPhoques::DInputHook::Get().SetEnabled(false);
            m_world.GetOverlayService().SetActive(false);
            
            if (auto* pClient = pOverlayApp->GetClient())
            {
                if (auto pRenderer = pClient->GetOverlayRenderHandler())
                {
                    pRenderer->SetCursorVisible(false);
                }
            }
        }
    }
}

void PlayerService::SyncCachedEquipment(PlayerCharacter* apPlayer) noexcept
{
    if (!apPlayer)
        return;

    auto& defaultObjects = DefaultObjectManager::Get();

    const auto dispatch = [&](uint32_t itemId, TESForm* pSlot, bool isSpell, bool isShout, bool isAmmo = false)
    {
        if (!itemId)
            return;

        if (!TESForm::GetById(itemId))
            return;

        EquipmentChangeEvent evt{};
        evt.ActorId = apPlayer->formID;
        evt.ItemId = itemId;
        evt.EquipSlotId = pSlot ? pSlot->formID : 0;
        evt.IsSpell = isSpell;
        evt.IsShout = isShout;
        evt.IsAmmo = isAmmo;
        if (!isSpell && !isShout)
            evt.Count = 1;

        m_world.GetRunner().Trigger(evt);
    };

    dispatch(m_cachedLeftHandSpellId, defaultObjects.leftEquipSlot, true, false);
    dispatch(m_cachedRightHandSpellId, defaultObjects.rightEquipSlot, true, false);

    if (m_cachedTwoHandedItemId)
    {
        dispatch(m_cachedTwoHandedItemId, defaultObjects.eitherEquipSlot, false, false);
    }
    else
    {
        if (m_cachedLeftHandSpellId == 0)
            dispatch(m_cachedLeftHandItemId, defaultObjects.leftEquipSlot, false, false);
        if (m_cachedRightHandSpellId == 0)
            dispatch(m_cachedRightHandItemId, defaultObjects.rightEquipSlot, false, false);
    }

    dispatch(m_cachedAmmoId, defaultObjects.rightEquipSlot, false, false, true);
    dispatch(m_cachedPowerId, nullptr, false, true);
}

void PlayerService::RequestManualRespawn() noexcept
{
    try
    {
        if (!m_isDeathSystemEnabled)
        {
            spdlog::warn("RequestManualRespawn: Death system not enabled");
            return;
        }

        // Only allow manual respawn while the death screen is active and the cooldown has finished.
        if (!m_waitingForRespawn || !m_canRespawn)
        {
            spdlog::warn("RequestManualRespawn: Not waiting for respawn or button not enabled. waiting={}, canRespawn={}", m_waitingForRespawn, m_canRespawn);
            return;
        }

        // Just set a flag - let RunRespawnUpdates handle the actual respawn
        m_shouldRespawnAtEntrance = true;
    }
    catch (const std::exception& e)
    {
        spdlog::error("RequestManualRespawn: Exception occurred: {}", e.what());
        m_waitingForRespawn = false;
        m_canRespawn = false;
        m_respawnTimer = 0.0;
    }
    catch (...)
    {
        spdlog::error("RequestManualRespawn: Unknown exception occurred");
        m_waitingForRespawn = false;
        m_canRespawn = false;
        m_respawnTimer = 0.0;
    }
}

void PlayerService::OnHealRevive() noexcept
{
    try
    {
        if (!m_isDeathSystemEnabled)
        {
            spdlog::warn("OnHealRevive: Death system not enabled");
            return;
        }

        // Mark that we should respawn the player in place on the next update.
        // RunRespawnUpdates will perform the actual revive (health, flags, UI).
        m_shouldRespawnInPlace = true;
        spdlog::info("OnHealRevive: queued in-place respawn via healing.");
    }
    catch (const std::exception& e)
    {
        spdlog::error("OnHealRevive: Exception occurred: {}", e.what());
    }
    catch (...)
    {
        spdlog::error("OnHealRevive: Unknown exception occurred");
    }
}

// Doesn't seem to respawn quite yet
void PlayerService::RunPostDeathUpdates(const double acDeltaTime) noexcept
{
    if (!m_isDeathSystemEnabled)
        return;

    // If a player dies in ragdoll, it gets stuck.
    // This code ragdolls the player again upon respawning.
    // It also makes the player invincible for 5 seconds.
    if (m_knockdownStart)
    {
        m_knockdownTimer -= acDeltaTime;
        if (m_knockdownTimer <= 0.0)
        {
            PlayerCharacter::SetGodMode(true);
            m_godmodeStart = true;
            m_godmodeTimer = 10.0;

            PlayerCharacter* pPlayer = PlayerCharacter::Get();
            pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);

            FadeOutGame(false, true, 0.5f, true, 2.f);

            m_knockdownStart = false;
        }
    }

    if (m_godmodeStart)
    {
        m_godmodeTimer -= acDeltaTime;
        if (m_godmodeTimer <= 0.0)
        {
            PlayerCharacter::SetGodMode(false);

            m_godmodeStart = false;
        }
    }
}

void PlayerService::RunDifficultyUpdates() const noexcept
{
    if (!m_transport.IsConnected())
        return;

    PlayerCharacter::Get()->SetDifficulty(m_serverDifficulty);
}

void PlayerService::RunLevelUpdates() const noexcept
{
    // The LevelUp hook is kinda weird, so ehh, just check periodically, doesn't really cost anything.

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    static uint16_t oldLevel = PlayerCharacter::Get()->GetLevel();

    uint16_t newLevel = PlayerCharacter::Get()->GetLevel();
    if (newLevel != oldLevel)
    {
        PlayerLevelRequest request{};
        request.NewLevel = newLevel;

        m_transport.Send(request);

        oldLevel = newLevel;
    }
}

void PlayerService::RunBeastFormDetection() const noexcept
{
    static uint32_t lastRaceFormID = 0;
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 250ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer->race)
        return;

    if (pPlayer->race->formID == lastRaceFormID)
        return;

    if (pPlayer->race->formID == 0x200283A || pPlayer->race->formID == 0xCDD84)
        m_world.GetDispatcher().trigger(BeastFormChangeEvent());

    lastRaceFormID = pPlayer->race->formID;
}

void PlayerService::ToggleDeathSystem(bool aSet) noexcept
{
    m_isDeathSystemEnabled = aSet;

    PlayerCharacter::Get()->SetPlayerRespawnMode(aSet);
}
