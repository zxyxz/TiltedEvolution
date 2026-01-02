#include <Components.h>
#include <GameServer.h>
#include <Packet.hpp>

#include <Events/CharacterRemoveEvent.h>
#include <Events/OwnershipTransferEvent.h>
#include <Events/PacketEvent.h>
#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveCellEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>
#include <steam/isteamnetworkingutils.h>

#include <Services/LoginService.h>

#include <Messages/AuthenticationResponse.h>
#include <Messages/ClientMessageFactory.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerProfileImage.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifySettingsChange.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <cctype>
#include <ChatMessageTypes.h>
#include <fmt/format.h>
#include <console/ConsoleRegistry.h>
#include <resources/ResourceCollection.h>
#include <fmt/format.h>
#include <Services/AdminService.h>
#include <Services/ChatCommandService.h>

constexpr size_t kMaxServerNameLength = 128u;

namespace
{
Player* FindPlayerByUsernameInsensitive(World& world, const TiltedPhoques::String& username)
{
    if (username.empty())
        return nullptr;

    TiltedPhoques::String needle = username;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (Player* player : world.GetPlayerManager())
    {
        if (!player)
            continue;

        TiltedPhoques::String candidate = player->GetUsername();
        std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (candidate == needle)
            return player;
    }

    return nullptr;
}

TiltedPhoques::String TrimCopy(const TiltedPhoques::String& value)
{
    TiltedPhoques::String trimmed = value;
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
        trimmed.pop_back();
    return trimmed;
}

bool ParseBroadcastCommand(const TiltedPhoques::String& command, TiltedPhoques::String& outMessage)
{
    const TiltedPhoques::String trimmed = TrimCopy(command);
    if (trimmed.empty() || trimmed.front() != '/')
        return false;

    TiltedPhoques::String withoutPrefix = trimmed.substr(1);
    withoutPrefix = TrimCopy(withoutPrefix);
    if (withoutPrefix.empty())
        return false;

    size_t splitPos = withoutPrefix.find_first_of(" \t");
    TiltedPhoques::String name = splitPos == TiltedPhoques::String::npos ? withoutPrefix : withoutPrefix.substr(0, splitPos);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name != "broadcast")
        return false;

    if (splitPos == TiltedPhoques::String::npos)
    {
        outMessage.clear();
        return true;
    }

    outMessage = TrimCopy(withoutPrefix.substr(splitPos + 1));
    return true;
}
} // namespace

// -- Cvars --
Console::Setting uServerPort{"GameServer:uPort", "Which port to host the server on", 10578u};
Console::Setting uMaxPlayerCount{"GameServer:uMaxPlayerCount", "Maximum number of players allowed on the server (going over the default of 8 is not recommended)", 8u};
Console::Setting bPremiumTickrate{"GameServer:bPremiumMode", "Use premium tick rate", true};

Console::StringSetting sServerName{"GameServer:sServerName", "Name that shows up in the server list", "Dedicated Together Server"};
Console::StringSetting sPassword{"GameServer:sPassword", "Server password", ""};

// Gameplay
// TODO: to make this easier for users, use game names for difficulty instead of int
Console::Setting uDifficulty{"Gameplay:uDifficulty", "In game difficulty (0 to 5)", 4u};
Console::Setting bEnableGreetings{"Gameplay:bEnableGreetings", "Enables NPC greetings (disabled by default since they can be spammy with dialogue sync)", false};
Console::Setting bEnablePvp{"Gameplay:bEnablePvp", "Enables pvp", false};
Console::Setting bSyncPlayerHomes{"Gameplay:bSyncPlayerHomes", "Sync chests and displays in player homes and other NoResetZones", false};
Console::Setting bEnableDeathSystem{"Gameplay:bEnableDeathSystem", "Enables the custom multiplayer death system", true};
Console::Setting uTimeScale{"Gameplay:uTimeScale", "How many seconds pass ingame for every real second (0 to 1000). Changing this can make the game unstable", 20u};
Console::Setting bSyncPlayerCalendar{"Gameplay:bSyncPlayerCalendar", "Syncs up all player calendars to be the same day, month, and year. This uses the date of the player with the furthest ahead date at connection.", false};
Console::Setting bSyncPartyFastTravelMarkers{"Gameplay:bSyncPartyFastTravelMarkers", "Sync discovered fast travel map markers within parties", true};
Console::Setting bAutoPartyJoin{"Gameplay:bAutoPartyJoin", "Join parties automatically, as long as there is only one party in the server", true};
// ModPolicy Stuff
Console::Setting bEnableModCheck{"ModPolicy:bEnableModCheck", "Bypass the checking of mods on the server", false, Console::SettingsFlags::kLocked};
Console::Setting bAllowSKSE{"ModPolicy:bAllowSKSE", "Allow clients with SKSE active to join", true, Console::SettingsFlags::kLocked};
Console::Setting bAllowMO2{"ModPolicy:bAllowMO2", "Allow clients running Mod Organizer 2 to join", true, Console::SettingsFlags::kLocked};

