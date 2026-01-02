#include <TiltedOnlinePCH.h>

#include <OverlayRenderHandler.hpp>
#include <DInputHook.hpp>

#include <Services/OverlayClient.h>
#include <Services/TransportService.h>
#include <Services/PlayerService.h>
#include <Services/TradeService.h>
#include <Services/NameTagService.h>
#include <Services/OverlayService.h>

#include <OverlayApp.hpp>
#include <Structs/Inventory.h>

#include <DefaultObjectManager.h>
#include <Games/TES.h>
#include <Games/ActorExtension.h>
#include <Games/Animation/ActorMediator.h>
#include <Games/Animation/TESActionData.h>
#include <Games/References.h>
#include <Games/Skyrim/Forms/BGSAction.h>
#include <Games/Skyrim/Forms/TESIdleForm.h>
#include <Games/Skyrim/TESObjectREFR.h>
#include <PlayerCharacter.h>
#include <algorithm>

#include <Messages/SendChatMessageRequest.h>
#include <Messages/TeleportRequest.h>
#include <Messages/TeleportResponse.h>
#include <Messages/PlayerProfileImageUpdateRequest.h>
#include <Messages/PlayEmoteRequest.h>

#include <Events/SetTimeCommandEvent.h>
#include <Services/SyncModeService.h>

#include <World.h>
#include <Components.h>
#include <include/cef_values.h>

extern thread_local bool g_forceAnimation;
extern thread_local bool g_forceAnimationNetwork;
std::atomic<bool> g_emoteWheelActive{false};
std::string g_emoteEventName{};
std::chrono::steady_clock::time_point g_emoteLastPlayed{};
NiPoint3 g_emoteStartPos{};
NiPoint3 g_emoteStartRot{};
std::atomic<bool> g_emoteStartValid{false};

namespace
{
uint32_t ResolveFallbackActionId(Actor* apActor) noexcept
{
    if (!apActor)
        return 0;

    const auto& latest = apActor->GetExtension()->LatestAnimation;
    if (latest.ActionId)
        return latest.ActionId;

    if (auto* pFallbackAction = DefaultObjectManager::Get().someAction)
        return pFallbackAction->formID;

    return 0;
}

void PlayEmoteInternal(const std::string& acEventName)
{
    World::Get().GetRunner().Queue([eventName = acEventName]()
    {
        Actor* pPlayer = PlayerCharacter::Get();
        if (!pPlayer)
            return;

        // Ensure hands/weapons are lowered before playing an emote; raised fists can block graph events.
        pPlayer->SetWeaponDrawnEx(false);

        // Capture the starting transform so any keep-alive replay snaps back to the original pose.
        g_emoteStartPos = pPlayer->position;
        g_emoteStartRot = pPlayer->rotation;
        g_emoteStartValid.store(true);

        if (g_emoteWheelActive.load())
        {
            BSFixedString stopEvent("IdleForceDefaultState");
            pPlayer->SendAnimationEvent(&stopEvent);
        }

        const auto actionId = ResolveFallbackActionId(pPlayer);
        auto* pAction = Cast<BGSAction>(TESForm::GetById(actionId));
        if (!pAction)
        {
            spdlog::warn("Unable to play emote '{}': no usable action found", eventName);
            return;
        }

        if (auto& transport = World::Get().GetTransport(); transport.IsConnected())
        {
            auto view = World::Get().view<FormIdComponent, LocalComponent>();
            const auto it = std::find_if(
                std::begin(view), std::end(view),
                [view, formId = pPlayer->formID](entt::entity entity) { return view.get<FormIdComponent>(entity).Id == formId; });

            if (it != std::end(view))
            {
                PlayEmoteRequest request{};
                request.ServerId = view.get<LocalComponent>(*it).Id;
                request.EventName = eventName;
                transport.Send(request);
            }
        }

        const auto& latest = pPlayer->GetExtension()->LatestAnimation;
        TESIdleForm* pIdle = latest.IdleId ? Cast<TESIdleForm>(TESForm::GetById(latest.IdleId)) : nullptr;

        // Use the same parameter bits the game already produced; fall back to the common idle param (2)
        const uint32_t typeBits = latest.Type ? (latest.Type & 0x3) : 2;
        const bool instantFlag = (latest.Type & 0x4) != 0;

        TESActionData actionData(typeBits, pPlayer, pAction, nullptr);
        actionData.eventName = BSFixedString(eventName.c_str());
        actionData.idleForm = pIdle;
        actionData.someFlag = instantFlag ? 1u : 0u;

        const bool ghosting = World::Get().ctx().contains<SyncModeService>() &&
                              (World::Get().GetSyncModeService().GetLocalMode() == SyncMode::Ghost);

        g_forceAnimation = true;
        g_forceAnimationNetwork = ghosting;
        ActorMediator::Get()->PerformAction(&actionData);
        g_forceAnimationNetwork = false;
        g_forceAnimation = false;

        g_emoteWheelActive.store(true);
        g_emoteEventName = eventName;
        g_emoteLastPlayed = std::chrono::steady_clock::now();

        // If the user chose a stop/clear emote, don't keep a stale transform around.
        if (g_emoteEventName == "IdleForceDefaultState" || g_emoteEventName == "IdleStopInstant")
            g_emoteStartValid.store(false);
        else
        {
            if (auto* pOverlayApp = World::Get().GetOverlayService().GetOverlayApp())
            {
                auto pArgs = CefListValue::Create();
                pArgs->SetString(0, "Press . to cancel emote");
                pArgs->SetInt(1, 0); // Persist until explicitly cleared
                pOverlayApp->ExecuteAsync("showBanner", pArgs);
            }
        }
    });
}
}

