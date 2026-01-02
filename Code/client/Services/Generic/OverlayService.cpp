#include <TiltedOnlinePCH.h>

#include <Services/OverlayService.h>

#include <OverlayApp.hpp>

#include <D3D11Hook.hpp>
#include <OverlayRenderHandlerD3D11.hpp>

#include <Systems/RenderSystemD3D11.h>

#include <World.h>

#include <Services/OverlayClient.h>
#include <Services/TransportService.h>

#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyPlayerList.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerProfileImage.h>
#include <Messages/NotifyPlayerDialogue.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifyPlayerCellChanged.h>
#include <Messages/NotifyTeleport.h>
#include <Messages/NotifyTeleportCountdown.h>
#include <Messages/NotifyTeleportRequest.h>
#include <Messages/RequestPlayerHealthUpdate.h>
#include <Messages/NotifyPlayerHealthUpdate.h>
#include <Messages/NotifyCommandList.h>
#include <Messages/NotifyPlayEmote.h>
#include <Messages/NotifyCancelEmote.h>
#include <Messages/CancelEmoteRequest.h>

#include <DefaultObjectManager.h>
#include <Structs/GridCellCoords.h>
#include <EquipManager.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/ConnectionErrorEvent.h>
#include <Events/UpdateEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>

#include <PlayerCharacter.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>
#include <Games/ActorExtension.h>
#include <AI/Movement/PlayerControls.h>
#include <Services/OverlayClient.h>
#include <Misc/BSFixedString.h>
#include <Games/Skyrim/Interface/UI.h>
#include <Components.h>
#include <client/Utils.h>
#include <cstring>
#include <string>
#include <string_view>
#include <chrono>

using TiltedPhoques::OverlayRenderHandler;
using TiltedPhoques::OverlayRenderHandlerD3D11;

namespace
{
bool IsAnyMenuOpen() noexcept
{
    UI* pUI = UI::Get();
    if (!pUI)
        return false;

    for (auto* pMenu : pUI->menuStack)
    {
        if (!pMenu)
            continue;

        const BSFixedString* pName = pUI->LookupMenuNameByInstance(pMenu);
        if (pName && (std::strcmp(pName->AsAscii(), "HUD Menu") == 0 || std::strcmp(pName->AsAscii(), "HUDMenu") == 0))
            continue;

        return true;
    }

    return false;
}

bool TryGetLocalServerId(uint32_t& aOutId) noexcept
{
    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return false;

    auto view = World::Get().view<FormIdComponent, LocalComponent>();
    for (entt::entity entity : view)
    {
        if (view.get<FormIdComponent>(entity).Id != pPlayer->formID)
            continue;

        aOutId = view.get<LocalComponent>(entity).Id;
        return true;
    }

    return false;
}
}

struct D3D11RenderProvider final : OverlayApp::RenderProvider, OverlayRenderHandlerD3D11::Renderer
{
    explicit D3D11RenderProvider(RenderSystemD3D11* apRenderSystem)
        : m_pRenderSystem(apRenderSystem)
    {
    }

    OverlayRenderHandler* Create() override
    {
        auto* pHandler = new OverlayRenderHandlerD3D11(this);
        pHandler->SetVisible(true);

        return pHandler;
    }

    [[nodiscard]] HWND GetWindow() override { return m_pRenderSystem->GetWindow(); }

    [[nodiscard]] IDXGISwapChain* GetSwapChain() const noexcept override { return m_pRenderSystem->GetSwapChain(); }

private:
    RenderSystemD3D11* m_pRenderSystem;
};

String GetCellName(const GameId& aWorldSpaceId, const GameId& aCellId) noexcept
{
    auto& modSystem = World::Get().GetModSystem();

    String cellName = "UNKNOWN";

    if (aWorldSpaceId)
    {
        const uint32_t worldSpaceId = modSystem.GetGameId(aWorldSpaceId);
        TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(worldSpaceId));
        if (pWorldSpace)
            cellName = pWorldSpace->GetName();
    }
    else
    {
        const uint32_t cellId = modSystem.GetGameId(aCellId);
        TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(cellId));
        if (pCell)
            cellName = pCell->GetName();
    }

    return cellName;
}