// -- Commands --
Console::Command<> TogglePremium(
    "TogglePremium", "Toggle Premium Tickrate on/off",
    [](Console::ArgStack&)
    {
        bPremiumTickrate = !bPremiumTickrate;
        spdlog::get("ConOut")->info("Premium Tickrate has been {}.", bPremiumTickrate == true ? "enabled" : "disabled");
    });

Console::Command<> TogglePvp(
    "TogglePvp", "Toggle PvP on/off",
    [](Console::ArgStack&)
    {
        bEnablePvp = !bEnablePvp;
        spdlog::get("ConOut")->info("PvP has been {}.", bEnablePvp == true ? "enabled" : "disabled");
        GameServer::Get()->UpdateSettings();
    });

Console::Command<int64_t> SetDifficulty(
    "SetDifficulty", "Set server difficulty (0 being Novice and 5 being Legendary; default is 4)",
    [](Console::ArgStack& aStack)
    {
        auto aDiff = aStack.Pop<int64_t>();

        if (aDiff < 0 || aDiff > 5)
        {
            spdlog::warn(
                "Game difficulty is invalid (should be from 0 to 5, "
                "current value is {}), setting difficulty to 4 (master).",
                aDiff);

            aDiff = 4;
        }

        uDifficulty = (uint32_t)aDiff;

        GameServer::Get()->UpdateSettings();
        spdlog::get("ConOut")->info("Difficulty has been set to {}.", aDiff);
    });

Console::Command<> ShowVersion("version", "Show the version the server was compiled with", [](Console::ArgStack&) { spdlog::get("ConOut")->info("Server " BUILD_COMMIT); });

Console::Command<> CrashServer(
    "crash", "Crashes the server, don't use!",
    [](Console::ArgStack&)
    {
        int* i = 0;
        *i = 42;
    });

Console::Command<> ShowMoPoStatus(
    "ShowMOPOStats", "Shows the status of ModPolicy",
    [](Console::ArgStack&)
    {
        auto formatStatus = [](bool aToggle)
        {
            return aToggle ? "yes" : "no";
        };

        spdlog::get("ConOut")->info("Modcheck enabled: {}\nSKSE allowed: {}\nMO2 allowed: {}", formatStatus(bEnableModCheck), formatStatus(bAllowSKSE), formatStatus(bAllowMO2));
    });

// -- Constants --
constexpr char kBypassMoPoWarning[]{"ModCheck is disabled. This can lead to desync and other oddities. Make sure you know what you are doing. We "
                                    "may not be able to assist you if ModCheck was disabled."};

constexpr char kMopoRecordsMissing[]{"Failed to start: ModPolicy's ModCheck is enabled, but no mods are installed. Players won't be able "
                                     "to join! Please create a Data/ directory, and put a \"loadorder.txt\" file in there."
                                     "Check the wiki, which can be found on skyrim-together.com, for more details."};

constexpr char kCalendarSyncWarning[]{"Calendar sync is enabled. We generally do not recommend that you use this feature."
                                      "Calendar sync can cause the calendar to jump ahead or behind, which might mess up the timing of quests."
                                      "If you disable this feature again (which is the default setting), the days will still progress, but the"
                                      "exact date will differ slightly between clients (which has no impact on gameplay)."};

static uint16_t GetUserTickRate()
{
    return bPremiumTickrate ? 60 : 30;
}

static bool IsMoPoActive()
{
    return bEnableModCheck;
}

ServerSettings GetSettings()
{
    ServerSettings settings{};
    settings.Difficulty = uDifficulty.value_as<uint8_t>();
    settings.GreetingsEnabled = bEnableGreetings;
    settings.PvpEnabled = bEnablePvp;
    settings.SyncPlayerHomes = bSyncPlayerHomes;
    settings.DeathSystemEnabled = bEnableDeathSystem;
    settings.SyncPlayerCalendar = bSyncPlayerCalendar;
    settings.SyncPartyFastTravelMarkers = bSyncPartyFastTravelMarkers;
    settings.AutoPartyJoin = bAutoPartyJoin;
    return settings;
}

GameServer::GameServer(Console::ConsoleRegistry& aConsole) noexcept
    : m_lastFrameTime(std::chrono::high_resolution_clock::now())
    , m_startTime(std::chrono::high_resolution_clock::now())
    , m_commands(aConsole)
    , m_requestStop(false)
{
    BASE_ASSERT(s_pInstance == nullptr, "Server instance already exists?");
    s_pInstance = this;

    auto port = uServerPort.value_as<uint16_t>();
    while (!Host(port, GetUserTickRate()))
    {
        spdlog::warn("Port {} is already in use, trying {}", port, port + 1);
        port++;
    }

    if (uDifficulty.value_as<uint8_t>() > 5)
    {
        spdlog::warn(
            "Game difficulty is invalid (should be from 0 to 5, current value is {}), setting difficulty to 4 "
            "(master).",
            uDifficulty.value_as<uint8_t>());

        uDifficulty = 4;
    }

    if (!bEnableDeathSystem)
    {
        spdlog::warn("The multiplayer death system is disabled on this server. We recommend that you ONLY do this if you have"
                     " a mod that replaces the vanilla death system. You should only disable our death system if you"
                     " absolutely know what you are doing!");
    }

    m_isPasswordProtected = strcmp(sPassword.value(), "") != 0;

    UpdateInfo();
    spdlog::info("Server {} started on port {}", BUILD_COMMIT, GetPort());
    UpdateTitle();

    m_pWorld = MakeUnique<World>();

    BindMessageHandlers();
    UpdateTimeScale();

    m_pResources = MakeUnique<Resources::ResourceCollection>();
}