OverlayClient::OverlayClient(TransportService& aTransport, TiltedPhoques::OverlayRenderHandler* apHandler)
    : TiltedPhoques::OverlayClient(apHandler)
    , m_transport(aTransport)
{
}

OverlayClient::~OverlayClient() noexcept
{
}

bool OverlayClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message)
{
    if (message->GetName() == "ui-event")
    {
        auto pArguments = message->GetArgumentList();

        auto eventName = pArguments->GetString(0).ToString();
        auto eventArgs = pArguments->GetList(1);

        spdlog::info("ui-event '{}' ({} args)", eventName, eventArgs->GetSize());

#ifndef PUBLIC_BUILD
        LOG(INFO) << "event=ui_event name=" << eventName;
#endif

        if (eventName == "connect")
            ProcessConnectMessage(eventArgs);
        else if (eventName == "disconnect")
            ProcessDisconnectMessage();
        else if (eventName == "revealPlayers")
            ProcessRevealPlayersMessage();
        else if (eventName == "sendMessage")
            ProcessChatMessage(eventArgs);
        else if (eventName == "setTime")
            ProcessSetTimeCommand(eventArgs);
        else if (eventName == "launchParty")
            World::Get().GetPartyService().CreateParty();
        else if (eventName == "leaveParty")
            World::Get().GetPartyService().LeaveParty();
        else if (eventName == "createPartyInvite")
        {
            uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetPartyService().CreateInvite(aPlayerId);
        }
        else if (eventName == "acceptPartyInvite")
        {
            uint32_t aInviterId = eventArgs->GetInt(0);
            // push to main thread because the party service has to check validity of invite thread safely
            World::Get().GetRunner().Queue([aInviterId]() { World::Get().GetPartyService().AcceptInvite(aInviterId); });
        }
        else if (eventName == "kickPartyMember")
        {
            uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetPartyService().KickPartyMember(aPlayerId);
        }
        else if (eventName == "changePartyLeader")
        {
            uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetPartyService().ChangePartyLeader(aPlayerId);
        }
        else if (eventName == "setProfilePicture")
            ProcessSetProfilePicture(eventArgs);
        else if (eventName == "setNameTagMode")
            ProcessSetNameTagMode(eventArgs);
        else if (eventName == "teleportToPlayer" || eventName == "requestTeleport")
            ProcessTeleportRequestMessage(eventArgs);
        else if (eventName == "respondTeleportRequest")
            ProcessTeleportResponseMessage(eventArgs);
        else if (eventName == "toggleDebugUI")
            ProcessToggleDebugUI();
        else if (eventName == "respawnButtonClicked")
            World::Get().GetRunner().Queue([]() { World::Get().ctx().at<PlayerService>().RequestManualRespawn(); });
        else if (eventName == "sendTradeInvite")
        {
            uint32_t aPlayerId = eventArgs->GetInt(0);
            World::Get().GetTradeService().SendInvite(aPlayerId);
        }
        else if (eventName == "respondTradeInvite")
        {
            uint32_t inviterId = eventArgs->GetInt(0);
            bool accept = eventArgs->GetBool(1);
            World::Get().GetTradeService().RespondToInvite(inviterId, accept);
        }
        else if (eventName == "cancelTrade")
        {
            World::Get().GetTradeService().CancelTrade();
        }
        else if (eventName == "setTradeReady")
        {
            bool ready = eventArgs->GetBool(0);
            World::Get().GetTradeService().SetReady(ready);
        }
        else if (eventName == "playEmote")
            ProcessPlayEmote(eventArgs);
        else if (eventName == "updateTradeOffer")
        {
            TiltedPhoques::Vector<TradeService::OfferSelection> selections;
            auto pList = eventArgs->GetList(0);
            if (pList)
            {
                const auto cCount = pList->GetSize();
                selections.reserve(cCount);
                for (size_t i = 0; i < cCount; ++i)
                {
                    TradeService::OfferSelection selection{};

                    if (pList->GetType(static_cast<int>(i)) == VTYPE_DICTIONARY)
                    {
                        auto pEntry = pList->GetDictionary(static_cast<int>(i));
                        if (!pEntry)
                            continue;

                        selection.Index = static_cast<uint32_t>(pEntry->GetInt("index"));
                        selection.Count = pEntry->GetInt("count");
                    }
                    else
                    {
                        auto pEntry = pList->GetList(static_cast<int>(i));
                        if (!pEntry || pEntry->GetSize() < 2)
                            continue;

                        selection.Index = static_cast<uint32_t>(pEntry->GetInt(0));
                        selection.Count = pEntry->GetInt(1);
                    }

                    if (selection.Count <= 0)
                        continue;

                    selections.push_back(selection);
                }
            }
            World::Get().GetTradeService().UpdateOffer(selections);
        }

        return true;
    }

    return false;
}