float CalculateHealthPercentage(Actor* apActor) noexcept
{
    const float maxHealth = apActor->GetActorPermanentValue(ActorValueInfo::kHealth);
    const float tempModHealth = apActor->healthModifiers.temporaryModifier;

    if (maxHealth == 0.f)
        return 0.f;
    const float health = apActor->GetActorValue(ActorValueInfo::kHealth);

    float percentage = health / (maxHealth + tempModHealth) * 100.f;
    if (percentage < 0.f)
        percentage = 0.f;

    return percentage;
}

OverlayService::OverlayService(World& aWorld, TransportService& transport, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_transport(transport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&OverlayService::OnUpdate>(this);
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&OverlayService::OnConnectedEvent>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&OverlayService::OnDisconnectedEvent>(this);
    m_connectionErrorConnection = aDispatcher.sink<ConnectionErrorEvent>().connect<&OverlayService::OnConnectionError>(this);
    m_chatMessageConnection = aDispatcher.sink<NotifyChatMessageBroadcast>().connect<&OverlayService::OnChatMessageReceived>(this);
    m_playerJoinedConnection = aDispatcher.sink<NotifyPlayerJoined>().connect<&OverlayService::OnPlayerJoined>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&OverlayService::OnPlayerLeft>(this);
    m_playerAvatarConnection = aDispatcher.sink<NotifyPlayerProfileImage>().connect<&OverlayService::OnPlayerProfileImage>(this);
    m_playerDialogueConnection = aDispatcher.sink<NotifyPlayerDialogue>().connect<&OverlayService::OnPlayerDialogue>(this);
    m_playerAddedConnection = m_world.on_destroy<WaitingFor3D>().connect<&OverlayService::OnWaitingFor3DRemoved>(this);
    m_playerRemovedConnection = m_world.on_destroy<PlayerComponent>().connect<&OverlayService::OnPlayerComponentRemoved>(this);
    m_playerLevelConnection = aDispatcher.sink<NotifyPlayerLevel>().connect<&OverlayService::OnPlayerLevel>(this);
    m_cellChangedConnection = aDispatcher.sink<NotifyPlayerCellChanged>().connect<&OverlayService::OnPlayerCellChanged>(this);
    m_teleportRequestConnection = aDispatcher.sink<NotifyTeleportRequest>().connect<&OverlayService::OnNotifyTeleportRequest>(this);
    m_teleportConnection = aDispatcher.sink<NotifyTeleport>().connect<&OverlayService::OnNotifyTeleport>(this);
    m_teleportCountdownConnection = aDispatcher.sink<NotifyTeleportCountdown>().connect<&OverlayService::OnNotifyTeleportCountdown>(this);
    m_playerHealthConnection = aDispatcher.sink<NotifyPlayerHealthUpdate>().connect<&OverlayService::OnNotifyPlayerHealthUpdate>(this);
    m_commandListConnection = aDispatcher.sink<NotifyCommandList>().connect<&OverlayService::OnNotifyCommandList>(this);
    m_playEmoteConnection = aDispatcher.sink<NotifyPlayEmote>().connect<&OverlayService::OnNotifyPlayEmote>(this);
    m_cancelEmoteConnection = aDispatcher.sink<NotifyCancelEmote>().connect<&OverlayService::OnNotifyCancelEmote>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&OverlayService::OnPartyJoinedEvent>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&OverlayService::OnPartyLeftEvent>(this);
}

OverlayService::~OverlayService() noexcept
{
}

void OverlayService::Create(RenderSystemD3D11* apRenderSystem) noexcept
{
    m_pProvider = TiltedPhoques::MakeUnique<D3D11RenderProvider>(apRenderSystem);
    m_pOverlay = new OverlayApp(m_pProvider.get(), new ::OverlayClient(m_transport, m_pProvider->Create()));

    if (!m_pOverlay->Initialize())
    {
        spdlog::error("Overlay could not be initialized");
        if (int32_t exitCode = CefGetExitCode())
        {
            spdlog::critical("CEF failed to initialize, exit code {}. See 'cef_types.h' for description", exitCode);
        }
    }

    m_pOverlay->GetClient()->Create();
}

void OverlayService::Render() noexcept
{
    auto pPlayer = PlayerCharacter::Get();
    bool inGame = pPlayer && pPlayer->GetNiNode();
    if (inGame && !m_inGame)
        SetInGame(true);
    else if (!inGame && m_inGame)
        SetInGame(false);

    m_pOverlay->GetClient()->Render();
}

void OverlayService::Reset() const noexcept
{
    m_pOverlay->GetClient()->Reset();
}

void OverlayService::Reload() noexcept
{
    SetInGame(false);
    SetActive(false);
    GetOverlayApp()->GetClient()->GetBrowser()->Reload();
    Initialize();
    SetInGame(true);
    m_pOverlay->ExecuteAsync("enterGame");
    SetActive(true);
}

void OverlayService::Initialize() noexcept
{
    m_pOverlay->ExecuteAsync("init");
}

void OverlayService::SetActive(bool aActive) noexcept
{
    if (!m_inGame)
        return;
    if (m_active == aActive)
        return;

    m_active = aActive;

    m_pOverlay->ExecuteAsync(m_active ? "activate" : "deactivate");
}

bool OverlayService::GetActive() const noexcept
{
    return m_active;
}

void OverlayService::SetInGame(bool aInGame) noexcept
{
    if (m_inGame == aInGame)
        return;
    m_inGame = aInGame;

    if (m_inGame)
    {
        SetVersion(BUILD_COMMIT);
        m_pOverlay->ExecuteAsync("enterGame");
    }
    else
    {
        m_pOverlay->ExecuteAsync("exitGame");
        // TODO: this does nothing, since m_inGame is false
        SetActive(false);
    }
}

bool OverlayService::GetInGame() const noexcept
{
    return m_inGame;
}

void OverlayService::SetVersion(const std::string& acVersion)
{
    if (!m_pOverlay)
        return;

    auto pArguments = CefListValue::Create();

    pArguments->SetString(0, acVersion);
    m_pOverlay->ExecuteAsync("setVersion", pArguments);
}

void OverlayService::SendSystemMessage(const std::string& acMessage)
{
    if (!m_pOverlay)
        return;

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, kSystemMessage);
    pArguments->SetString(1, acMessage);

    m_pOverlay->ExecuteAsync("message", pArguments);
}