GameServer::~GameServer()
{
    s_pInstance = nullptr;
}

GameServer* GameServer::Get() noexcept
{
    return s_pInstance;
}

void GameServer::Initialize()
{
    if (!CheckMoPo())
        return;

    if (bSyncPlayerCalendar)
        spdlog::warn(kCalendarSyncWarning);

    BindServerCommands();
    m_pWorld->GetScriptService().Initialize(*m_pResources);
}

void GameServer::Kill()
{
    spdlog::info("Server shutdown requested");
    m_requestStop = true;
}

bool GameServer::CheckMoPo()
{
    if (!bEnableModCheck)
    {
        // TODO: re-enable this warning when mopo has good ui and the line endings problem is fixed
        // spdlog::warn(kBypassMoPoWarning);
    }
    // Server is not aware of any installed mods.
    else if (!m_pWorld->GetRecordCollection())
    {
        spdlog::error(kMopoRecordsMissing);
        Kill();
        return false;
    }
    else
        spdlog::info("ModPolicy is active");
    return true;
}

void GameServer::BindMessageHandlers()
{
    auto handlerGenerator = [this](auto& x)
    {
        using T = typename std::remove_reference_t<decltype(x)>::Type;

        m_messageHandlers[T::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId)
        {
            auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);

            if (!pPlayer)
            {
                spdlog::error("Connection {:x} is not associated with a player.", aConnectionId);
                Kick(aConnectionId);
                return;
            }

            const auto pRealMessage = CastUnique<T>(std::move(apMessage));

            m_pWorld->GetDispatcher().trigger(PacketEvent<T>(pRealMessage.get(), pPlayer));
        };

        return false;
    };

    ClientMessageFactory::Visit(handlerGenerator);

    // Override authentication request
    m_messageHandlers[AuthenticationRequest::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId)
    {
        const auto pRealMessage = CastUnique<AuthenticationRequest>(std::move(apMessage));
        HandleAuthenticationRequest(aConnectionId, pRealMessage);
    };

}