void OverlayClient::ProcessConnectMessage(CefRefPtr<CefListValue> aEventArgs)
{
    std::string baseIp = aEventArgs->GetString(0);
    if (baseIp == "localhost")
    {
        baseIp = "127.0.0.1";
    }

    const uint16_t port = aEventArgs->GetInt(1) ? static_cast<uint16_t>(aEventArgs->GetInt(1)) : 10578;
    std::string username;
    std::string password;

    if (aEventArgs->GetSize() >= 3)
        username = aEventArgs->GetString(2);
    if (aEventArgs->GetSize() >= 4)
        password = aEventArgs->GetString(3);

    if (aEventArgs->GetSize() >= 5)
        World::Get().GetTransport().SetServerPassword(aEventArgs->GetString(4));
    else
        World::Get().GetTransport().SetServerPassword("");
    World::Get().GetTransport().SetLoginCredentials(username, password);

    std::string endpoint = baseIp + ":" + std::to_string(port);

    World::Get().GetRunner().Queue([endpoint] { World::Get().GetTransport().Connect(endpoint); });
}

void OverlayClient::ProcessDisconnectMessage()
{
    World::Get().GetRunner().Queue([]() { World::Get().GetTransport().Close(); });
}

void OverlayClient::ProcessRevealPlayersMessage()
{
    SetUIVisible(false);
    World::Get().GetMagicService().StartRevealingOtherPlayers();
}