void OverlayService::SetPlayerHealthPercentage(uint32_t aFormId) const noexcept
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    if (!pActor)
    {
        spdlog::error("{}: cannot find actor for form id {:X}", __FUNCTION__, aFormId);
        return;
    }

    float percentage = CalculateHealthPercentage(pActor);

    auto view = m_world.view<FormIdComponent, PlayerComponent>();
    auto entityIt = std::find_if(view.begin(), view.end(), [view, aFormId](auto aEntity) { return view.get<FormIdComponent>(aEntity).Id == aFormId; });

    if (entityIt == view.end())
    {
        spdlog::error("{}: cannot find player entity for form id {:X}", __FUNCTION__, aFormId);
        return;
    }

    const auto& playerComponent = view.get<PlayerComponent>(*entityIt);

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, playerComponent.Id);
    pArguments->SetDouble(1, static_cast<double>(percentage));
    m_pOverlay->ExecuteAsync("setHealth", pArguments);
}

void OverlayService::SetPartyPinsJson(const std::string& aJson) noexcept
{
    if (!m_pOverlay)
        return;

    auto pArguments = CefListValue::Create();
    pArguments->SetString(0, aJson);
    m_pOverlay->ExecuteAsync("setPartyPins", pArguments);
}

void OverlayService::OnUpdate(const UpdateEvent&) noexcept
{
    RunDebugDataUpdates();
    RunPlayerHealthUpdates();
    UpdateRemoteEmoteLoops();

    // Allow cancelling emotes from the keyboard even when the menu is closed.
    const bool cancelRequested = (GetAsyncKeyState(VK_OEM_PERIOD) & 0x8001) != 0;
    const bool allowCancel = g_emoteWheelActive.load() || (!m_active && !IsAnyMenuOpen());
    if (cancelRequested && allowCancel)
    {
        const bool wasEmoting = g_emoteWheelActive.load();
        if (auto* pPlayer = PlayerCharacter::Get())
        {
            if (auto* pExt = pPlayer->GetExtension())
            {
                pExt->LatestAnimation = {};
            }

            BSFixedString stopEvent("IdleForceDefaultState");
            BSFixedString stopInstant("IdleStopInstant");
            pPlayer->SendAnimationEvent(&stopInstant);
            pPlayer->SendAnimationEvent(&stopEvent);
            g_emoteWheelActive.store(false);
            g_emoteEventName.clear();
            g_emoteStartValid.store(false);

            if (wasEmoting && m_transport.IsConnected())
            {
                uint32_t serverId = 0;
                if (TryGetLocalServerId(serverId))
                {
                    CancelEmoteRequest request{};
                    request.ServerId = serverId;
                    m_transport.Send(request);
                }
            }

            // Re-equip current weapons/spells to refresh combat state (prevents stuck hands).
            if (auto* pEquipManager = EquipManager::Get())
            {
                auto& defaults = DefaultObjectManager::Get();

                TESForm* pLeftSpell = pPlayer->magicItems[0];
                TESForm* pRightSpell = pPlayer->magicItems[1];

                TESForm* pLeftWeapon = pLeftSpell ? nullptr : pPlayer->GetEquippedWeapon(0);
                TESForm* pRightWeapon = pRightSpell ? nullptr : pPlayer->GetEquippedWeapon(1);
                TESForm* pTwoHand = (pLeftWeapon && pRightWeapon && pLeftWeapon == pRightWeapon) ? pLeftWeapon : nullptr;

                if (pLeftSpell)
                    pEquipManager->EquipSpell(pPlayer, pLeftSpell, 0);
                else if (pLeftWeapon && !pTwoHand)
                    pEquipManager->Equip(pPlayer, pLeftWeapon, nullptr, 1, defaults.leftEquipSlot, false, true, false, false);

                if (pRightSpell)
                    pEquipManager->EquipSpell(pPlayer, pRightSpell, 1);
                else if (pTwoHand)
                    pEquipManager->Equip(pPlayer, pTwoHand, nullptr, 1, defaults.eitherEquipSlot, false, true, false, false);
                else if (pRightWeapon)
                    pEquipManager->Equip(pPlayer, pRightWeapon, nullptr, 1, defaults.rightEquipSlot, false, true, false, false);

                if (auto* pAmmo = pPlayer->GetEquippedAmmo())
                    pEquipManager->Equip(pPlayer, pAmmo, nullptr, 1, defaults.rightEquipSlot, false, true, false, false);

                if (auto* pShout = pPlayer->equippedShout)
                    pEquipManager->EquipShout(pPlayer, pShout);
            }

            if (m_pOverlay)
            {
                auto pArgs = CefListValue::Create();
                pArgs->SetString(0, "Emote cancelled");
                pArgs->SetInt(1, 2000);
                m_pOverlay->ExecuteAsync("showBanner", pArgs);
            }
        }
    }

    // Keep looping emotes that have a timeout
    if (g_emoteWheelActive.load())
    {
        // Keep looping emotes that have a timeout
        if (!g_emoteEventName.empty() && g_emoteEventName != "IdleForceDefaultState" && g_emoteEventName != "IdleStopInstant")
        {
            static constexpr auto kKeepAlive = std::chrono::seconds(2);
            const auto now = std::chrono::steady_clock::now();
            if (now - g_emoteLastPlayed > kKeepAlive)
            {
                if (auto* pPlayer = PlayerCharacter::Get())
                {
                    if (g_emoteStartValid.load())
                    {
                        // Snap back to the same transform we started from so looping emotes don't drift.
                        pPlayer->position = g_emoteStartPos;
                        pPlayer->SetRotation(g_emoteStartRot.x, g_emoteStartRot.y, g_emoteStartRot.z);
                    }

                    BSFixedString keepAlive(g_emoteEventName.c_str());
                    pPlayer->SendAnimationEvent(&keepAlive);
                    g_emoteLastPlayed = now;
                }
            }
        }
    }
}