void GameServer::BindServerCommands()
{
    m_commands.RegisterCommand<>(
        "uptime", "Show how long the server has been running for",
        [this](Console::ArgStack&)
        {
            Uptime uptime = GetUptime();
            spdlog::get("ConOut")->info("Server uptime: {}w {}d {}h {}m", uptime.weeks, uptime.days, uptime.hours, uptime.minutes);
        });

    m_commands.RegisterCommand<>(
        "players", "List all players on this server",
        [&](Console::ArgStack&)
        {
            auto out = spdlog::get("ConOut");
            uint32_t count = m_pWorld->GetPlayerManager().Count();
            if (count == 0)
            {
                out->warn("No players on here. Invite some friends!");
                return;
            }

            out->info("<------Players-({})--->", count);
            for (Player* pPlayer : m_pWorld->GetPlayerManager())
            {
                out->info("{}: {}", pPlayer->GetId(), pPlayer->GetUsername().c_str());
            }
        });

    m_commands.RegisterCommand<std::string>(
        "AdminAdd", "Add a username to the admin list",
        [&](Console::ArgStack& aStack)
        {
            auto out = spdlog::get("ConOut");
            const TiltedPhoques::String username = aStack.Pop<TiltedPhoques::String>();
            if (username.empty())
            {
                out->error("AdminAdd <username>");
                return;
            }

            if (m_pWorld->GetAdminService().AddAdmin(username))
            {
                out->info("Added admin '{}'.", username.c_str());
                if (auto* player = FindPlayerByUsernameInsensitive(*m_pWorld, username))
                    m_pWorld->ctx().at<ChatCommandService>().SendCommandList(player);
            }
            else
                out->error("Failed to add admin '{}'.", username.c_str());
        });

    m_commands.RegisterCommand<std::string>(
        "AdminRemove", "Remove a username from the admin list",
        [&](Console::ArgStack& aStack)
        {
            auto out = spdlog::get("ConOut");
            const TiltedPhoques::String username = aStack.Pop<TiltedPhoques::String>();
            if (username.empty())
            {
                out->error("AdminRemove <username>");
                return;
            }

            if (m_pWorld->GetAdminService().RemoveAdmin(username))
            {
                out->info("Removed admin '{}'.", username.c_str());
                if (auto* player = FindPlayerByUsernameInsensitive(*m_pWorld, username))
                    m_pWorld->ctx().at<ChatCommandService>().SendCommandList(player);
            }
            else
                out->error("Failed to remove admin '{}'.", username.c_str());
        });

    m_commands.RegisterCommand<>(
        "AdminList", "List all admins",
        [&](Console::ArgStack&)
        {
            auto out = spdlog::get("ConOut");
            TiltedPhoques::Vector<TiltedPhoques::String> admins;
            m_pWorld->GetAdminService().GetAdmins(admins);
            if (admins.empty())
            {
                out->info("No admins configured.");
                return;
            }

            out->info("<------Admins-({})--->", admins.size());
            for (const auto& name : admins)
                out->info("{}", name.c_str());
        });

    m_commands.RegisterCommand<>(
        "mods", "List all installed mods on this server",
        [&](Console::ArgStack&)
        {
            auto out = spdlog::get("ConOut");
            auto& mods = m_pWorld->ctx().at<ModsComponent>().GetServerMods();
            if (mods.size() == 0)
            {
                out->warn("No mods installed");
                return;
            }

            out->info("<------Mods-({})--->", mods.size());
            for (auto& it : mods)
            {
                out->info(it.first);
            }
        });

    m_commands.RegisterCommand<>(
        "resources", "List all loaded resources on the server",
        [&](Console::ArgStack&)
        {
            auto out = spdlog::get("ConOut");
            if (!m_pResources || m_pResources->GetManifests().size() == 0)
            {
                out->warn("No resources loaded");
                return;
            }

            out->info("<------Resources-({})--->", m_pResources->GetManifests().size());
            m_pResources->ForEachManifest([&](const auto& aManifest) { out->info("{} -> {}", aManifest.Name.c_str(), aManifest.Description.c_str()); });
        });

    m_commands.RegisterCommand<>("quit", "Stop the server", [&](Console::ArgStack&) { Kill(); });

    m_commands.RegisterCommand<int64_t, int64_t>(
        "SetTime", "Set ingame hour and minute",
        [&](Console::ArgStack& aStack)
        {
            auto out = spdlog::get("ConOut");

            const int hour = static_cast<int>(aStack.Pop<int64_t>());
            const int minute = static_cast<int>(aStack.Pop<int64_t>());
            auto timescale = m_pWorld->GetCalendarService().GetTimeScale();

            bool time_set_successfully = m_pWorld->GetCalendarService().SetTime(hour, minute, timescale);

            if (time_set_successfully)
            {
                out->info("Time set to {:02}:{:02}", hour, minute);
            }
            else
            {
                out->error("Hour must be between 0-23 and minute must be between 0-59");
            }
        });

    m_commands.RegisterCommand<int64_t, int64_t, int64_t>(
        "SetDate", "Set ingame day, month, and year",
        [&](Console::ArgStack& aStack)
        {
            auto out = spdlog::get("ConOut");

            const int day = static_cast<int>(aStack.Pop<int64_t>());
            const int month = static_cast<int>(aStack.Pop<int64_t>());
            const float year = static_cast<float>(aStack.Pop<int64_t>());

            bool time_set_successfully = m_pWorld->GetCalendarService().SetDate(day, month, year);

            if (time_set_successfully)
            {
                out->info("Time set to {:02}/{:02}:{:02}", month, day, year);
            }
            else
            {
                out->error("Day must be between 0 and 31, month must be between 0 and 11, and year must be between 0 and 999.");
            }
        });

}

/* Update Info fields from user facing CVARS.*/
void GameServer::UpdateInfo()
{
    const String cServerName = sServerName.c_str();

    if (cServerName.length() > kMaxServerNameLength)
    {
        spdlog::error("sServerName is longer than the limit of {} characters/bytes, and has been cut short", kMaxServerNameLength);
        m_info.name = cServerName.substr(0U, kMaxServerNameLength);
    }
    else
    {
        m_info.name = cServerName;
    }

    m_info.desc = "";
    m_info.icon_url = "";
    m_info.tagList = "";
    m_info.tick_rate = GetUserTickRate();
}

void GameServer::UpdateTimeScale()
{
    auto timescale = uTimeScale.value_as<float>();

    bool timescale_set_successfully = m_pWorld->GetCalendarService().SetTimeScale(timescale);

    if (!timescale_set_successfully)
    {
        spdlog::warn("TimeScale is invalid (should be from 0 to 1000, current value is {}), setting TimeScale to 20 (default)", timescale);

        uTimeScale = 20u;
    }
}

void GameServer::OnUpdate()
{
    const auto cNow = std::chrono::high_resolution_clock::now();
    const auto cDelta = cNow - m_lastFrameTime;
    m_lastFrameTime = cNow;

    const auto cDeltaSeconds = std::chrono::duration_cast<std::chrono::duration<float>>(cDelta).count();

    auto& dispatcher = m_pWorld->GetDispatcher();

    dispatcher.trigger(UpdateEvent{cDeltaSeconds});

    if (m_requestStop)
        Close();
}

void GameServer::OnConsume(const void* apData, const uint32_t aSize, const ConnectionId_t aConnectionId)
{
    ViewBuffer buf((uint8_t*)apData, aSize);
    Buffer::Reader reader(&buf);

    const ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from {:x}", aConnectionId);
        return;
    }

    m_messageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId);
}

void GameServer::OnConnection(const ConnectionId_t aHandle)
{
    spdlog::info("Connection received {:x}", aHandle);
    UpdateTitle();
}

