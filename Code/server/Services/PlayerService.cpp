#include "Events/CharacterInteriorCellChangeEvent.h"
#include "Events/CharacterExteriorCellChangeEvent.h"
#include "Events/PlayerLeaveCellEvent.h"

#include <Services/PlayerService.h>

#include <Components.h>

#include <Services/CharacterService.h>
#include <GameServer.h>


#include <Messages/ShiftGridCellRequest.h>
#include <Messages/EnterExteriorCellRequest.h>
#include <Messages/EnterInteriorCellRequest.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifyPlayerCellChanged.h>
#include <Messages/PartyMemberDownedRequest.h>
#include <Messages/NotifyPartyMemberDowned.h>
#include <Messages/PlayerProfileImageUpdateRequest.h>
#include <Messages/NotifyPlayerProfileImage.h>
#include <Services/LoginService.h>
#include <Services/PlayerLocationService.h>

#include <Setting.h>
namespace
{
Console::Setting fGoldLossFactor{"Gameplay:fGoldLossFactor", "Factor of the amount of gold lost on death", 0.0f};
}

PlayerService::PlayerService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
    , m_interiorCellEnterConnection(aDispatcher.sink<PacketEvent<EnterInteriorCellRequest>>().connect<&PlayerService::HandleInteriorCellEnter>(this))
    , m_gridCellShiftConnection(aDispatcher.sink<PacketEvent<ShiftGridCellRequest>>().connect<&PlayerService::HandleGridCellShift>(this))
    , m_exteriorCellEnterConnection(aDispatcher.sink<PacketEvent<EnterExteriorCellRequest>>().connect<&PlayerService::HandleExteriorCellEnter>(this))
    , m_playerRespawnConnection(aDispatcher.sink<PacketEvent<PlayerRespawnRequest>>().connect<&PlayerService::OnPlayerRespawnRequest>(this))
    , m_playerLevelConnection(aDispatcher.sink<PacketEvent<PlayerLevelRequest>>().connect<&PlayerService::OnPlayerLevelRequest>(this))
    , m_partyMemberDownedConnection(aDispatcher.sink<PacketEvent<PartyMemberDownedRequest>>().connect<&PlayerService::OnPartyMemberDownedRequest>(this))
    , m_playerProfileImageUpdateConnection(aDispatcher.sink<PacketEvent<PlayerProfileImageUpdateRequest>>().connect<&PlayerService::OnPlayerProfileImageUpdate>(this))
{
}

void SendPlayerCellChanged(const Player* apPlayer) noexcept
{
    auto& cellComponent = apPlayer->GetCellComponent();

    NotifyPlayerCellChanged notify{};
    notify.PlayerId = apPlayer->GetId();
    notify.WorldSpaceId = cellComponent.WorldSpaceId;
    notify.CellId = cellComponent.Cell;

    GameServer::Get()->SendToPlayers(notify, apPlayer);
}

void PlayerService::HandleGridCellShift(const PacketEvent<ShiftGridCellRequest>& acMessage) const noexcept
{
    auto* pPlayer = acMessage.pPlayer;

    auto& message = acMessage.Packet;

    const GameId oldCell = pPlayer->GetCellComponent().Cell;

    CellIdComponent cell = CellIdComponent{message.PlayerCell, message.WorldSpaceId, message.CenterCoords};
    pPlayer->SetCellComponent(cell);
    m_world.GetPlayerLocationService().UpdateCell(pPlayer, cell.WorldSpaceId, cell.Cell, PlayerLocation::Source::CellChange);

    m_world.GetDispatcher().trigger(PlayerLeaveCellEvent(oldCell));

    auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();
    for (auto character : characterView)
    {
        const auto& ownedComponent = characterView.get<OwnerComponent>(character);
        const auto& characterCellComponent = characterView.get<CellIdComponent>(character);

        if (ownedComponent.GetOwner() == pPlayer)
            continue;

        const auto cellIt = std::find_if(std::begin(message.Cells), std::end(message.Cells), [Cells = message.Cells, CharacterCell = characterCellComponent.Cell](auto playerCell) { return playerCell == CharacterCell; });

        if (cellIt == std::end(message.Cells))
        {
            continue;
        }

        CharacterSpawnRequest spawnMessage;
        CharacterService::Serialize(m_world, character, &spawnMessage);

        pPlayer->Send(spawnMessage);
    }
}