void OverlayService::UpdateRemoteEmoteLoops() noexcept
{
    if (m_remoteEmotes.empty())
        return;

    static constexpr auto kKeepAlive = std::chrono::seconds(2);
    const auto now = std::chrono::steady_clock::now();

    for (auto it = m_remoteEmotes.begin(); it != m_remoteEmotes.end();)
    {
        if (it->second.EventName.empty() ||
            it->second.EventName == "IdleForceDefaultState" ||
            it->second.EventName == "IdleStopInstant")
        {
            it = m_remoteEmotes.erase(it);
            continue;
        }

        Actor* pActor = Utils::GetByServerId<Actor>(it->first);
        if (!pActor || pActor == PlayerCharacter::Get())
        {
            it = m_remoteEmotes.erase(it);
            continue;
        }

        if (now - it->second.LastPlayed > kKeepAlive)
        {
            pActor->SetWeaponDrawnEx(false);
            BSFixedString keepAlive(it->second.EventName.c_str());
            pActor->SendAnimationEvent(&keepAlive);
            it->second.LastPlayed = now;
        }

        ++it;
    }
}

void OverlayService::OnConnectedEvent(const ConnectedEvent& acEvent) noexcept
{
    m_pOverlay->ExecuteAsync("connect");

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acEvent.PlayerId);
    m_pOverlay->ExecuteAsync("setLocalPlayerId", pArguments);
}

