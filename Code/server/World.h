#pragma once

#include <Services/PlayerService.h>
#include <Services/PartyService.h>
#include <Services/CharacterService.h>
#include <Services/CalendarService.h>
#include <Services/QuestService.h>
#include <Services/TradeService.h>
#include <Services/ScriptService.h>
#include <Services/AdminService.h>
#include <Services/ChatCommandService.h>
#include <Services/PlayerLocationService.h>

#include <entt/entt.hpp>

#include "Game/PlayerManager.h"

#include <optional>

namespace ESLoader
{
struct RecordCollection;
}

struct World : entt::registry
{
    World();
    ~World() noexcept;

    TP_NOCOPYMOVE(World);

    entt::dispatcher& GetDispatcher() noexcept { return m_dispatcher; }
    const entt::dispatcher& GetDispatcher() const noexcept { return m_dispatcher; }
    CharacterService& GetCharacterService() noexcept { return ctx().at<CharacterService>(); }
    const CharacterService& GetCharacterService() const noexcept { return ctx().at<const CharacterService>(); }
    PlayerService& GetPlayerService() noexcept { return ctx().at<PlayerService>(); }
    const PlayerService& GetPlayerService() const noexcept { return ctx().at<const PlayerService>(); }
    AdminService& GetAdminService() noexcept { return ctx().at<AdminService>(); }
    const AdminService& GetAdminService() const noexcept { return ctx().at<const AdminService>(); }
    ChatCommandService& GetChatCommandService() noexcept { return ctx().at<ChatCommandService>(); }
    const ChatCommandService& GetChatCommandService() const noexcept { return ctx().at<const ChatCommandService>(); }
    PlayerLocationService& GetPlayerLocationService() noexcept { return ctx().at<PlayerLocationService>(); }
    const PlayerLocationService& GetPlayerLocationService() const noexcept { return ctx().at<const PlayerLocationService>(); }
    PartyService& GetPartyService() noexcept { return ctx().at<PartyService>(); }
    const PartyService& GetPartyService() const noexcept { return ctx().at<const PartyService>(); }
    TradeService& GetTradeService() noexcept { return ctx().at<TradeService>(); }
    const TradeService& GetTradeService() const noexcept { return ctx().at<const TradeService>(); }
    CalendarService& GetCalendarService() noexcept { return ctx().at<CalendarService>(); }
    const CalendarService& GetCalendarService() const noexcept { return ctx().at<const CalendarService>(); }
    QuestService& GetQuestService() noexcept { return ctx().at<QuestService>(); }
    const QuestService& GetQuestService() const noexcept { return ctx().at<const QuestService>(); }
    PlayerManager& GetPlayerManager() noexcept { return m_playerManager; }
    const PlayerManager& GetPlayerManager() const noexcept { return m_playerManager; }
    ScriptService& GetScriptService() const noexcept { return *m_pScriptService; }

    // Null checked at start when MoPo is on!
    ESLoader::RecordCollection* GetRecordCollection() noexcept { return m_recordCollection.get(); }

    const ESLoader::RecordCollection* GetRecordCollection() const noexcept { return m_recordCollection.get(); }

    [[nodiscard]] static uint32_t ToInteger(entt::entity aEntity) { return to_integral(aEntity); }
    [[nodiscard]] std::optional<entt::entity> TryResolveEntity(uint32_t aServerId) noexcept;
    [[nodiscard]] std::optional<entt::entity> TryResolveEntity(uint32_t aServerId) const noexcept;

private:
    entt::dispatcher m_dispatcher;

    TiltedPhoques::UniquePtr<ScriptService> m_pScriptService;
    PlayerManager m_playerManager;
    UniquePtr<ESLoader::RecordCollection> m_recordCollection;
};