void GameServer::OnDisconnection(const ConnectionId_t aConnectionId, EDisconnectReason aReason)
{
    auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);

    spdlog::info("Connection ended {:x} - '{}' disconnected", aConnectionId, (pPlayer != NULL ? pPlayer->GetUsername().c_str() : "NULL"));

    m_pWorld->GetScriptService().HandlePlayerQuit(aConnectionId, aReason);

    if (pPlayer)
    {
        if (const auto& cell = pPlayer->GetCellComponent())
        {
            const auto oldCell = cell.Cell;
            pPlayer->SetCellComponent(CellIdComponent{{}, {}, {}});
            m_pWorld->GetDispatcher().trigger(PlayerLeaveCellEvent(oldCell));
        }

        m_pWorld->GetDispatcher().trigger(PlayerLeaveEvent(pPlayer));

        NotifyPlayerLeft notify{};
        notify.PlayerId = pPlayer->GetId();
        notify.Username = pPlayer->GetUsername();
        SendToPlayers(notify);

        entt::entity playerCharacter = pPlayer->GetCharacter().value_or(entt::null);

        // Cleanup all entities that we own
        auto ownerView = m_pWorld->view<OwnerComponent>();
        for (auto entity : ownerView)
        {
            if (entity == playerCharacter)
            {
                m_pWorld->GetDispatcher().enqueue(CharacterRemoveEvent(World::ToInteger(entity)));
                continue;
            }

            const auto& [ownerComponent] = ownerView.get(entity);
            if (ownerComponent.GetOwner() == pPlayer)
            {
                m_pWorld->GetDispatcher().enqueue(OwnershipTransferEvent(entity));
            }
        }

        m_pWorld->GetDispatcher().update();

        m_pWorld->GetPlayerManager().Remove(pPlayer);
    }

    UpdateTitle();
}

void GameServer::Send(const ConnectionId_t aConnectionId, const ServerMessage& acServerMessage) const
{
    static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};

    Buffer buffer(1 << 20);
    Buffer::Writer writer(&buffer);
    writer.WriteBits(0, 8); // Skip the first byte as it is used by packet

    acServerMessage.Serialize(writer);

    TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), static_cast<uint32_t>(writer.Size()));
    Server::Send(aConnectionId, &packet);

    s_allocator.Reset();
}


void GameServer::SendToLoaded(const ServerMessage& acServerMessage) const
{
    TiltedPhoques::Vector<ConnectionId_t> players;
    players.reserve(m_pWorld->GetPlayerManager().Count());

    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        players.push_back(pPlayer->GetConnectionId());
    }

    for (auto connectionId : players)
    {
        Player* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(connectionId);
        if (pPlayer && pPlayer->GetCellComponent())
            pPlayer->Send(acServerMessage);
    }
}

void GameServer::SendToPlayers(const ServerMessage& acServerMessage, const Player* apExcludedPlayer) const
{
    TiltedPhoques::Vector<ConnectionId_t> players;
    players.reserve(m_pWorld->GetPlayerManager().Count());

    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        players.push_back(pPlayer->GetConnectionId());
    }

    for (auto connectionId : players)
    {
        Player* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(connectionId);
        if (pPlayer && pPlayer != apExcludedPlayer)
            pPlayer->Send(acServerMessage);
    }
}

// NOTE: this doesn't check objects in range, only characters in range.
bool GameServer::SendToPlayersInRange(const ServerMessage& acServerMessage, const entt::entity acOrigin, const Player* apExcludedPlayer) const
{
    if (!m_pWorld->valid(acOrigin))
    {
        spdlog::error("Entity is invalid: {:X}", World::ToInteger(acOrigin));
        return false;
    }

    const auto view = m_pWorld->view<CellIdComponent>();
    const auto it = view.find(acOrigin);

    if (it == view.end())
    {
        spdlog::warn("Cell component not found for entity {:X}", World::ToInteger(acOrigin));
        return false;
    }

    const auto& cellComponent = view.get<CellIdComponent>(*it);

    bool isDragon = false;
    if (const auto* characterComponent = m_pWorld->try_get<CharacterComponent>(acOrigin))
        isDragon = characterComponent->IsDragon();

    TiltedPhoques::Vector<ConnectionId_t> players;
    players.reserve(m_pWorld->GetPlayerManager().Count());

    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        players.push_back(pPlayer->GetConnectionId());
    }

    for (auto connectionId : players)
    {
        Player* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(connectionId);

        if (pPlayer && cellComponent.IsInRange(pPlayer->GetCellComponent(), isDragon) && pPlayer != apExcludedPlayer)
            pPlayer->Send(acServerMessage);
    }

    return true;
}