void PlayerService::HandleExteriorCellEnter(const PacketEvent<EnterExteriorCellRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;
    auto* pPlayer = acMessage.pPlayer;

    if (pPlayer->GetCharacter())
    {
        auto entity = *pPlayer->GetCharacter();

        auto cell = CellIdComponent{message.CellId, message.WorldSpaceId, message.CurrentCoords};

        if (pPlayer->GetCellComponent())
        {
            m_world.GetDispatcher().trigger(CharacterExteriorCellChangeEvent{pPlayer, entity, message.WorldSpaceId, message.CurrentCoords});
        }

        pPlayer->SetCellComponent(cell);
        m_world.GetPlayerLocationService().UpdateCell(pPlayer, cell.WorldSpaceId, cell.Cell, PlayerLocation::Source::CellChange);

        SendPlayerCellChanged(pPlayer);
    }
}

void PlayerService::HandleInteriorCellEnter(const PacketEvent<EnterInteriorCellRequest>& acMessage) const noexcept
{
    auto* pPlayer = acMessage.pPlayer;

    auto& message = acMessage.Packet;

    const auto oldCell = pPlayer->GetCellComponent().Cell;

    auto cell = CellIdComponent{message.CellId, {}, {}};
    pPlayer->SetCellComponent(cell);
    m_world.GetPlayerLocationService().UpdateCell(pPlayer, cell.WorldSpaceId, cell.Cell, PlayerLocation::Source::CellChange);

    m_world.GetDispatcher().trigger(PlayerLeaveCellEvent(oldCell));

    if (pPlayer->GetCharacter())
    {
        auto entity = *pPlayer->GetCharacter();

        if (auto pCellIdComponent = m_world.try_get<CellIdComponent>(entity); pCellIdComponent)
        {
            m_world.GetDispatcher().trigger(CharacterInteriorCellChangeEvent{pPlayer, entity, message.CellId});
        }
    }

    auto characterView = m_world.view<CellIdComponent, CharacterComponent, OwnerComponent>();
    for (auto character : characterView)
    {
        const auto& ownedComponent = characterView.get<OwnerComponent>(character);

        if (ownedComponent.GetOwner() == pPlayer)
            continue;

        if (message.CellId != characterView.get<CellIdComponent>(character).Cell)
            continue;

        CharacterSpawnRequest spawnMessage;
        CharacterService::Serialize(m_world, character, &spawnMessage);

        pPlayer->Send(spawnMessage);
    }

    SendPlayerCellChanged(pPlayer);
}

void PlayerService::OnPlayerRespawnRequest(const PacketEvent<PlayerRespawnRequest>& acMessage) const noexcept
{
    float goldLossFactor = fGoldLossFactor.as_float();

    auto character = acMessage.pPlayer->GetCharacter();
    if (!character)
        return;

    const auto entity = m_world.TryResolveEntity(World::ToInteger(*character));
    if (!entity)
    {
        spdlog::warn("Respawn request references invalid player entity {:X}", World::ToInteger(*character));
        return;
    }

    if (auto* pAnimationComponent = m_world.try_get<AnimationComponent>(*entity))
    {
        pAnimationComponent->Actions.clear();
        pAnimationComponent->CurrentAction = {};
        pAnimationComponent->ActionsReplayCache.Clear();
    }

    auto view = m_world.view<InventoryComponent>();

    const auto it = view.find(*entity);

    if (it != view.end())
    {
        if (goldLossFactor != 0.0)
        {
            auto& inventoryComponent = view.get<InventoryComponent>(*it);

            GameId goldId(0, 0xF);
            int32_t goldCount = inventoryComponent.Content.GetEntryCountById(goldId);
            int32_t goldToRemove = static_cast<int32_t>(goldCount * goldLossFactor);

            Inventory::Entry entry{};
            entry.BaseId = goldId;
            entry.Count = -goldToRemove;

            inventoryComponent.Content.AddOrRemoveEntry(entry);

            NotifyInventoryChanges notifyInventoryChanges{};
            notifyInventoryChanges.ServerId = World::ToInteger(*entity);
            notifyInventoryChanges.Item = entry;

            // Exclude respawned player from inventory changes notification...
            if (!GameServer::Get()->SendToPlayersInRange(notifyInventoryChanges, *entity, acMessage.GetSender()))
                spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);

            // ...and instead, send NotifyPlayerRespawn so that the client can print a message.
            NotifyPlayerRespawn notifyPlayerRespawn{};
            notifyPlayerRespawn.GoldLost = goldToRemove;

            acMessage.pPlayer->Send(notifyPlayerRespawn);
        }

        // Let all other players in cell respawn this player, since the body state seems to be bugged otherwise
        NotifyRespawn notifyRespawn{};
        notifyRespawn.ActorId = World::ToInteger(*entity);

        if (!GameServer::Get()->SendToPlayersInRange(notifyRespawn, *entity, acMessage.GetSender()))
            spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
    }
}