void OverlayClient::ProcessChatMessage(CefRefPtr<CefListValue> aEventArgs)
{
    std::string contents = aEventArgs->GetString(1).ToString();
    if (!contents.empty())
    {
        SendChatMessageRequest messageRequest;
        messageRequest.MessageType = static_cast<ChatMessageType>(aEventArgs->GetInt(0));
        messageRequest.ChatMessage = contents;

        spdlog::info(L"Send chat message of type {}: '{}' ", messageRequest.MessageType, aEventArgs->GetString(1).ToWString());

        m_transport.Send(messageRequest);
    }
}

void OverlayClient::ProcessSetTimeCommand(CefRefPtr<CefListValue> aEventArgs)
{
    const uint8_t hours = static_cast<uint8_t>(aEventArgs->GetInt(0));
    const uint8_t minutes = static_cast<uint8_t>(aEventArgs->GetInt(1));
    const uint32_t senderId = m_transport.GetLocalPlayerId();
    World::Get().GetDispatcher().trigger(SetTimeCommandEvent(hours, minutes, senderId));
}

void OverlayClient::ProcessTeleportRequestMessage(CefRefPtr<CefListValue> aEventArgs)
{
    TeleportRequest request{};
    request.PlayerId = aEventArgs->GetInt(0);

    m_transport.Send(request);
}

void OverlayClient::ProcessTeleportResponseMessage(CefRefPtr<CefListValue> aEventArgs)
{
    TeleportResponse response{};
    response.RequesterId = static_cast<uint16_t>(aEventArgs->GetInt(0));
    response.Accepted = aEventArgs->GetBool(1);

    m_transport.Send(response);
}

void OverlayClient::ProcessSetProfilePicture(CefRefPtr<CefListValue> aEventArgs)
{
    std::string payload;
    if (aEventArgs->GetSize() > 0)
        payload = aEventArgs->GetString(0).ToString();

    constexpr size_t cMaxAvatarBytes = 256u * 1024u;
    if (payload.size() > cMaxAvatarBytes)
    {
        spdlog::warn("[OverlayClient] Ignoring avatar upload larger than {} bytes", cMaxAvatarBytes);
        return;
    }

    PlayerProfileImageUpdateRequest request{};
    request.ImageData = payload;
    m_transport.Send(request);
}

void OverlayClient::ProcessSetNameTagMode(CefRefPtr<CefListValue> aEventArgs)
{
    if (!aEventArgs || aEventArgs->GetSize() < 1)
        return;

    const int rawMode = aEventArgs->GetInt(0);
    NameTagService::Mode mode = NameTagService::Mode::Normal;
    switch (rawMode)
    {
    case static_cast<int>(NameTagService::Mode::Detailed):
        mode = NameTagService::Mode::Detailed;
        break;
    case static_cast<int>(NameTagService::Mode::Basic):
        mode = NameTagService::Mode::Basic;
        break;
    case static_cast<int>(NameTagService::Mode::Hidden):
        mode = NameTagService::Mode::Hidden;
        break;
    case static_cast<int>(NameTagService::Mode::Normal):
        mode = NameTagService::Mode::Normal;
        break;
    default:
        break;
    }

    World::Get().GetRunner().Queue([mode]() {
        auto& world = World::Get();
        if (!world.ctx().contains<NameTagService>())
            return;
        world.ctx().at<NameTagService>().SetMode(mode);
    });
}

void OverlayClient::ProcessToggleDebugUI()
{
    World::Get().GetDebugService().m_showDebugStuff = !World::Get().GetDebugService().m_showDebugStuff;
}

void OverlayClient::ProcessPlayEmote(CefRefPtr<CefListValue> aEventArgs)
{
    if (!aEventArgs || aEventArgs->GetSize() < 1)
        return;

    const std::string eventName = aEventArgs->GetString(0).ToString();
    if (eventName.empty() || eventName.size() > 64)
        return;

    PlayEmoteInternal(eventName);
}

void OverlayClient::SetUIVisible(bool aVisible) noexcept
{
    auto pRenderer = GetOverlayRenderHandler();
    if (!pRenderer)
        return;

    TiltedPhoques::DInputHook::Get().SetEnabled(aVisible);
    World::Get().GetOverlayService().SetActive(aVisible);
    pRenderer->SetCursorVisible(aVisible);
}