void OverlayService::OnDisconnectedEvent(const DisconnectedEvent&) noexcept
{
    m_pOverlay->ExecuteAsync("disconnect");
}

void OverlayService::OnWaitingFor3DRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept
{
    const auto* pPlayerComponent = m_world.try_get<PlayerComponent>(aEntity);
    if (!pPlayerComponent)
        return;

    const auto& formIdComponent = m_world.get<FormIdComponent>(aEntity);

    Actor* pActor = Cast<Actor>(TESForm::GetById(formIdComponent.Id));
    if (!pActor)
    {
        spdlog::error("{}: cannot find actor for form id {:X}", __FUNCTION__, formIdComponent.Id);
        return;
    }

    float percentage = CalculateHealthPercentage(pActor);

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, pPlayerComponent->Id);
    pArguments->SetInt(1, static_cast<int>(percentage));

    m_pOverlay->ExecuteAsync("setPlayer3dLoaded", pArguments);
}

void OverlayService::OnPlayerComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) const noexcept
{
    const auto& playerComponent = m_world.get<PlayerComponent>(aEntity);

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, playerComponent.Id);

    m_pOverlay->ExecuteAsync("setPlayer3dUnloaded", pArguments);
}

void OverlayService::OnChatMessageReceived(const NotifyChatMessageBroadcast& acMessage) noexcept
{
    if (!m_pOverlay)
        return;

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, (int)acMessage.MessageType);
    pArguments->SetString(1, acMessage.ChatMessage.c_str());
    pArguments->SetString(2, acMessage.PlayerName.c_str());

    m_pOverlay->ExecuteAsync("message", pArguments);
}

void OverlayService::OnPlayerDialogue(const NotifyPlayerDialogue& acMessage) noexcept
{
    if (!m_pOverlay)
        return;

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, kPlayerDialogue);
    pArguments->SetString(1, acMessage.Text.c_str());
    pArguments->SetString(2, acMessage.Name.c_str());

    m_pOverlay->ExecuteAsync("message", pArguments);
}

void OverlayService::OnConnectionError(const ConnectionErrorEvent& acConnectedEvent) const noexcept
{
    auto pArgs = CefListValue::Create();
    pArgs->SetString(0, acConnectedEvent.ErrorDetail.c_str());
    m_pOverlay->ExecuteAsync("triggerError", pArgs);
}

void OverlayService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acMessage.PlayerId);
    pArguments->SetString(1, acMessage.Username.c_str());
    pArguments->SetInt(2, acMessage.Level);

    String cellName = GetCellName(acMessage.WorldSpaceId, acMessage.CellId);
    pArguments->SetString(3, cellName.c_str());
    pArguments->SetString(4, acMessage.Avatar.c_str());

    m_pOverlay->ExecuteAsync("playerConnected", pArguments);
}

void OverlayService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acMessage.PlayerId);
    pArguments->SetString(1, acMessage.Username.c_str());
    m_pOverlay->ExecuteAsync("playerDisconnected", pArguments);
}

void OverlayService::OnPlayerProfileImage(const NotifyPlayerProfileImage& acMessage) noexcept
{
    if (!m_pOverlay)
        return;

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acMessage.PlayerId);
    pArguments->SetString(1, acMessage.Avatar.c_str());
    m_pOverlay->ExecuteAsync("playerAvatarUpdated", pArguments);
}