void PlayerService::OnPlayerLevelRequest(const PacketEvent<PlayerLevelRequest>& acMessage) const noexcept
{
    acMessage.pPlayer->SetLevel(acMessage.Packet.NewLevel);

    NotifyPlayerLevel notify{};
    notify.PlayerId = acMessage.pPlayer->GetId();
    notify.NewLevel = acMessage.Packet.NewLevel;

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void PlayerService::OnPartyMemberDownedRequest(const PacketEvent<PartyMemberDownedRequest>& acMessage) const noexcept
{
    auto* pPlayer = acMessage.pPlayer;
    if (!pPlayer)
        return;

    NotifyPartyMemberDowned notify{};
    notify.PlayerId = pPlayer->GetId();
    notify.IsDowned = acMessage.Packet.IsDowned;

    if (auto character = pPlayer->GetCharacter())
    {
        notify.ServerId = World::ToInteger(*character);
        if (const auto* pMovement = m_world.try_get<MovementComponent>(*character))
        {
            notify.PositionX = pMovement->Position.x;
            notify.PositionY = pMovement->Position.y;
            notify.PositionZ = pMovement->Position.z;
        }
    }
    else
    {
        notify.ServerId = 0;
    }

    const auto& cellComponent = pPlayer->GetCellComponent();
    notify.WorldSpaceId = cellComponent.WorldSpaceId;
    notify.CellId = cellComponent.Cell;

    GameServer::Get()->SendToParty(notify, pPlayer->GetParty(), acMessage.GetSender());
}

void PlayerService::OnPlayerProfileImageUpdate(const PacketEvent<PlayerProfileImageUpdateRequest>& acMessage) const noexcept
{
    auto* pPlayer = acMessage.pPlayer;
    if (!pPlayer)
        return;

    const auto& imageData = acMessage.Packet.ImageData;

    constexpr size_t cMaxAvatarBytes = 256u * 1024u;
    if (imageData.size() > cMaxAvatarBytes)
    {
        spdlog::warn("[PlayerService] Avatar upload from player {} exceeded {} bytes ({} received)", pPlayer->GetId(), cMaxAvatarBytes, imageData.size());
        return;
    }

    auto sanitized = imageData;
    if (!sanitized.empty())
    {
        if (sanitized.rfind("data:image", 0) != 0)
        {
            spdlog::warn("[PlayerService] Avatar upload from player {} rejected due to invalid data URI prefix", pPlayer->GetId());
            return;
        }
    }

    pPlayer->SetAvatar(std::move(sanitized));

    NotifyPlayerProfileImage notify{};
    notify.PlayerId = pPlayer->GetId();
    notify.Avatar = pPlayer->GetAvatar();

    GameServer::Get()->SendToPlayers(notify);

    if (m_world.ctx().contains<LoginService>())
    {
        auto& loginService = m_world.ctx().at<LoginService>();
        loginService.SetAvatar(pPlayer->GetUsername(), pPlayer->GetAvatar());
    }
}