void GameServer::SendToParty(const ServerMessage& acServerMessage, const PartyComponent& acPartyComponent, const Player* apExcludeSender) const
{
    if (!acPartyComponent.JoinedPartyId.has_value())
    {
        spdlog::warn("Party does not exist, canceling broadcast.");
        return;
    }

    TiltedPhoques::Vector<ConnectionId_t> players;
    players.reserve(m_pWorld->GetPlayerManager().Count());

    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        players.push_back(pPlayer->GetConnectionId());
    }

    for (auto connectionId : players)
    {
        Player* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(connectionId);

        if (!pPlayer || pPlayer == apExcludeSender)
            continue;

        const auto& partyComponent = pPlayer->GetParty();
        if (partyComponent.JoinedPartyId == acPartyComponent.JoinedPartyId)
        {
            pPlayer->Send(acServerMessage);
        }
    }
}

void GameServer::SendToPartyInRange(const ServerMessage& acServerMessage, const PartyComponent& acPartyComponent, const entt::entity acOrigin, const Player* apExcludeSender) const
{
    if (!acPartyComponent.JoinedPartyId.has_value())
    {
        spdlog::warn("Party does not exist, canceling broadcast.");
        return;
    }

    const auto view = m_pWorld->view<CellIdComponent>();
    const auto it = view.find(acOrigin);

    if (it == view.end())
    {
        spdlog::warn("Cell component not found for entity {:X}", World::ToInteger(acOrigin));
        return;
    }

    const auto& cellComponent = view.get<CellIdComponent>(*it);

    TiltedPhoques::Vector<ConnectionId_t> players;
    players.reserve(m_pWorld->GetPlayerManager().Count());

    for (Player* pPlayer : m_pWorld->GetPlayerManager())
    {
        players.push_back(pPlayer->GetConnectionId());
    }

    for (auto connectionId : players)
    {
        Player* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(connectionId);

        if (!pPlayer || pPlayer == apExcludeSender)
            continue;

        if (!cellComponent.IsInRange(pPlayer->GetCellComponent(), false))
            continue;

        if (pPlayer->GetParty().JoinedPartyId != acPartyComponent.JoinedPartyId)
            continue;

        pPlayer->Send(acServerMessage);
    }
}

static String PrettyPrintModList(const Vector<Mods::Entry>& acMods)
{
    String text;
    for (size_t i = 0; i < acMods.size(); i++)
    {
        text += acMods[i].Filename;
        if (i != (acMods.size() - 1))
            text += ", ";
    }

    return text;
}

bool GameServer::ValidateAuthParams(ConnectionId_t aConnectionId, const UniquePtr<AuthenticationRequest>& acRequest)
{
    return false;
}