void OverlayService::OnPlayerLevel(const NotifyPlayerLevel& acMessage) noexcept
{
    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acMessage.PlayerId);
    pArguments->SetInt(1, acMessage.NewLevel);
    m_pOverlay->ExecuteAsync("setLevel", pArguments);
}

void OverlayService::OnPlayerCellChanged(const NotifyPlayerCellChanged& acMessage) const noexcept
{
    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acMessage.PlayerId);
    String cellName = GetCellName(acMessage.WorldSpaceId, acMessage.CellId);
    pArguments->SetString(1, cellName.c_str());
    m_pOverlay->ExecuteAsync("setCell", pArguments);
}

void OverlayService::OnNotifyTeleportRequest(const NotifyTeleportRequest& acMessage) noexcept
{
    if (!m_pOverlay)
        return;

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acMessage.RequesterId);
    pArguments->SetString(1, acMessage.RequesterName.c_str());
    m_pOverlay->ExecuteAsync("teleportRequest", pArguments);
}

void OverlayService::OnNotifyTeleportCountdown(const NotifyTeleportCountdown& acMessage) noexcept
{
    if (!m_pOverlay)
        return;

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acMessage.TargetPlayerId);
    pArguments->SetString(1, acMessage.TargetName.c_str());
    pArguments->SetInt(2, acMessage.DurationSeconds);
    pArguments->SetBool(3, acMessage.Cancelled);
    pArguments->SetString(4, acMessage.Reason.c_str());
    m_pOverlay->ExecuteAsync("teleportCountdown", pArguments);
}

void OverlayService::OnNotifyTeleport(const NotifyTeleport& acMessage) noexcept
{
    auto& modSystem = m_world.GetModSystem();

    const uint32_t cellId = modSystem.GetGameId(acMessage.CellId);
    TESObjectCELL* pCell = Cast<TESObjectCELL>(TESForm::GetById(cellId));
    if (!pCell)
    {
        const uint32_t worldSpaceId = modSystem.GetGameId(acMessage.WorldSpaceId);
        TESWorldSpace* pWorldSpace = Cast<TESWorldSpace>(TESForm::GetById(worldSpaceId));
        if (pWorldSpace)
        {
            GridCellCoords coordinates = GridCellCoords::CalculateGridCellCoords(acMessage.Position);
            pCell = pWorldSpace->LoadCell(coordinates.X, coordinates.Y);
        }

        if (!pCell)
        {
            spdlog::error("Failed to fetch cell to teleport to.");
            m_world.GetOverlayService().SendSystemMessage("Teleporting to player failed.");
            return;
        }
    }

    PlayerCharacter::Get()->MoveTo(pCell, acMessage.Position);
}

void OverlayService::OnNotifyPlayerHealthUpdate(const NotifyPlayerHealthUpdate& acMessage) noexcept
{
    const float percentage = acMessage.Percentage >= 0.f ? acMessage.Percentage : 0.f;

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, acMessage.PlayerId);
    pArguments->SetDouble(1, static_cast<double>(percentage));
    m_pOverlay->ExecuteAsync("setHealth", pArguments);
}

namespace
{
std::string EscapeJson(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}
} // namespace

void OverlayService::OnNotifyCommandList(const NotifyCommandList& acMessage) noexcept
{
    if (!m_pOverlay)
        return;

    std::string json = "[";
    bool first = true;
    for (const auto& command : acMessage.Commands)
    {
        if (!first)
            json += ",";
        first = false;
        json += "{\"name\":\"";
        json += EscapeJson(command.Name);
        json += "\",\"description\":\"";
        json += EscapeJson(command.Description);
        json += "\"}";
    }
    json += "]";

    auto pArguments = CefListValue::Create();
    pArguments->SetString(0, json);
    m_pOverlay->ExecuteAsync("commandList", pArguments);
}

void OverlayService::OnNotifyPlayEmote(const NotifyPlayEmote& acMessage) noexcept
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    if (!pActor)
    {
        spdlog::warn("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.ServerId);
        return;
    }

    if (pActor == PlayerCharacter::Get())
        return;

    if (acMessage.EventName.empty())
        return;

    pActor->SetWeaponDrawnEx(false);
    BSFixedString eventName(acMessage.EventName.c_str());
    pActor->SendAnimationEvent(&eventName);

    auto& state = m_remoteEmotes[acMessage.ServerId];
    state.EventName = acMessage.EventName;
    state.LastPlayed = std::chrono::steady_clock::now();
}