void GameServer::HandleAuthenticationRequest(const ConnectionId_t aConnectionId, const UniquePtr<AuthenticationRequest>& acRequest)
{
    const auto info = GetConnectionInfo(aConnectionId);

    char remoteAddress[48]{};
    info.m_addrRemote.ToString(remoteAddress, 48, false);

    AuthenticationResponse serverResponse;
    serverResponse.Version = BUILD_COMMIT;

    using RT = AuthenticationResponse::ResponseType;
    auto sendKick = [&](const RT type)
    {
        serverResponse.Type = type;
        Send(aConnectionId, serverResponse);
        // the previous message is a lingering kick, it still gets delivered.
        Kick(aConnectionId);
    };
#if 1
    // to make our testing life a bit easier.
    if (acRequest->Version != BUILD_COMMIT)
    {
        spdlog::info("New player {:x} '{}' tried to connect with client {} - Version mismatch", aConnectionId, remoteAddress, acRequest->Version.c_str());
        sendKick(RT::kWrongVersion);
        return;
    }
#endif

    if (m_pWorld->GetPlayerManager().Count() >= uMaxPlayerCount.value_as<uint32_t>())
    {
        sendKick(RT::kServerFull);
        return;
    }

    bool skseProblem = !bAllowSKSE && acRequest->SKSEActive;
    bool mo2Problem = !bAllowMO2 && acRequest->MO2Active;

    if (skseProblem || mo2Problem)
    {
        TiltedPhoques::String response;
        if (skseProblem)
            response += "SKSE ";
        if (mo2Problem)
            response += "MO2 ";

        spdlog::info("New player {:x} '{}' tried to connect, but {}{} disallowed - Kicked.", aConnectionId, remoteAddress, response.c_str(), skseProblem && mo2Problem ? "are" : "is");

        serverResponse.SKSEActive = acRequest->SKSEActive;
        serverResponse.MO2Active = acRequest->MO2Active;
        sendKick(RT::kClientModsDisallowed);
        return;
    }

    const auto sanitizedUsername = SanitizeUsername(acRequest->Username);
    if (!sanitizedUsername.empty())
    {
        auto iequals = [](const String& a, const String& b) -> bool {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        };

        for (auto* pExisting : m_pWorld->GetPlayerManager())
        {
            if (!pExisting)
                continue;

            if (iequals(pExisting->GetUsername(), sanitizedUsername))
            {
                spdlog::info("New player {:x} '{}' denied: username '{}' already connected", aConnectionId, remoteAddress, sanitizedUsername.c_str());
                sendKick(RT::kDuplicateUser);
                return;
            }
        }
    }
    auto& loginService = m_pWorld->ctx().at<LoginService>();
    const auto loginResult = loginService.VerifyOrCreateUser(sanitizedUsername, acRequest->Password);

    if (loginResult != LoginService::LoginResult::Ok)
    {
        spdlog::info("New player {:x} '{}' failed to authenticate for user '{}'", aConnectionId, remoteAddress, sanitizedUsername.c_str());
        sendKick(RT::kWrongAccountPassword);
        return;
    }

    const auto storedAvatar = loginService.GetAvatar(sanitizedUsername);

    acRequest->Username = sanitizedUsername;
    acRequest->Password.clear();

    // check if the proper server password was supplied.
    if (acRequest->Token == sPassword.value())
    {

        Mods& responseList = serverResponse.UserMods;
        auto& modsComponent = m_pWorld->ctx().at<ModsComponent>();

        if (IsMoPoActive())
        {
            // mods that exist on the client, but not on the server
            // modscomponent contains a list filled in by the recordcollection
            Mods modsToRemove;

            const auto& userMods = acRequest->UserMods.ModList;
            for (const Mods::Entry& mod : userMods)
            {
                // if the client has more mods than the server..
                if (!modsComponent.IsInstalled(mod.Filename))
                {
                    modsToRemove.ModList.push_back(mod);
                }
            }

            // TODO(Vince): if you have a better to do this than two for loops
            // let me know!
            // Also, for the future, lets think about a mode that allows more than the server installed mods
            // but requires essential mods?

            // mods that may exist on the server, but not on the client
            for (const auto& entry : modsComponent.GetServerMods())
            {
                const auto it = std::find_if(userMods.begin(), userMods.end(), [&](const Mods::Entry& it) { return it.Filename == entry.first; });

                if (it == userMods.end())
                {
                    Mods::Entry removeEntry;
                    removeEntry.Filename = entry.first;
                    removeEntry.Id = 0;
                    modsToRemove.ModList.push_back(removeEntry);
                }
            }

            if (modsToRemove.ModList.size() > 0)
            {
                String text = PrettyPrintModList(modsToRemove.ModList);
                // "ModPolicy: refusing connection {:x} because essential mods are missing: {}"
                // for future reference ^
                spdlog::info("ModPolicy: refusing connection {:x} because the following mods are installed on the client: {}", aConnectionId, text.c_str());

                serverResponse.UserMods.ModList = std::move(modsToRemove.ModList);
                sendKick(RT::kModsMismatch);
                return;
            }
        }

        // Note: to lower traffic we only send the mod ids the user can fix in order as other ids will lead to a
        // null form id anyway
        Vector<String> playerMods;
        Vector<uint16_t> playerModsIds;

        size_t i = 0;
        for (auto& mod : acRequest->UserMods.ModList)
        {
            const uint32_t id = mod.IsLite ? modsComponent.AddLite(mod.Filename) : modsComponent.AddStandard(mod.Filename);

            Mods::Entry entry;
            entry.Filename = mod.Filename;
            entry.Id = static_cast<uint16_t>(id);
            entry.IsLite = mod.IsLite;

            playerMods.push_back(mod.Filename);
            playerModsIds.push_back(entry.Id);
            responseList.ModList.push_back(entry);
        }

        Player* pPlayer = m_pWorld->GetPlayerManager().Create(aConnectionId);
        pPlayer->SetEndpoint(remoteAddress);
        pPlayer->SetDiscordId(acRequest->DiscordId);
        pPlayer->SetUsername(std::move(acRequest->Username));
        pPlayer->SetMods(playerMods);
        pPlayer->SetModIds(playerModsIds);
        pPlayer->SetLevel(acRequest->Level);
        pPlayer->SetAvatar(storedAvatar);

        // this event is shit, needs to be fixed, i know
        auto [canceled, reason] = m_pWorld->GetScriptService().HandlePlayerJoin(aConnectionId);
        if (canceled)
        {
            spdlog::info("New player {:x} has a been rejected because \"{}\".", aConnectionId, reason.c_str());
            Kick(aConnectionId);
            m_pWorld->GetPlayerManager().Remove(pPlayer);
            return;
        }

        serverResponse.PlayerId = pPlayer->GetId();

        auto modList = PrettyPrintModList(acRequest->UserMods.ModList);
        spdlog::info("New player '{}' [{:x}] connected with {} mods\n\t: {}", pPlayer->GetUsername().c_str(), aConnectionId, acRequest->UserMods.ModList.size(), modList.c_str());

        serverResponse.Settings = GetSettings();

        serverResponse.Type = AuthenticationResponse::ResponseType::kAccepted;
        Send(aConnectionId, serverResponse);

        NotifyChatMessageBroadcast joinMessage{};
        joinMessage.MessageType = ChatMessageType::kSystemMessage;
        joinMessage.PlayerName = "";
        joinMessage.ChatMessage = fmt::format("{} connected to the server.", pPlayer->GetUsername().c_str());
        SendToPlayers(joinMessage);

        uint32_t startId = 0;
        auto initStringCache = StringCache::Get().Serialize(startId);

        pPlayer->SetStringCacheId(startId);

        Send(aConnectionId, initStringCache);

        {
            NotifyPlayerProfileImage avatarNotify{};
            avatarNotify.PlayerId = pPlayer->GetId();
            avatarNotify.Avatar = pPlayer->GetAvatar();
            Send(pPlayer->GetConnectionId(), avatarNotify);
        }

        for (auto* pOtherPlayer : m_pWorld->GetPlayerManager())
        {
            if (pOtherPlayer == pPlayer)
                continue;

            NotifyPlayerJoined notify{};
            notify.PlayerId = pOtherPlayer->GetId();
            notify.Username = pOtherPlayer->GetUsername();
            notify.Avatar = pOtherPlayer->GetAvatar();

            auto& cellComponent = pOtherPlayer->GetCellComponent();
            notify.WorldSpaceId = cellComponent.WorldSpaceId;
            notify.CellId = cellComponent.Cell;

            notify.Level = pOtherPlayer->GetLevel();

            spdlog::debug("[GameServer] New notify player {:x} {}", notify.PlayerId, notify.Username.c_str());

            Send(pPlayer->GetConnectionId(), notify);
        }

        m_pWorld->GetDispatcher().trigger(PlayerJoinEvent(pPlayer, acRequest->WorldSpaceId, acRequest->CellId, acRequest->PlayerTime));
    }
    else
    {
        spdlog::info("New player {:x} '{}' supplied an incorrect server password, kicking.", aConnectionId, remoteAddress);
        sendKick(RT::kWrongServerPassword);
    }
}

void GameServer::UpdateSettings()
{
    NotifySettingsChange notify{};
    notify.Settings = GetSettings();

    SendToPlayers(notify);
}

GameServer::Uptime GameServer::GetUptime() const noexcept
{
    auto duration = std::chrono::high_resolution_clock::now() - m_startTime;
    auto weeks = std::chrono::duration_cast<std::chrono::weeks>(duration);
    duration -= weeks;
    auto days = std::chrono::duration_cast<std::chrono::days>(duration);
    duration -= days;
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    duration -= hours;
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    return {weeks.count(), days.count(), hours.count(), minutes.count()};
}

Console::ConsoleRegistry::ExecutionResult GameServer::ExecuteConsoleCommand(const String& aCommand) noexcept
{
    using exr = Console::ConsoleRegistry::ExecutionResult;

    const TiltedPhoques::String trimmed = TrimCopy(aCommand);
    if (trimmed.empty())
        return exr::kFailure;

    if (trimmed.front() != '/')
    {
        if (m_pWorld)
            m_pWorld->GetChatCommandService().BroadcastSystemMessage(trimmed);
        return exr::kSuccess;
    }

    TiltedPhoques::String message;
    if (ParseBroadcastCommand(trimmed, message))
    {
        auto out = spdlog::get("ConOut");
        if (message.empty())
        {
            out->error("Usage: /broadcast <message>");
            return exr::kFailure;
        }

        if (m_pWorld)
            m_pWorld->GetChatCommandService().BroadcastSystemMessage(message);
        return exr::kSuccess;
    }

    return m_commands.TryExecuteCommand(trimmed);
}

void GameServer::GetStatusSnapshot(ServerStatusSnapshot& aOutStatus) const
{
    const auto duration = std::chrono::high_resolution_clock::now() - m_startTime;
    aOutStatus.UptimeSeconds = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(duration).count());

    aOutStatus.Players.clear();
    aOutStatus.Players.reserve(m_pWorld->GetPlayerManager().Count());

    for (Player* player : m_pWorld->GetPlayerManager())
    {
        ServerPlayerStatusSnapshot entry;
        entry.PlayerId = player->GetId();
        entry.Username = player->GetUsername();

        const auto& cell = player->GetCellComponent();
        entry.CellBaseId = cell.Cell.BaseId;
        entry.CellModId = cell.Cell.ModId;
        entry.WorldBaseId = cell.WorldSpaceId.BaseId;
        entry.WorldModId = cell.WorldSpaceId.ModId;
        entry.GridX = cell.CenterCoords.X;
        entry.GridY = cell.CenterCoords.Y;

        if (auto character = player->GetCharacter())
        {
            if (const auto* movement = m_pWorld->try_get<MovementComponent>(*character))
            {
                entry.HasPosition = true;
                entry.PositionX = movement->Position.x;
                entry.PositionY = movement->Position.y;
                entry.PositionZ = movement->Position.z;
            }
        }

        aOutStatus.Players.push_back(std::move(entry));
    }
}

void GameServer::UpdateTitle() const
{
    const auto name = m_info.name.empty() ? "Private server" : m_info.name;
    const char* playerText = GetClientCount() <= 1 ? " player" : " players";

    const auto title = fmt::format("{} - {} {} - {} Ticks - " BUILD_BRANCH "@" BUILD_COMMIT, name.c_str(), GetClientCount(), playerText, GetTickRate());

#if TP_PLATFORM_WINDOWS
    SetConsoleTitleA(title.c_str());
#else
    std::cout << "\033]0;" << title << "\007";
#endif
}

String GameServer::SanitizeUsername(const String& acUsername) const noexcept
{
    String username = acUsername;

    // space in username handling | "_" -> space
    std::ranges::replace(username, '_', ' ');

    return username;
}