void OverlayService::OnNotifyCancelEmote(const NotifyCancelEmote& acMessage) noexcept
{
    Actor* pActor = Utils::GetByServerId<Actor>(acMessage.ServerId);
    if (!pActor)
    {
        spdlog::warn("{}: could not find actor server id {:X}", __FUNCTION__, acMessage.ServerId);
        return;
    }

    if (pActor == PlayerCharacter::Get())
        return;

    BSFixedString stopInstant("IdleStopInstant");
    BSFixedString stopEvent("IdleForceDefaultState");
    pActor->SendAnimationEvent(&stopInstant);
    pActor->SendAnimationEvent(&stopEvent);
    m_remoteEmotes.erase(acMessage.ServerId);
}

void OverlayService::OnPartyJoinedEvent(const PartyJoinedEvent& acEvent) noexcept
{
    if (acEvent.IsLeader)
        m_world.GetOverlayService().GetOverlayApp()->ExecuteAsync("partyCreated");
}

void OverlayService::OnPartyLeftEvent(const PartyLeftEvent& acEvent) noexcept
{
    m_world.GetOverlayService().GetOverlayApp()->ExecuteAsync("partyLeft");
}

void OverlayService::RunDebugDataUpdates() noexcept
{
    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 1000ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    auto internalStats = m_transport.GetStatistics();
    auto steamStats = m_transport.GetConnectionStatus();

    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, static_cast<int32_t>(steamStats.m_flOutPacketsPerSec));
    pArguments->SetInt(1, static_cast<int32_t>(steamStats.m_flInPacketsPerSec));
    pArguments->SetInt(2, steamStats.m_nPing);
    pArguments->SetInt(3, 0);
    pArguments->SetInt(4, internalStats.SentBytes);
    pArguments->SetInt(5, internalStats.RecvBytes);

    m_pOverlay->ExecuteAsync("debugData", pArguments);
}

// TODO(cosideci): this whole thing is a really hacky solution to
// health sync code being somewhat broken for players.
void OverlayService::RunPlayerHealthUpdates() noexcept
{
    if (!m_transport.IsConnected() || !m_world.GetPartyService().IsInParty())
        return;

    static std::chrono::steady_clock::time_point lastSendTimePoint;
    constexpr auto cDelayBetweenUpdates = 500ms;

    const auto now = std::chrono::steady_clock::now();
    if (now - lastSendTimePoint < cDelayBetweenUpdates)
        return;

    lastSendTimePoint = now;

    static float s_previousPercentage = -1.f;
    static int32_t s_previousLevel = -1;

    auto* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    const float newPercentage = CalculateHealthPercentage(pPlayer);
    const int32_t newLevel = static_cast<int32_t>(pPlayer->GetLevel());

    const bool healthChanged = newPercentage != s_previousPercentage;
    const bool levelChanged = newLevel != s_previousLevel;

    if (!healthChanged && !levelChanged)
        return;

    uint32_t localId = m_transport.GetLocalPlayerId();
    if (localId == 0)
    {
        TryGetLocalServerId(localId);
    }
    const bool canUpdateUi = m_pOverlay && localId != 0;

    if (levelChanged)
    {
        s_previousLevel = newLevel;

        if (canUpdateUi)
        {
            auto pArguments = CefListValue::Create();
            pArguments->SetInt(0, localId);
            pArguments->SetInt(1, newLevel);
            m_pOverlay->ExecuteAsync("setLevel", pArguments);
        }
    }

    if (healthChanged)
    {
        s_previousPercentage = newPercentage;

        if (canUpdateUi)
        {
            auto pArguments = CefListValue::Create();
            pArguments->SetInt(0, localId);
            pArguments->SetDouble(1, static_cast<double>(newPercentage));
            m_pOverlay->ExecuteAsync("setHealth", pArguments);
        }

        RequestPlayerHealthUpdate request{};
        request.Percentage = newPercentage;

        m_transport.Send(request);
    }
}
