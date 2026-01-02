#include "DropService.h"

#include <World.h>
#include <Components.h>
#include <GameServer.h>
#include <Game/Player.h>
#include <Messages/NotifyActorDrop.h>
#include <Messages/NotifyDroppedItemPickedUp.h>
#include <Messages/NotifyDroppedItems.h>
#include <Messages/NotifyDroppedItemMove.h>
#include <Messages/NotifyDroppedItemPhysicsDisabled.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Setting.h>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/ViewBuffer.hpp>
#include <glm/glm.hpp>
#include <Events/UpdateEvent.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <memory>
#include <string>

#include <sqlite3.h>
#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <unistd.h>
#endif

namespace
{
Console::Setting bEnableItemDrops{"Gameplay:bEnableItemDrops", "(Experimental) Syncs dropped items by players", true};

constexpr float kDropForwardOffset = 35.f;
constexpr float kDropVerticalOffset = 5.f;
constexpr double kCleanupIntervalSeconds = 60.0;
constexpr int64_t kDropExpirySeconds = 7 * 24 * 60 * 60;
constexpr int64_t kCreationEnginePickupExpirySeconds = 24 * 60 * 60;

Vector3_NetQuantize ToNetVector(const glm::vec3& aVector) noexcept
{
    Vector3_NetQuantize value{};
    value.x = aVector.x;
    value.y = aVector.y;
    value.z = aVector.z;
    return value;
}

struct StatementDeleter
{
    void operator()(sqlite3_stmt* apStatement) const noexcept
    {
        if (apStatement)
            sqlite3_finalize(apStatement);
    }
};

using StatementPtr = std::unique_ptr<sqlite3_stmt, StatementDeleter>;

StatementPtr PrepareStatement(sqlite3* apDatabase, const char* acSql) noexcept
{
    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(apDatabase, acSql, -1, &pStatement, nullptr) != SQLITE_OK)
        return nullptr;

    return StatementPtr(pStatement);
}

void BindGuid(sqlite3_stmt* apStatement, int aIndex, const Guid& acGuid) noexcept
{
    if (acGuid.IsEmpty())
        sqlite3_bind_null(apStatement, aIndex);
    else
        sqlite3_bind_blob(apStatement, aIndex, acGuid.Bytes.data(), static_cast<int>(acGuid.Bytes.size()), SQLITE_TRANSIENT);
}

Guid GuidFromColumn(sqlite3_stmt* apStatement, int aIndex) noexcept
{
    Guid value{};
    const void* pBlob = sqlite3_column_blob(apStatement, aIndex);
    const int blobSize = sqlite3_column_bytes(apStatement, aIndex);
    if (pBlob && blobSize == static_cast<int>(value.Bytes.size()))
        std::memcpy(value.Bytes.data(), pBlob, value.Bytes.size());
    else
        value.Clear();
    return value;
}

bool EnsureColumnExists(sqlite3* apDatabase, const char* acSql) noexcept
{
    if (!apDatabase || !acSql)
        return false;

    char* pError = nullptr;
    const int result = sqlite3_exec(apDatabase, acSql, nullptr, nullptr, &pError);
    if (result == SQLITE_OK)
        return true;

    std::string errorMessage = pError ? pError : "";
    sqlite3_free(pError);

    if (result == SQLITE_ERROR && errorMessage.find("duplicate column name") != std::string::npos)
        return true;

    spdlog::error("DropService: schema migration failed: {}", errorMessage.empty() ? "unknown error" : errorMessage);
    return false;
}

TiltedPhoques::Vector<GameId> FetchCreationEnginePickupsForCell(sqlite3* apDatabase, const GameId& acCellId, bool aHasWorldFilter, const GameId& acWorldId) noexcept
{
    TiltedPhoques::Vector<GameId> result{};
    if (!apDatabase || !acCellId)
        return result;

    const char* sql = aHasWorldFilter
        ? "SELECT engine_mod_id, engine_base_id FROM creation_engine_pickups WHERE cell_mod_id = ?1 AND cell_base_id = ?2 AND world_mod_id = ?3 AND world_base_id = ?4;"
        : "SELECT engine_mod_id, engine_base_id FROM creation_engine_pickups WHERE cell_mod_id = ?1 AND cell_base_id = ?2;";

    StatementPtr statement = PrepareStatement(apDatabase, sql);
    if (!statement)
        return result;

    sqlite3_bind_int(statement.get(), 1, static_cast<int>(acCellId.ModId));
    sqlite3_bind_int(statement.get(), 2, static_cast<int>(acCellId.BaseId));
    if (aHasWorldFilter)
    {
        sqlite3_bind_int(statement.get(), 3, static_cast<int>(acWorldId.ModId));
        sqlite3_bind_int(statement.get(), 4, static_cast<int>(acWorldId.BaseId));
    }

    while (sqlite3_step(statement.get()) == SQLITE_ROW)
    {
        GameId ref{};
        ref.ModId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 0));
        ref.BaseId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 1));
        if (ref)
            result.push_back(ref);
    }

    return result;
}

std::string SerializeEntryBlob(const Inventory::Entry& acEntry) noexcept
{
    TiltedPhoques::Buffer buffer(1 << 12);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    Inventory::Entry normalized = acEntry;
    normalized.Serialize(writer);
    const auto size = static_cast<size_t>(writer.Size());
    std::string blob(size, '\0');
    std::memcpy(blob.data(), buffer.GetWriteData(), size);
    return blob;
}

Inventory::Entry DeserializeEntryBlob(const void* apBlob, int aSize) noexcept
{
    Inventory::Entry entry{};
    if (!apBlob || aSize <= 0)
        return entry;

    auto* pData = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(apBlob));
    TiltedPhoques::ViewBuffer view(pData, static_cast<size_t>(aSize));
    TiltedPhoques::Buffer::Reader reader(&view);
    entry.Deserialize(reader);
    return entry;
}

constexpr const char* kCreateDropsTableSql = R"SQL(
    CREATE TABLE IF NOT EXISTS server_drops(
        server_drop_id INTEGER PRIMARY KEY AUTOINCREMENT,
        server_id INTEGER NOT NULL,
        actor_form_id INTEGER NOT NULL,
        origin_player_id INTEGER NOT NULL,
        client_drop_id BLOB NULL,
        item_type INTEGER NOT NULL DEFAULT 0,
        item_mod_id INTEGER NOT NULL,
        item_base_id INTEGER NOT NULL,
        count INTEGER NOT NULL CHECK(count > 0),
        cell_mod_id INTEGER NOT NULL,
        cell_base_id INTEGER NOT NULL,
        world_mod_id INTEGER NOT NULL,
        world_base_id INTEGER NOT NULL,
        reference_mod_id INTEGER NOT NULL DEFAULT 0,
        reference_base_id INTEGER NOT NULL DEFAULT 0,
        has_location INTEGER NOT NULL DEFAULT 0,
        pos_x REAL,
        pos_y REAL,
        pos_z REAL,
        has_rotation INTEGER NOT NULL DEFAULT 0,
        rot_x REAL,
        rot_y REAL,
        rot_z REAL,
        is_active INTEGER NOT NULL DEFAULT 1,
        version INTEGER NOT NULL DEFAULT 1,
        item_blob BLOB NOT NULL,
        created_at INTEGER DEFAULT (strftime('%s','now')),
        updated_at INTEGER DEFAULT (strftime('%s','now'))
    );
    CREATE INDEX IF NOT EXISTS idx_server_drops_cell_active ON server_drops(cell_mod_id, cell_base_id) WHERE is_active = 1;
    CREATE UNIQUE INDEX IF NOT EXISTS idx_server_drops_client ON server_drops(origin_player_id, client_drop_id) WHERE client_drop_id IS NOT NULL AND is_active = 1;

    CREATE TABLE IF NOT EXISTS server_drop_history(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        server_drop_id INTEGER NOT NULL,
        action TEXT NOT NULL,
        performed_by INTEGER,
        details TEXT,
        created_at INTEGER DEFAULT (strftime('%s','now')),
        FOREIGN KEY(server_drop_id) REFERENCES server_drops(server_drop_id)
    );
    CREATE INDEX IF NOT EXISTS idx_server_drop_history_drop ON server_drop_history(server_drop_id);

    CREATE TABLE IF NOT EXISTS player_inventory(
        player_id INTEGER NOT NULL,
        item_mod_id INTEGER NOT NULL,
        item_base_id INTEGER NOT NULL,
        stack_id TEXT NOT NULL,
        count INTEGER NOT NULL DEFAULT 0 CHECK(count >= 0),
        entry_blob BLOB NOT NULL,
        updated_at INTEGER DEFAULT (strftime('%s','now')),
        PRIMARY KEY(player_id, stack_id)
    );
    CREATE INDEX IF NOT EXISTS idx_player_inventory_item ON player_inventory(player_id, item_mod_id, item_base_id);

    CREATE TABLE IF NOT EXISTS drop_bindings(
        server_drop_id INTEGER NOT NULL,
        player_id INTEGER NOT NULL,
        client_ref_handle INTEGER NOT NULL,
        bound_at INTEGER DEFAULT (strftime('%s','now')),
        PRIMARY KEY(server_drop_id, player_id),
        FOREIGN KEY(server_drop_id) REFERENCES server_drops(server_drop_id)
    );
)SQL";

constexpr const char* kCreateCreationEngineTableSql = R"SQL(
    CREATE TABLE IF NOT EXISTS creation_engine_pickups(
        engine_mod_id INTEGER NOT NULL,
        engine_base_id INTEGER NOT NULL,
        cell_mod_id INTEGER NOT NULL,
        cell_base_id INTEGER NOT NULL,
        world_mod_id INTEGER NOT NULL DEFAULT 0,
        world_base_id INTEGER NOT NULL DEFAULT 0,
        picked_by INTEGER NOT NULL,
        picked_at INTEGER DEFAULT (strftime('%s','now')),
        PRIMARY KEY(engine_mod_id, engine_base_id)
    );
    CREATE INDEX IF NOT EXISTS idx_creation_engine_pickups_cell ON creation_engine_pickups(cell_mod_id, cell_base_id);
)SQL";

std::filesystem::path ResolveExecutableDirectory() noexcept
{
    namespace fs = std::filesystem;

#if defined(_WIN32)
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length != 0 && length < buffer.size())
    {
        return fs::path(buffer.data()).parent_path();
    }
#else
    std::error_code ec;
    auto exePath = fs::canonical("/proc/self/exe", ec);
    if (!ec)
        return exePath.parent_path();
#endif

    std::error_code ec;
    const auto current = fs::current_path(ec);
    if (!ec)
        return current;

    return {};
}

std::filesystem::path ResolveItemsDatabasePath() noexcept
{
    namespace fs = std::filesystem;

    if (auto exeDirectory = ResolveExecutableDirectory(); !exeDirectory.empty())
    {
        return exeDirectory / "items.db";
    }

#if defined(_WIN32)
    if (char* localAppData = nullptr; _dupenv_s(&localAppData, nullptr, "LOCALAPPDATA") == 0 && localAppData)
    {
        fs::path path(localAppData);
        free(localAppData);
        path /= "SkyrimTogether";
        path /= "Server";
        path /= "items.db";
        return path;
    }
#else
    if (const char* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && xdgDataHome[0] != '\0')
    {
        return fs::path(xdgDataHome) / "skyrimtogether" / "server" / "items.db";
    }

    if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
    {
        return fs::path(home) / ".local" / "share" / "skyrimtogether" / "server" / "items.db";
    }
#endif

    std::error_code ec;
    const auto current = fs::current_path(ec);
    if (!ec)
        return current / "data" / "items.db";

    return fs::path("items.db");
}

std::filesystem::path ResolveCreationEngineDatabasePath() noexcept
{
    namespace fs = std::filesystem;

    const fs::path itemsPath = ResolveItemsDatabasePath();
    if (!itemsPath.empty())
        return itemsPath.parent_path() / "items_creation_engine.db";

    return fs::path("items_creation_engine.db");
}
}

DropService::DropService(World& aWorld, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
{
    m_requestDropConnection = aDispatcher.sink<PacketEvent<RequestActorDrop>>().connect<&DropService::OnDropRequest>(this);
    m_requestPickupConnection = aDispatcher.sink<PacketEvent<RequestPickupDroppedItem>>().connect<&DropService::OnPickupRequest>(this);
    m_requestDroppedItemsConnection = aDispatcher.sink<PacketEvent<RequestDroppedItems>>().connect<&DropService::OnDroppedItemsRequest>(this);
    m_requestDropMoveConnection = aDispatcher.sink<PacketEvent<RequestDroppedItemMove>>().connect<&DropService::OnDropMoveRequest>(this);
    m_requestDropPhysicsDisabledConnection = aDispatcher.sink<PacketEvent<RequestDroppedItemPhysicsDisabled>>().connect<&DropService::OnDropPhysicsDisabledRequest>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&DropService::OnUpdate>(this);

    if (!InitializeDatabase())
        spdlog::error("DropService: failed to initialize drop persistence database");
    else
        LoadPersistedDrops();

    if (!InitializeCreationEngineDatabase())
        spdlog::error("DropService: failed to initialize creation engine pickup database");
}

DropService::~DropService()
{
    ShutdownCreationEngineDatabase();
    ShutdownDatabase();
}

void DropService::OnDropRequest(const PacketEvent<RequestActorDrop>& acMessage) noexcept
{
    if (!bEnableItemDrops)
        return;

    const auto& message = acMessage.Packet;

    const auto entity = m_world.TryResolveEntity(message.ServerId);
    if (!entity)
    {
        spdlog::warn("Drop requested for unknown entity {:X}", message.ServerId);
        return;
    }

    auto view = m_world.view<OwnerComponent>();
    const auto it = view.find(*entity);
    if (it == view.end())
    {
        spdlog::warn("Drop requested for entity {:X} without OwnerComponent", message.ServerId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    if (ownerComponent.GetOwner() != acMessage.pPlayer)
    {
        spdlog::warn("Drop denied for {:X}: player {:X} not owner", message.ServerId, acMessage.pPlayer->GetConnectionId());
        return;
    }

    Player* pPlayer = ownerComponent.GetOwner();
    if (!pPlayer)
    {
        spdlog::warn("Drop denied for {:X}: missing owner player", message.ServerId);
        return;
    }

    const FormIdComponent* pFormIdComponent = m_world.try_get<FormIdComponent>(*entity);
    entt::entity notifyEntity = *entity;
    if (!pFormIdComponent)
    {
        if (auto characterEntity = acMessage.pPlayer->GetCharacter(); characterEntity && m_world.valid(*characterEntity))
        {
            notifyEntity = *characterEntity;
            pFormIdComponent = m_world.try_get<FormIdComponent>(*characterEntity);
        }
    }

    uint32_t actorFormId = pFormIdComponent ? pFormIdComponent->Id : message.ActorFormId;
    if (!pFormIdComponent)
        spdlog::warn("DropService: drop requested for entity {:X} without FormIdComponent (using fallback {:X})", message.ServerId, actorFormId);

    auto* pCellComponent = m_world.try_get<CellIdComponent>(*entity);
    if (!pCellComponent)
        spdlog::warn("DropService: drop requested for entity {:X} without CellIdComponent", message.ServerId);

    GameId fallbackCellId{};
    GameId fallbackWorldId{};
    const auto& playerCellComponent = acMessage.pPlayer->GetCellComponent();
    if (playerCellComponent.Cell)
        fallbackCellId = playerCellComponent.Cell;
    if (playerCellComponent.WorldSpaceId)
        fallbackWorldId = playerCellComponent.WorldSpaceId;

    glm::vec3 authoritativePosition{};
    glm::vec3 authoritativeRotation{};
    bool hasAuthoritativePosition = false;
    bool hasAuthoritativeRotation = false;

    if (auto* pMovementComponent = m_world.try_get<MovementComponent>(*entity))
    {
        const glm::vec3 movementPos = pMovementComponent->Position;
        const glm::vec3 movementRot = pMovementComponent->Rotation;

        const bool posFinite = std::isfinite(movementPos.x) && std::isfinite(movementPos.y) && std::isfinite(movementPos.z);
        const bool rotFinite = std::isfinite(movementRot.x) && std::isfinite(movementRot.y) && std::isfinite(movementRot.z);
        const float posLenSq = movementPos.x * movementPos.x + movementPos.y * movementPos.y + movementPos.z * movementPos.z;

        if (posFinite && posLenSq > 1.f)
        {
            const float yaw = movementRot.z;
            const glm::vec3 forward{std::cos(yaw), std::sin(yaw), 0.f};
            authoritativePosition = movementPos + forward * kDropForwardOffset;
            authoritativePosition.z += kDropVerticalOffset;
            hasAuthoritativePosition = true;
        }

        if (rotFinite)
        {
            authoritativeRotation = movementRot;
            hasAuthoritativeRotation = true;
        }
    }

    if (!hasAuthoritativePosition && message.HasLocation)
    {
        authoritativePosition = glm::vec3(message.Location.x, message.Location.y, message.Location.z);
        hasAuthoritativePosition = true;
    }

    if (!hasAuthoritativeRotation && message.HasRotation)
    {
        authoritativeRotation = glm::vec3(message.Rotation.x, message.Rotation.y, message.Rotation.z);
        hasAuthoritativeRotation = true;
    }

    ActiveDrop drop{};
    drop.ServerId = notifyEntity != entt::null ? World::ToInteger(notifyEntity) : message.ServerId;
    drop.ActorFormId = actorFormId;
    drop.OriginPlayerId = pPlayer->GetId();
    drop.DropEntry = message.Item;
    drop.PickupEntry = message.Item;
    if (drop.PickupEntry.Count < 0)
        drop.PickupEntry.Count = -drop.PickupEntry.Count;
    drop.HasLocation = hasAuthoritativePosition;
    if (drop.HasLocation)
        drop.Location = ToNetVector(authoritativePosition);
    drop.HasRotation = hasAuthoritativeRotation;
    if (drop.HasRotation)
        drop.Rotation = ToNetVector(authoritativeRotation);
    if (message.CellId)
        drop.CellId = message.CellId;
    else if (pCellComponent && pCellComponent->Cell)
        drop.CellId = pCellComponent->Cell;
    else
        drop.CellId = fallbackCellId;

    if (message.WorldSpaceId)
        drop.WorldSpaceId = message.WorldSpaceId;
    else if (pCellComponent && pCellComponent->WorldSpaceId)
        drop.WorldSpaceId = pCellComponent->WorldSpaceId;
    else
        drop.WorldSpaceId = fallbackWorldId;
    drop.ReferenceId = message.ReferenceId;
    drop.ClientDropId = message.ClientDropId;
    drop.Version = 1;

    const auto& ownerCell = pPlayer->GetCellComponent();
    if (ownerCell.Cell)
    {
        const bool cellMismatch = drop.CellId && drop.CellId != ownerCell.Cell;
        if (!drop.CellId || cellMismatch)
        {
            if (cellMismatch)
            {
                spdlog::debug("DropService: correcting drop cell for player {} from {:X}:{:X} to {:X}:{:X}", pPlayer->GetId(), drop.CellId.ModId, drop.CellId.BaseId, ownerCell.Cell.ModId,
                              ownerCell.Cell.BaseId);
            }
            drop.CellId = ownerCell.Cell;
        }

        if (ownerCell.WorldSpaceId)
        {
            if (!drop.WorldSpaceId || drop.WorldSpaceId != ownerCell.WorldSpaceId)
                drop.WorldSpaceId = ownerCell.WorldSpaceId;
        }
        else if (drop.WorldSpaceId)
        {
            drop.WorldSpaceId = {};
        }
    }

    const int32_t dropCount = drop.PickupEntry.Count;
    if (dropCount <= 0)
    {
        spdlog::warn("DropService: invalid drop count {} for actor {:X}", dropCount, message.ServerId);
        return;
    }

    auto buildNotify = [](const ActiveDrop& acDrop) {
        NotifyActorDrop notify{};
        notify.ServerId = acDrop.ServerId;
        notify.ActorFormId = acDrop.ActorFormId;
        notify.Item = acDrop.DropEntry;
        notify.DropId = acDrop.DropId;
        notify.SpawnEpoch = acDrop.SpawnEpoch;
        notify.HasLocation = acDrop.HasLocation;
        if (notify.HasLocation)
            notify.Location = acDrop.Location;
        notify.HasRotation = acDrop.HasRotation;
        if (notify.HasRotation)
            notify.Rotation = acDrop.Rotation;
        notify.CellId = acDrop.CellId;
        notify.WorldSpaceId = acDrop.WorldSpaceId;
        notify.ReferenceId = acDrop.ReferenceId;
        return notify;
    };

    if (!drop.ClientDropId.IsEmpty())
    {
        if (auto existingDropId = FindExistingDropId(drop.OriginPlayerId, drop.ClientDropId))
        {
            if (auto* pExisting = ResolveActiveDrop(*existingDropId))
            {
                // Bump spawn epoch by incrementing version in DB and mirror in memory
                {
                    constexpr const char* cBumpSql = "UPDATE server_drops SET version = version + 1, updated_at = strftime('%s','now') WHERE server_drop_id = ?1;";
                    StatementPtr stmt = PrepareStatement(m_pDatabase, cBumpSql);
                    if (stmt)
                    {
                        sqlite3_bind_int64(stmt.get(), 1, static_cast<sqlite3_int64>(*existingDropId));
                        sqlite3_step(stmt.get());
                    }
                }
                pExisting->Version += 1;
                pExisting->SpawnEpoch = pExisting->Version;

                NotifyActorDrop notify = buildNotify(*pExisting);
                const bool broadcasted = GameServer::Get()->SendToPlayersInRange(notify, notifyEntity, nullptr);
                if (!broadcasted)
                {
                    spdlog::error("{}: SendToPlayersInRange failed (duplicate drop)", __FUNCTION__);
                    GameServer::Get()->SendToPlayers(notify, nullptr);
                }
                else
                {
                    auto* pNotifyCell = m_world.try_get<CellIdComponent>(notifyEntity);
                    const bool playerCellReady = static_cast<bool>(pPlayer->GetCellComponent());
                    bool inRange = false;
                    if (pNotifyCell && playerCellReady)
                    {
                        bool isDragon = false;
                        if (const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(notifyEntity))
                            isDragon = pCharacterComponent->IsDragon();
                        inRange = pNotifyCell->IsInRange(pPlayer->GetCellComponent(), isDragon);
                    }

                    if (!pNotifyCell || !playerCellReady || !inRange)
                        pPlayer->Send(notify);
                }
            }
            else
            {
                spdlog::warn("DropService: duplicate drop {} found but failed to load active state", *existingDropId);
            }
            return;
        }
    }

    if (!BeginTransaction())
    {
        spdlog::error("DropService: failed to begin transaction for actor {:X}", message.ServerId);
        return;
    }

    uint64_t serverDropId = 0;
    if (!InsertDrop(drop, serverDropId))
    {
        spdlog::error("DropService: failed to persist drop for actor {:X}", message.ServerId);
        RollbackTransaction();
        return;
    }
    drop.DropId = serverDropId;

    std::string historyDetails;
    if (drop.HasLocation)
    {
        historyDetails = fmt::format("cell={:X}:{:X}, world={:X}:{:X}, pos=({:.2f}, {:.2f}, {:.2f})", drop.CellId.ModId, drop.CellId.BaseId, drop.WorldSpaceId.ModId, drop.WorldSpaceId.BaseId, drop.Location.x,
                                     drop.Location.y, drop.Location.z);
    }
    else
    {
        historyDetails = fmt::format("cell={:X}:{:X}, world={:X}:{:X}", drop.CellId.ModId, drop.CellId.BaseId, drop.WorldSpaceId.ModId, drop.WorldSpaceId.BaseId);
    }

    if (!InsertDropHistory(drop.DropId, "create", drop.OriginPlayerId, historyDetails))
    {
        spdlog::error("DropService: failed to insert history for drop {}", drop.DropId);
        RollbackTransaction();
        return;
    }

    if (!CommitTransaction())
    {
        spdlog::error("DropService: commit failed for drop {}", drop.DropId);
        RollbackTransaction();
        return;
    }
    TrackActiveDrop(drop);

    NotifyActorDrop notify = buildNotify(drop);

    const bool broadcasted = GameServer::Get()->SendToPlayersInRange(notify, notifyEntity, nullptr);
    if (!broadcasted)
    {
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
        GameServer::Get()->SendToPlayers(notify, nullptr);
    }
    else
    {
        auto* pNotifyCell = m_world.try_get<CellIdComponent>(notifyEntity);
        const bool playerCellReady = static_cast<bool>(pPlayer->GetCellComponent());
        bool inRange = false;
        if (pNotifyCell && playerCellReady)
        {
            bool isDragon = false;
            if (const auto* pCharacterComponent = m_world.try_get<CharacterComponent>(notifyEntity))
                isDragon = pCharacterComponent->IsDragon();
            inRange = pNotifyCell->IsInRange(pPlayer->GetCellComponent(), isDragon);
        }

        if (!pNotifyCell || !playerCellReady || !inRange)
            pPlayer->Send(notify);
    }

    spdlog::debug("DropService: drop {} tracked for actor {:X}, player {}, cell {:X}:{:X}, world {:X}:{:X}", drop.DropId, drop.ServerId, drop.OriginPlayerId, drop.CellId.ModId, drop.CellId.BaseId,
                  drop.WorldSpaceId.ModId, drop.WorldSpaceId.BaseId);
}

void DropService::OnPickupRequest(const PacketEvent<RequestPickupDroppedItem>& acMessage) noexcept
{
    if (!bEnableItemDrops)
        return;

    const auto& message = acMessage.Packet;
    spdlog::debug("DropService: pickup request actor {:X} drop {} ref {:X}:{:X}", message.ServerId, message.DropId, message.ReferenceId.ModId, message.ReferenceId.BaseId);

    if (!message.DropId)
    {
        HandleUntrackedPickupRequest(acMessage);
        return;
    }

    ActiveDrop* pDrop = ResolveActiveDrop(message.DropId);
    if (!pDrop)
    {
        spdlog::warn("DropService: pickup requested for unknown drop {}", message.DropId);
        if (message.Item.BaseId)
        {
            Inventory::Entry correctionEntry = message.Item;
            const int32_t pickedCount = correctionEntry.Count == 0 ? 1 : std::abs(correctionEntry.Count);
            correctionEntry.Count = -pickedCount;

            NotifyInventoryChanges correction{};
            correction.ServerId = message.ServerId;
            correction.Item = correctionEntry;
            correction.Silent = true;
            acMessage.pPlayer->Send(correction);
        }
        return;
    }

    const auto pickerEntity = m_world.TryResolveEntity(message.ServerId);
    if (!pickerEntity)
    {
        spdlog::warn("DropService: pickup requested for missing entity {:X}", message.ServerId);
        return;
    }

    auto view = m_world.view<OwnerComponent>();
    const auto it = view.find(*pickerEntity);
    if (it == view.end())
    {
        spdlog::warn("DropService: pickup requested for entity {:X} without OwnerComponent", message.ServerId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    if (ownerComponent.GetOwner() != acMessage.pPlayer)
    {
        spdlog::warn("DropService: pickup denied for {:X}: player {:X} not owner", message.ServerId, acMessage.pPlayer->GetConnectionId());
        return;
    }

    Player* pPicker = ownerComponent.GetOwner();
    if (!pPicker)
    {
        spdlog::warn("DropService: pickup denied for {:X}: missing owner player", message.ServerId);
        return;
    }

    if (!BeginTransaction())
    {
        spdlog::error("DropService: failed to begin pickup transaction for drop {}", message.DropId);
        if (pDrop->PickupEntry.BaseId)
        {
            Inventory::Entry correctionEntry = pDrop->PickupEntry;
            const int32_t pickedCount = correctionEntry.Count == 0 ? 1 : std::abs(correctionEntry.Count);
            correctionEntry.Count = -pickedCount;

            NotifyInventoryChanges correction{};
            correction.ServerId = message.ServerId;
            correction.Item = correctionEntry;
            correction.Silent = true;
            acMessage.pPlayer->Send(correction);
        }
        return;
    }

    const std::string historyDetails = fmt::format("picked_by={}, cell={:04X}:{:04X}", pPicker->GetId(), pDrop->CellId.ModId, pDrop->CellId.BaseId);
    if (!InsertDropHistory(pDrop->DropId, "pickup", pPicker->GetId(), historyDetails))
    {
        spdlog::error("DropService: failed to insert pickup history for drop {}", pDrop->DropId);
        RollbackTransaction();
        if (pDrop->PickupEntry.BaseId)
        {
            Inventory::Entry correctionEntry = pDrop->PickupEntry;
            const int32_t pickedCount = correctionEntry.Count == 0 ? 1 : std::abs(correctionEntry.Count);
            correctionEntry.Count = -pickedCount;

            NotifyInventoryChanges correction{};
            correction.ServerId = message.ServerId;
            correction.Item = correctionEntry;
            correction.Silent = true;
            acMessage.pPlayer->Send(correction);
        }
        return;
    }

    if (!MarkDropInactive(pDrop->DropId))
    {
        spdlog::warn("DropService: drop {} already inactive during pickup", pDrop->DropId);
        RollbackTransaction();
        if (pDrop->PickupEntry.BaseId)
        {
            Inventory::Entry correctionEntry = pDrop->PickupEntry;
            const int32_t pickedCount = correctionEntry.Count == 0 ? 1 : std::abs(correctionEntry.Count);
            correctionEntry.Count = -pickedCount;

            NotifyInventoryChanges correction{};
            correction.ServerId = message.ServerId;
            correction.Item = correctionEntry;
            correction.Silent = true;
            acMessage.pPlayer->Send(correction);
        }
        return;
    }

    if (!CommitTransaction())
    {
        spdlog::error("DropService: pickup commit failed for drop {}", pDrop->DropId);
        RollbackTransaction();
        if (pDrop->PickupEntry.BaseId)
        {
            Inventory::Entry correctionEntry = pDrop->PickupEntry;
            const int32_t pickedCount = correctionEntry.Count == 0 ? 1 : std::abs(correctionEntry.Count);
            correctionEntry.Count = -pickedCount;

            NotifyInventoryChanges correction{};
            correction.ServerId = message.ServerId;
            correction.Item = correctionEntry;
            correction.Silent = true;
            acMessage.pPlayer->Send(correction);
        }
        return;
    }
    ActiveDrop dropCopy = *pDrop;
    if (dropCopy.Type == ServerItemType::CreationEngine)
    {
        const GameId pickupRef = message.ReferenceId ? message.ReferenceId : dropCopy.ReferenceId;
        if (pickupRef && dropCopy.CellId && m_pCreationEngineDatabase)
        {
            if (!RecordCreationEnginePickup(pickupRef, dropCopy.CellId, dropCopy.WorldSpaceId, pPicker->GetId()) &&
                !IsCreationEnginePickupRecorded(pickupRef))
            {
                spdlog::warn("DropService: failed to record creation engine pickup for drop {}", dropCopy.DropId);
            }
        }
    }
    RemoveActiveDrop(pDrop->DropId);

    NotifyDroppedItemPickedUp notify{};
    notify.ServerId = message.ServerId;
    notify.Item = dropCopy.PickupEntry;
    notify.DropId = dropCopy.DropId;
    if (dropCopy.HasLocation)
    {
        notify.HasLocation = true;
        notify.Location = dropCopy.Location;
    }
    if (dropCopy.HasRotation)
    {
        notify.HasRotation = true;
        notify.Rotation = dropCopy.Rotation;
    }
    notify.CellId = dropCopy.CellId;
    notify.WorldSpaceId = dropCopy.WorldSpaceId;
    notify.ReferenceId = message.ReferenceId ? message.ReferenceId : dropCopy.ReferenceId;

    BroadcastPickup(notify);
    spdlog::debug("DropService: drop {} picked up by actor {:X}", dropCopy.DropId, message.ServerId);
}

void DropService::OnDroppedItemsRequest(const PacketEvent<RequestDroppedItems>& acMessage) noexcept
{
    if (!bEnableItemDrops)
        return;

    const auto& request = acMessage.Packet;
    NotifyDroppedItems notify{};
    notify.RequestId = request.RequestId;
    spdlog::debug("DropService: drop sync request {} from player {:X}, all={}, cell {:X}:{:X}, discoveries={}", request.RequestId, acMessage.GetSender()->GetConnectionId(), request.RequestAll,
                  request.HasCellFilter ? request.CellId.ModId : 0, request.HasCellFilter ? request.CellId.BaseId : 0, request.Discoveries.size());

    if (!request.Discoveries.empty())
    {
        const bool useTransaction = BeginTransaction();
        bool hadFailure = false;

        for (const auto& entry : request.Discoveries)
        {
            if (!entry.ReferenceId || !entry.Item.BaseId || !entry.CellId)
                continue;

            if (IsCreationEnginePickupRecorded(entry.ReferenceId))
                continue;

            const auto existingIt = m_referenceDropIndex.find(entry.ReferenceId);
            if (existingIt != m_referenceDropIndex.end())
            {
                ActiveDrop* pExisting = ResolveActiveDrop(existingIt->second);
                if (!pExisting || pExisting->Type != ServerItemType::CreationEngine)
                    continue;

                const GameId previousCell = pExisting->CellId;
                pExisting->CellId = entry.CellId;
                pExisting->WorldSpaceId = entry.WorldSpaceId;
                if (entry.HasLocation)
                {
                    pExisting->HasLocation = true;
                    pExisting->Location = entry.Location;
                }
                if (entry.HasRotation)
                {
                    pExisting->HasRotation = true;
                    pExisting->Rotation = entry.Rotation;
                }

                if (previousCell != pExisting->CellId)
                {
                    EraseDropFromIndex(pExisting->DropId, previousCell);
                    IndexDrop(pExisting->DropId, pExisting->CellId);
                }

                if (!UpdateDropLocation(*pExisting) && useTransaction)
                    hadFailure = true;

                continue;
            }

            ActiveDrop drop{};
            drop.ServerId = 0;
            drop.ActorFormId = 0;
            drop.OriginPlayerId = acMessage.GetSender()->GetConnectionId();
            drop.Type = ServerItemType::CreationEngine;
            drop.DropEntry = entry.Item;
            drop.PickupEntry = entry.Item;
            if (drop.PickupEntry.Count == 0)
                drop.PickupEntry.Count = 1;
            drop.CellId = entry.CellId;
            drop.WorldSpaceId = entry.WorldSpaceId;
            drop.ReferenceId = entry.ReferenceId;
            drop.HasLocation = entry.HasLocation;
            if (entry.HasLocation)
                drop.Location = entry.Location;
            drop.HasRotation = entry.HasRotation;
            if (entry.HasRotation)
                drop.Rotation = entry.Rotation;

            uint64_t dropId = 0;
            if (!InsertDrop(drop, dropId))
            {
                spdlog::warn("DropService: failed to insert creation-engine item ref {:X}:{:X}", entry.ReferenceId.ModId, entry.ReferenceId.BaseId);
                if (useTransaction)
                    hadFailure = true;
                continue;
            }

            drop.DropId = dropId;
            drop.Version = 1;
            drop.SpawnEpoch = drop.Version;
            TrackActiveDrop(drop);
        }

        if (useTransaction)
        {
            if (!hadFailure)
                hadFailure = !CommitTransaction();

            if (hadFailure)
            {
                RollbackTransaction();
                LoadPersistedDrops();
                spdlog::warn("DropService: discovery transaction rolled back");
            }
        }
    }

    auto appendEntry = [&](const ActiveDrop& acDrop) {
        if (!request.RequestAll)
        {
            if (request.HasWorldSpaceFilter && acDrop.WorldSpaceId != request.WorldSpaceId)
                return;
        }

        notify.Entries.push_back(MakeNotifyEntry(acDrop));
    };

    if (!request.RequestAll && request.HasCellFilter)
    {
        auto indexIt = m_cellDropIndex.find(request.CellId);
        if (indexIt != m_cellDropIndex.end())
        {
            for (auto dropId : indexIt->second)
            {
                const auto dropIt = m_activeDrops.find(dropId);
                if (dropIt == m_activeDrops.end())
                    continue;
                appendEntry(dropIt->second);
            }
        }
    }
    else
    {
        for (const auto& [dropId, drop] : m_activeDrops)
            appendEntry(drop);
    }

    if (!request.RequestAll && request.HasCellFilter)
        notify.CreationEnginePickedUpReferences = FetchCreationEnginePickupsForCell(m_pCreationEngineDatabase, request.CellId, request.HasWorldSpaceFilter, request.WorldSpaceId);

    acMessage.pPlayer->Send(notify);
    spdlog::debug("DropService: sent {} drops and {} creation-engine pickups in response to request {}", notify.Entries.size(), notify.CreationEnginePickedUpReferences.size(), request.RequestId);
}

void DropService::OnDropMoveRequest(const PacketEvent<RequestDroppedItemMove>& acMessage) noexcept
{
    if (!bEnableItemDrops)
        return;

    const auto& message = acMessage.Packet;
    if (!message.HasLocation && !message.HasRotation)
        return;

    uint64_t resolvedDropId = message.DropId;
    if (!resolvedDropId && message.ReferenceId)
    {
        if (const auto it = m_referenceDropIndex.find(message.ReferenceId); it != m_referenceDropIndex.end())
            resolvedDropId = it->second;
    }

    NotifyDroppedItemMove notify{};
    notify.DropId = resolvedDropId;
    notify.HasLocation = message.HasLocation;
    if (notify.HasLocation)
        notify.Location = message.Location;
    notify.HasRotation = message.HasRotation;
    if (notify.HasRotation)
        notify.Rotation = message.Rotation;
    notify.HasVelocity = message.HasVelocity;
    if (notify.HasVelocity)
        notify.Velocity = message.Velocity;
    notify.HasAngularVelocity = message.HasAngularVelocity;
    if (notify.HasAngularVelocity)
        notify.AngularVelocity = message.AngularVelocity;
    notify.CellId = message.CellId;
    notify.WorldSpaceId = message.WorldSpaceId;
    notify.ReferenceId = message.ReferenceId;

    if (resolvedDropId)
    {
        ActiveDrop* pDrop = ResolveActiveDrop(resolvedDropId);
        if (!pDrop)
        {
            if (message.DropId)
            {
                spdlog::debug("DropService: move requested for unknown drop {}", message.DropId);
                return;
            }

            resolvedDropId = 0;
            notify.DropId = 0;
        }
        else
        {
            const GameId previousCell = pDrop->CellId;

            if (message.CellId)
                pDrop->CellId = message.CellId;
            if (message.WorldSpaceId)
                pDrop->WorldSpaceId = message.WorldSpaceId;
            const GameId previousRef = pDrop->ReferenceId;
            if (message.ReferenceId)
                pDrop->ReferenceId = message.ReferenceId;

            if (message.HasLocation)
            {
                pDrop->HasLocation = true;
                pDrop->Location = message.Location;
            }

            if (message.HasRotation)
            {
                pDrop->HasRotation = true;
                pDrop->Rotation = message.Rotation;
            }

            if (message.HasVelocity)
            {
                pDrop->HasVelocity = true;
                pDrop->Velocity = message.Velocity;
            }

            if (message.HasAngularVelocity)
            {
                pDrop->HasAngularVelocity = true;
                pDrop->AngularVelocity = message.AngularVelocity;
            }

            if (previousRef && previousRef != pDrop->ReferenceId)
                m_referenceDropIndex.erase(previousRef);
            if (pDrop->ReferenceId)
                m_referenceDropIndex[pDrop->ReferenceId] = pDrop->DropId;

            if (previousCell != pDrop->CellId)
            {
                EraseDropFromIndex(pDrop->DropId, previousCell);
                IndexDrop(pDrop->DropId, pDrop->CellId);
            }

            UpdateDropLocation(*pDrop);

            notify.DropId = pDrop->DropId;
            notify.HasLocation = pDrop->HasLocation;
            if (notify.HasLocation)
                notify.Location = pDrop->Location;
            notify.HasRotation = pDrop->HasRotation;
            if (notify.HasRotation)
                notify.Rotation = pDrop->Rotation;
            notify.HasVelocity = pDrop->HasVelocity;
            if (notify.HasVelocity)
                notify.Velocity = pDrop->Velocity;
            notify.HasAngularVelocity = pDrop->HasAngularVelocity;
            if (notify.HasAngularVelocity)
                notify.AngularVelocity = pDrop->AngularVelocity;
            notify.CellId = pDrop->CellId;
            notify.WorldSpaceId = pDrop->WorldSpaceId;
            notify.ReferenceId = pDrop->ReferenceId;
        }
    }

    if (auto characterEntity = acMessage.pPlayer->GetCharacter(); characterEntity && m_world.valid(*characterEntity))
    {
        if (!GameServer::Get()->SendToPlayersInRange(notify, *characterEntity, acMessage.pPlayer))
        {
            spdlog::error("{}: SendToPlayersInRange failed for drop move {}", __FUNCTION__, notify.DropId);
            GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
        }
    }
    else
    {
        GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
    }
}

void DropService::OnDropPhysicsDisabledRequest(const PacketEvent<RequestDroppedItemPhysicsDisabled>& acMessage) noexcept
{
    if (!bEnableItemDrops)
        return;

    const auto& message = acMessage.Packet;

    uint64_t resolvedDropId = message.DropId;
    if (!resolvedDropId && message.ReferenceId)
    {
        if (const auto it = m_referenceDropIndex.find(message.ReferenceId); it != m_referenceDropIndex.end())
            resolvedDropId = it->second;
    }

    if (!resolvedDropId)
        return;

    NotifyDroppedItemPhysicsDisabled notify{};
    notify.DropId = resolvedDropId;
    notify.HasLocation = message.HasLocation;
    if (notify.HasLocation)
        notify.Location = message.Location;
    notify.HasRotation = message.HasRotation;
    if (notify.HasRotation)
        notify.Rotation = message.Rotation;
    notify.CellId = message.CellId;
    notify.WorldSpaceId = message.WorldSpaceId;
    notify.ReferenceId = message.ReferenceId;

    ActiveDrop* pDrop = ResolveActiveDrop(resolvedDropId);
    if (!pDrop)
    {
        spdlog::debug("DropService: physics disabled requested for unknown drop {}", resolvedDropId);
        return;
    }

    const GameId previousCell = pDrop->CellId;

    if (message.CellId)
        pDrop->CellId = message.CellId;
    if (message.WorldSpaceId)
        pDrop->WorldSpaceId = message.WorldSpaceId;
    const GameId previousRef = pDrop->ReferenceId;
    if (message.ReferenceId)
        pDrop->ReferenceId = message.ReferenceId;

    if (message.HasLocation)
    {
        pDrop->HasLocation = true;
        pDrop->Location = message.Location;
    }

    if (message.HasRotation)
    {
        pDrop->HasRotation = true;
        pDrop->Rotation = message.Rotation;
    }

    if (previousRef && previousRef != pDrop->ReferenceId)
        m_referenceDropIndex.erase(previousRef);
    if (pDrop->ReferenceId)
        m_referenceDropIndex[pDrop->ReferenceId] = pDrop->DropId;

    if (previousCell != pDrop->CellId)
    {
        EraseDropFromIndex(pDrop->DropId, previousCell);
        IndexDrop(pDrop->DropId, pDrop->CellId);
    }

    UpdateDropLocation(*pDrop);

    notify.DropId = pDrop->DropId;
    notify.HasLocation = pDrop->HasLocation;
    if (notify.HasLocation)
        notify.Location = pDrop->Location;
    notify.HasRotation = pDrop->HasRotation;
    if (notify.HasRotation)
        notify.Rotation = pDrop->Rotation;
    notify.CellId = pDrop->CellId;
    notify.WorldSpaceId = pDrop->WorldSpaceId;
    notify.ReferenceId = pDrop->ReferenceId;

    if (auto characterEntity = acMessage.pPlayer->GetCharacter(); characterEntity && m_world.valid(*characterEntity))
    {
        if (!GameServer::Get()->SendToPlayersInRange(notify, *characterEntity, acMessage.pPlayer))
        {
            spdlog::error("{}: SendToPlayersInRange failed for drop physics disabled {}", __FUNCTION__, resolvedDropId);
            GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
        }
    }
    else
    {
        GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
    }
}

void DropService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    m_cleanupAccumulator += acEvent.Delta;
    if (m_cleanupAccumulator < kCleanupIntervalSeconds)
        return;

    m_cleanupAccumulator = 0.0;
    CleanupExpiredDrops();
    CleanupExpiredCreationEnginePickups();
}

bool DropService::InitializeDatabase() noexcept
{
    m_databasePath = ResolveItemsDatabasePath();
    const auto dataDirectory = m_databasePath.parent_path();

    std::error_code ec;
    if (!dataDirectory.empty() && !std::filesystem::exists(dataDirectory, ec))
    {
        std::filesystem::create_directories(dataDirectory, ec);
        if (ec)
        {
            spdlog::error("DropService: failed to create data directory '{}': {}", dataDirectory.string(), ec.message());
            return false;
        }
    }

    spdlog::info("DropService: using drop database at '{}'", m_databasePath.string());
    if (sqlite3_open(m_databasePath.string().c_str(), &m_pDatabase) != SQLITE_OK)
    {
        spdlog::error("DropService: unable to open drop database at '{}': {}", m_databasePath.string(), sqlite3_errmsg(m_pDatabase));
        return false;
    }

    if (sqlite3_exec(m_pDatabase, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        spdlog::error("DropService: failed to enable foreign keys: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    char* pError = nullptr;
    const int execResult = sqlite3_exec(m_pDatabase, kCreateDropsTableSql, nullptr, nullptr, &pError);
    if (execResult != SQLITE_OK)
    {
        spdlog::error("DropService: failed to initialize drop schema: {}", pError ? pError : "unknown");
        sqlite3_free(pError);
        return false;
    }

    if (!EnsureColumnExists(m_pDatabase, "ALTER TABLE server_drops ADD COLUMN item_type INTEGER NOT NULL DEFAULT 0;"))
        return false;
    if (!EnsureColumnExists(m_pDatabase, "ALTER TABLE server_drops ADD COLUMN reference_mod_id INTEGER NOT NULL DEFAULT 0;"))
        return false;
    if (!EnsureColumnExists(m_pDatabase, "ALTER TABLE server_drops ADD COLUMN reference_base_id INTEGER NOT NULL DEFAULT 0;"))
        return false;

    return true;
}

void DropService::ShutdownDatabase() noexcept
{
    if (m_pDatabase)
    {
        sqlite3_close(m_pDatabase);
        m_pDatabase = nullptr;
    }
}

bool DropService::InitializeCreationEngineDatabase() noexcept
{
    m_creationEngineDatabasePath = ResolveCreationEngineDatabasePath();
    const auto dataDirectory = m_creationEngineDatabasePath.parent_path();

    std::error_code ec;
    if (!dataDirectory.empty() && !std::filesystem::exists(dataDirectory, ec))
    {
        std::filesystem::create_directories(dataDirectory, ec);
        if (ec)
        {
            spdlog::error("DropService: failed to create data directory '{}': {}", dataDirectory.string(), ec.message());
            return false;
        }
    }

    spdlog::info("DropService: using creation engine pickup database at '{}'", m_creationEngineDatabasePath.string());
    if (sqlite3_open(m_creationEngineDatabasePath.string().c_str(), &m_pCreationEngineDatabase) != SQLITE_OK)
    {
        spdlog::error("DropService: unable to open creation engine database at '{}': {}", m_creationEngineDatabasePath.string(), sqlite3_errmsg(m_pCreationEngineDatabase));
        return false;
    }

    char* pError = nullptr;
    const int execResult = sqlite3_exec(m_pCreationEngineDatabase, kCreateCreationEngineTableSql, nullptr, nullptr, &pError);
    if (execResult != SQLITE_OK)
    {
        spdlog::error("DropService: failed to initialize creation engine schema: {}", pError ? pError : "unknown");
        sqlite3_free(pError);
        return false;
    }

    return true;
}

void DropService::ShutdownCreationEngineDatabase() noexcept
{
    if (m_pCreationEngineDatabase)
    {
        sqlite3_close(m_pCreationEngineDatabase);
        m_pCreationEngineDatabase = nullptr;
    }
}

bool DropService::IsCreationEnginePickupRecorded(const GameId& acEngineRefId) noexcept
{
    if (!m_pCreationEngineDatabase || !acEngineRefId)
        return false;

    constexpr const char* cSelectSql = "SELECT 1 FROM creation_engine_pickups WHERE engine_mod_id = ?1 AND engine_base_id = ?2 LIMIT 1;";
    StatementPtr statement = PrepareStatement(m_pCreationEngineDatabase, cSelectSql);
    if (!statement)
        return false;

    sqlite3_bind_int(statement.get(), 1, static_cast<int>(acEngineRefId.ModId));
    sqlite3_bind_int(statement.get(), 2, static_cast<int>(acEngineRefId.BaseId));

    return sqlite3_step(statement.get()) == SQLITE_ROW;
}

bool DropService::RecordCreationEnginePickup(const GameId& acEngineRefId, const GameId& acCellId, const GameId& acWorldId, uint32_t aPickedBy) noexcept
{
    if (!m_pCreationEngineDatabase || !acEngineRefId || !acCellId)
        return false;

    constexpr const char* cInsertSql =
        "INSERT OR IGNORE INTO creation_engine_pickups(engine_mod_id, engine_base_id, cell_mod_id, cell_base_id, world_mod_id, world_base_id, picked_by) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7);";
    StatementPtr statement = PrepareStatement(m_pCreationEngineDatabase, cInsertSql);
    if (!statement)
        return false;

    sqlite3_bind_int(statement.get(), 1, static_cast<int>(acEngineRefId.ModId));
    sqlite3_bind_int(statement.get(), 2, static_cast<int>(acEngineRefId.BaseId));
    sqlite3_bind_int(statement.get(), 3, static_cast<int>(acCellId.ModId));
    sqlite3_bind_int(statement.get(), 4, static_cast<int>(acCellId.BaseId));
    sqlite3_bind_int(statement.get(), 5, static_cast<int>(acWorldId.ModId));
    sqlite3_bind_int(statement.get(), 6, static_cast<int>(acWorldId.BaseId));
    sqlite3_bind_int(statement.get(), 7, static_cast<int>(aPickedBy));

    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        spdlog::error("DropService: failed to record creation engine pickup: {}", sqlite3_errmsg(m_pCreationEngineDatabase));
        return false;
    }

    return sqlite3_changes(m_pCreationEngineDatabase) > 0;
}

void DropService::CleanupExpiredCreationEnginePickups() noexcept
{
    if (!m_pCreationEngineDatabase)
        return;

    constexpr const char* cDeleteSql = "DELETE FROM creation_engine_pickups WHERE picked_at < strftime('%s','now') - ?1;";
    StatementPtr statement = PrepareStatement(m_pCreationEngineDatabase, cDeleteSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare creation engine cleanup query: {}", sqlite3_errmsg(m_pCreationEngineDatabase));
        return;
    }

    sqlite3_bind_int(statement.get(), 1, static_cast<int>(kCreationEnginePickupExpirySeconds));

    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        spdlog::error("DropService: failed to cleanup creation engine pickups: {}", sqlite3_errmsg(m_pCreationEngineDatabase));
        return;
    }

    const int changes = sqlite3_changes(m_pCreationEngineDatabase);
    if (changes > 0)
        spdlog::info("DropService: cleaned up {} expired creation engine pickups", changes);
}

void DropService::LoadPersistedDrops() noexcept
{
    if (!m_pDatabase)
        return;

    m_activeDrops.clear();
    m_cellDropIndex.clear();
    m_referenceDropIndex.clear();

    constexpr const char* cSelectSql =
        "SELECT server_drop_id, server_id, actor_form_id, origin_player_id, client_drop_id, item_type, item_mod_id, item_base_id, count, cell_mod_id, cell_base_id, world_mod_id, world_base_id, reference_mod_id, "
        "reference_base_id, has_location, pos_x, pos_y, pos_z, has_rotation, rot_x, rot_y, rot_z, version, item_blob "
        "FROM server_drops WHERE is_active = 1;";

    StatementPtr statement = PrepareStatement(m_pDatabase, cSelectSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare drop select statement: {}", sqlite3_errmsg(m_pDatabase));
        return;
    }

    uint32_t restoredCount = 0;
    while (sqlite3_step(statement.get()) == SQLITE_ROW)
    {
        ActiveDrop drop{};
        drop.DropId = static_cast<uint64_t>(sqlite3_column_int64(statement.get(), 0));
        drop.ServerId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 1));
        drop.ActorFormId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 2));
        drop.OriginPlayerId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 3));
        drop.ClientDropId = GuidFromColumn(statement.get(), 4);
        const auto typeValue = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 5));
        drop.Type = typeValue == static_cast<uint32_t>(ServerItemType::CreationEngine) ? ServerItemType::CreationEngine : ServerItemType::Dropped;
        drop.CellId.ModId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 9));
        drop.CellId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 10));
        drop.WorldSpaceId.ModId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 11));
        drop.WorldSpaceId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 12));
        drop.ReferenceId.ModId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 13));
        drop.ReferenceId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 14));

        drop.HasLocation = sqlite3_column_int(statement.get(), 15) != 0;
        if (drop.HasLocation)
        {
            drop.Location.x = static_cast<float>(sqlite3_column_double(statement.get(), 16));
            drop.Location.y = static_cast<float>(sqlite3_column_double(statement.get(), 17));
            drop.Location.z = static_cast<float>(sqlite3_column_double(statement.get(), 18));
        }

        drop.HasRotation = sqlite3_column_int(statement.get(), 19) != 0;
        if (drop.HasRotation)
        {
            drop.Rotation.x = static_cast<float>(sqlite3_column_double(statement.get(), 20));
            drop.Rotation.y = static_cast<float>(sqlite3_column_double(statement.get(), 21));
            drop.Rotation.z = static_cast<float>(sqlite3_column_double(statement.get(), 22));
        }

        drop.Version = static_cast<uint64_t>(sqlite3_column_int64(statement.get(), 23));

        const void* pBlob = sqlite3_column_blob(statement.get(), 24);
        const int blobSize = sqlite3_column_bytes(statement.get(), 24);
        drop.DropEntry = DeserializeEntryBlob(pBlob, blobSize);
        drop.PickupEntry = drop.DropEntry;
        if (drop.PickupEntry.Count < 0)
            drop.PickupEntry.Count = -drop.PickupEntry.Count;

        TrackActiveDrop(drop);
        ++restoredCount;
    }

    spdlog::info("DropService: restored {} persisted drops", restoredCount);
}


NotifyDroppedItems::Entry DropService::MakeNotifyEntry(const ActiveDrop& acDrop) const noexcept
{
    NotifyDroppedItems::Entry entry{};
    entry.DropId = acDrop.DropId;
    entry.ServerId = acDrop.ServerId;
    entry.ActorFormId = acDrop.ActorFormId;
    entry.Type = acDrop.Type;
    entry.Item = acDrop.DropEntry;
    entry.HasLocation = acDrop.HasLocation;
    if (entry.HasLocation)
        entry.Location = acDrop.Location;
    entry.HasRotation = acDrop.HasRotation;
    if (entry.HasRotation)
        entry.Rotation = acDrop.Rotation;
    entry.CellId = acDrop.CellId;
    entry.WorldSpaceId = acDrop.WorldSpaceId;
    entry.ReferenceId = acDrop.ReferenceId;
    entry.SpawnEpoch = acDrop.SpawnEpoch;
    return entry;
}

bool DropService::BeginTransaction() noexcept
{
    if (!m_pDatabase)
        return false;

    if (sqlite3_exec(m_pDatabase, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        spdlog::error("DropService: failed to begin transaction: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    return true;
}

bool DropService::CommitTransaction() noexcept
{
    if (!m_pDatabase)
        return false;

    if (sqlite3_exec(m_pDatabase, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        spdlog::error("DropService: commit failed: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    return true;
}

void DropService::RollbackTransaction() noexcept
{
    if (m_pDatabase)
        sqlite3_exec(m_pDatabase, "ROLLBACK;", nullptr, nullptr, nullptr);
}

std::optional<uint64_t> DropService::FindExistingDropId(uint32_t aPlayerId, const Guid& acClientDropId) noexcept
{
    if (!m_pDatabase || acClientDropId.IsEmpty())
        return std::nullopt;

    constexpr const char* cSelectSql = "SELECT server_drop_id FROM server_drops WHERE origin_player_id = ?1 AND client_drop_id = ?2 AND is_active = 1 LIMIT 1;";
    StatementPtr statement = PrepareStatement(m_pDatabase, cSelectSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare client drop lookup: {}", sqlite3_errmsg(m_pDatabase));
        return std::nullopt;
    }

    sqlite3_bind_int(statement.get(), 1, static_cast<int>(aPlayerId));
    BindGuid(statement.get(), 2, acClientDropId);

    if (sqlite3_step(statement.get()) == SQLITE_ROW)
        return static_cast<uint64_t>(sqlite3_column_int64(statement.get(), 0));

    return std::nullopt;
}

std::optional<DropService::ActiveDrop> DropService::FetchDropFromDatabase(uint64_t aDropId) noexcept
{
    if (!m_pDatabase)
        return std::nullopt;

    constexpr const char* cSelectSql =
        "SELECT server_drop_id, server_id, actor_form_id, origin_player_id, client_drop_id, item_type, item_mod_id, item_base_id, count, cell_mod_id, cell_base_id, world_mod_id, world_base_id, reference_mod_id, "
        "reference_base_id, has_location, pos_x, pos_y, pos_z, has_rotation, rot_x, rot_y, rot_z, version, item_blob "
        "FROM server_drops WHERE server_drop_id = ?1 AND is_active = 1;";

    StatementPtr statement = PrepareStatement(m_pDatabase, cSelectSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare drop fetch: {}", sqlite3_errmsg(m_pDatabase));
        return std::nullopt;
    }

    sqlite3_bind_int64(statement.get(), 1, static_cast<sqlite3_int64>(aDropId));

    if (sqlite3_step(statement.get()) != SQLITE_ROW)
        return std::nullopt;

    ActiveDrop drop{};
    drop.DropId = static_cast<uint64_t>(sqlite3_column_int64(statement.get(), 0));
    drop.ServerId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 1));
    drop.ActorFormId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 2));
    drop.OriginPlayerId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 3));
    drop.ClientDropId = GuidFromColumn(statement.get(), 4);
    const auto typeValue = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 5));
    drop.Type = typeValue == static_cast<uint32_t>(ServerItemType::CreationEngine) ? ServerItemType::CreationEngine : ServerItemType::Dropped;
    drop.CellId.ModId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 9));
    drop.CellId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 10));
    drop.WorldSpaceId.ModId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 11));
    drop.WorldSpaceId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 12));
    drop.ReferenceId.ModId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 13));
    drop.ReferenceId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(statement.get(), 14));
    drop.HasLocation = sqlite3_column_int(statement.get(), 15) != 0;
    if (drop.HasLocation)
    {
        drop.Location.x = static_cast<float>(sqlite3_column_double(statement.get(), 16));
        drop.Location.y = static_cast<float>(sqlite3_column_double(statement.get(), 17));
        drop.Location.z = static_cast<float>(sqlite3_column_double(statement.get(), 18));
    }
    drop.HasRotation = sqlite3_column_int(statement.get(), 19) != 0;
    if (drop.HasRotation)
    {
        drop.Rotation.x = static_cast<float>(sqlite3_column_double(statement.get(), 20));
        drop.Rotation.y = static_cast<float>(sqlite3_column_double(statement.get(), 21));
        drop.Rotation.z = static_cast<float>(sqlite3_column_double(statement.get(), 22));
    }
    drop.Version = static_cast<uint64_t>(sqlite3_column_int64(statement.get(), 23));
    drop.SpawnEpoch = drop.Version;
    const void* pBlob = sqlite3_column_blob(statement.get(), 24);
    const int blobSize = sqlite3_column_bytes(statement.get(), 24);
    drop.DropEntry = DeserializeEntryBlob(pBlob, blobSize);
    drop.PickupEntry = drop.DropEntry;
    if (drop.PickupEntry.Count < 0)
        drop.PickupEntry.Count = -drop.PickupEntry.Count;

    return drop;
}

bool DropService::InsertDrop(const ActiveDrop& acDrop, uint64_t& aOutDropId) noexcept
{
    if (!m_pDatabase)
        return false;

    constexpr const char* cInsertSql =
        "INSERT INTO server_drops(server_id, actor_form_id, origin_player_id, client_drop_id, item_type, item_mod_id, item_base_id, count, cell_mod_id, cell_base_id, world_mod_id, world_base_id, reference_mod_id, "
        "reference_base_id, has_location, pos_x, pos_y, pos_z, has_rotation, rot_x, rot_y, rot_z, version, item_blob) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?24);";

    StatementPtr statement = PrepareStatement(m_pDatabase, cInsertSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare drop insert: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_int(statement.get(), 1, static_cast<int>(acDrop.ServerId));
    sqlite3_bind_int(statement.get(), 2, static_cast<int>(acDrop.ActorFormId));
    sqlite3_bind_int(statement.get(), 3, static_cast<int>(acDrop.OriginPlayerId));
    BindGuid(statement.get(), 4, acDrop.ClientDropId);
    sqlite3_bind_int(statement.get(), 5, static_cast<int>(acDrop.Type));
    sqlite3_bind_int(statement.get(), 6, static_cast<int>(acDrop.PickupEntry.BaseId.ModId));
    sqlite3_bind_int(statement.get(), 7, static_cast<int>(acDrop.PickupEntry.BaseId.BaseId));
    sqlite3_bind_int(statement.get(), 8, static_cast<int>(acDrop.PickupEntry.Count));
    sqlite3_bind_int(statement.get(), 9, static_cast<int>(acDrop.CellId.ModId));
    sqlite3_bind_int(statement.get(), 10, static_cast<int>(acDrop.CellId.BaseId));
    sqlite3_bind_int(statement.get(), 11, static_cast<int>(acDrop.WorldSpaceId.ModId));
    sqlite3_bind_int(statement.get(), 12, static_cast<int>(acDrop.WorldSpaceId.BaseId));
    sqlite3_bind_int(statement.get(), 13, static_cast<int>(acDrop.ReferenceId.ModId));
    sqlite3_bind_int(statement.get(), 14, static_cast<int>(acDrop.ReferenceId.BaseId));
    sqlite3_bind_int(statement.get(), 15, acDrop.HasLocation ? 1 : 0);
    if (acDrop.HasLocation)
    {
        sqlite3_bind_double(statement.get(), 16, acDrop.Location.x);
        sqlite3_bind_double(statement.get(), 17, acDrop.Location.y);
        sqlite3_bind_double(statement.get(), 18, acDrop.Location.z);
    }
    else
    {
        sqlite3_bind_null(statement.get(), 16);
        sqlite3_bind_null(statement.get(), 17);
        sqlite3_bind_null(statement.get(), 18);
    }
    sqlite3_bind_int(statement.get(), 19, acDrop.HasRotation ? 1 : 0);
    if (acDrop.HasRotation)
    {
        sqlite3_bind_double(statement.get(), 20, acDrop.Rotation.x);
        sqlite3_bind_double(statement.get(), 21, acDrop.Rotation.y);
        sqlite3_bind_double(statement.get(), 22, acDrop.Rotation.z);
    }
    else
    {
        sqlite3_bind_null(statement.get(), 20);
        sqlite3_bind_null(statement.get(), 21);
        sqlite3_bind_null(statement.get(), 22);
    }
    sqlite3_bind_int(statement.get(), 23, static_cast<int>(acDrop.Version));
    const std::string dropBlob = SerializeEntryBlob(acDrop.DropEntry);
    sqlite3_bind_blob(statement.get(), 24, dropBlob.data(), static_cast<int>(dropBlob.size()), SQLITE_TRANSIENT);

    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        spdlog::error("DropService: failed to insert drop: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    aOutDropId = static_cast<uint64_t>(sqlite3_last_insert_rowid(m_pDatabase));
    return true;
}

bool DropService::InsertDropHistory(uint64_t aDropId, std::string_view aAction, uint32_t aPerformedBy, const std::string& acDetails) noexcept
{
    if (!m_pDatabase)
        return false;

    constexpr const char* cInsertSql = "INSERT INTO server_drop_history(server_drop_id, action, performed_by, details) VALUES (?1, ?2, ?3, ?4);";
    StatementPtr statement = PrepareStatement(m_pDatabase, cInsertSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare drop history insert: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_int64(statement.get(), 1, static_cast<sqlite3_int64>(aDropId));
    sqlite3_bind_text(statement.get(), 2, aAction.data(), static_cast<int>(aAction.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(statement.get(), 3, static_cast<int>(aPerformedBy));
    sqlite3_bind_text(statement.get(), 4, acDetails.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        spdlog::error("DropService: failed to persist drop history: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    return true;
}

bool DropService::UpdateDropLocation(const ActiveDrop& acDrop) noexcept
{
    if (!m_pDatabase)
        return false;

    constexpr const char* cUpdateSql =
        "UPDATE server_drops SET cell_mod_id = ?2, cell_base_id = ?3, world_mod_id = ?4, world_base_id = ?5, reference_mod_id = ?6, reference_base_id = ?7, has_location = ?8, pos_x = ?9, pos_y = ?10, pos_z = ?11, "
        "has_rotation = ?12, rot_x = ?13, rot_y = ?14, rot_z = ?15, updated_at = strftime('%s','now') WHERE server_drop_id = ?1 AND is_active = 1;";

    StatementPtr statement = PrepareStatement(m_pDatabase, cUpdateSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare drop location update: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_int64(statement.get(), 1, static_cast<sqlite3_int64>(acDrop.DropId));
    sqlite3_bind_int(statement.get(), 2, static_cast<int>(acDrop.CellId.ModId));
    sqlite3_bind_int(statement.get(), 3, static_cast<int>(acDrop.CellId.BaseId));
    sqlite3_bind_int(statement.get(), 4, static_cast<int>(acDrop.WorldSpaceId.ModId));
    sqlite3_bind_int(statement.get(), 5, static_cast<int>(acDrop.WorldSpaceId.BaseId));
    sqlite3_bind_int(statement.get(), 6, static_cast<int>(acDrop.ReferenceId.ModId));
    sqlite3_bind_int(statement.get(), 7, static_cast<int>(acDrop.ReferenceId.BaseId));
    sqlite3_bind_int(statement.get(), 8, acDrop.HasLocation ? 1 : 0);
    if (acDrop.HasLocation)
    {
        sqlite3_bind_double(statement.get(), 9, acDrop.Location.x);
        sqlite3_bind_double(statement.get(), 10, acDrop.Location.y);
        sqlite3_bind_double(statement.get(), 11, acDrop.Location.z);
    }
    else
    {
        sqlite3_bind_null(statement.get(), 9);
        sqlite3_bind_null(statement.get(), 10);
        sqlite3_bind_null(statement.get(), 11);
    }
    sqlite3_bind_int(statement.get(), 12, acDrop.HasRotation ? 1 : 0);
    if (acDrop.HasRotation)
    {
        sqlite3_bind_double(statement.get(), 13, acDrop.Rotation.x);
        sqlite3_bind_double(statement.get(), 14, acDrop.Rotation.y);
        sqlite3_bind_double(statement.get(), 15, acDrop.Rotation.z);
    }
    else
    {
        sqlite3_bind_null(statement.get(), 13);
        sqlite3_bind_null(statement.get(), 14);
        sqlite3_bind_null(statement.get(), 15);
    }

    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        spdlog::error("DropService: failed to update drop location {}: {}", acDrop.DropId, sqlite3_errmsg(m_pDatabase));
        return false;
    }

    return sqlite3_changes(m_pDatabase) > 0;
}

bool DropService::MarkDropInactive(uint64_t aDropId) noexcept
{
    if (!m_pDatabase)
        return false;

    constexpr const char* cUpdateSql = "UPDATE server_drops SET is_active = 0, version = version + 1, updated_at = strftime('%s','now') WHERE server_drop_id = ?1 AND is_active = 1;";
    StatementPtr statement = PrepareStatement(m_pDatabase, cUpdateSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare drop deactivate: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_int64(statement.get(), 1, static_cast<sqlite3_int64>(aDropId));
    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        spdlog::error("DropService: failed to mark drop {} inactive: {}", aDropId, sqlite3_errmsg(m_pDatabase));
        return false;
    }

    return sqlite3_changes(m_pDatabase) > 0;
}

#if 0
bool DropService::UpdateInventoryForDelta(uint32_t aPlayerId, const Inventory::Entry& acEntry, int32_t aDelta, InventoryComponent& aInventoryComponent) noexcept
{
    if (!m_pDatabase)
        return false;

    Inventory::Entry signature = NormalizeEntrySignature(acEntry);
    std::string stackId;
    int32_t stackCount = 0;
    const bool createIfMissing = aDelta > 0;
    if (!EnsureStackForEntry(aPlayerId, acEntry, signature, aInventoryComponent, stackId, stackCount, createIfMissing))
        return false;

    constexpr const char* cUpdateSql =
        "UPDATE player_inventory SET count = count + ?1, updated_at = strftime('%s','now') WHERE player_id = ?2 AND stack_id = ?3 AND (count + ?1) >= 0;";

    StatementPtr statement = PrepareStatement(m_pDatabase, cUpdateSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare inventory update: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_int(statement.get(), 1, aDelta);
    sqlite3_bind_int(statement.get(), 2, static_cast<int>(aPlayerId));
    sqlite3_bind_text(statement.get(), 3, stackId.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        spdlog::error("DropService: inventory update failed: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    if (sqlite3_changes(m_pDatabase) == 0)
    {
        spdlog::warn("DropService: inventory update affected no rows for player {}", aPlayerId);
        return false;
    }

    return true;
}

bool DropService::EnsureStackForEntry(uint32_t aPlayerId, const Inventory::Entry& acEntry, const Inventory::Entry& acSignature, InventoryComponent& aInventoryComponent, std::string& aOutStackId,
                                      int32_t& aOutCount, bool aCreateIfMissing) noexcept
{
    if (!m_pDatabase)
        return false;

    constexpr const char* cSelectSql = "SELECT stack_id, count, entry_blob FROM player_inventory WHERE player_id = ?1 AND item_mod_id = ?2 AND item_base_id = ?3;";
    StatementPtr statement = PrepareStatement(m_pDatabase, cSelectSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare inventory lookup: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_int(statement.get(), 1, static_cast<int>(aPlayerId));
    sqlite3_bind_int(statement.get(), 2, static_cast<int>(acEntry.BaseId.ModId));
    sqlite3_bind_int(statement.get(), 3, static_cast<int>(acEntry.BaseId.BaseId));

    while (sqlite3_step(statement.get()) == SQLITE_ROW)
    {
        const char* stack = reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0));
        const int32_t count = sqlite3_column_int(statement.get(), 1);
        const void* pBlob = sqlite3_column_blob(statement.get(), 2);
        const int blobSize = sqlite3_column_bytes(statement.get(), 2);
        Inventory::Entry storedEntry = DeserializeEntryBlob(pBlob, blobSize);
        storedEntry.Count = 1;

        if (!storedEntry.CanBeMerged(acSignature))
            continue;

        aOutStackId = stack ? stack : "";
        aOutCount = count;
        return true;
    }

    if (!aCreateIfMissing)
    {
        const auto& entries = aInventoryComponent.Content.Entries;
        for (const auto& runtimeEntry : entries)
        {
            if (!runtimeEntry.CanBeMerged(acEntry))
                continue;

            int32_t runtimeCount = runtimeEntry.Count;
            if (runtimeCount < 0)
                runtimeCount = -runtimeCount;
            if (runtimeCount <= 0)
                continue;

            const Guid stackGuid = Guid::Random();
            aOutStackId = stackGuid.ToString();
            aOutCount = runtimeCount;
            const std::string blob = SerializeEntryBlob(acSignature);

            constexpr const char* cInsertSql =
                "INSERT INTO player_inventory(player_id, item_mod_id, item_base_id, stack_id, count, entry_blob) VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
            StatementPtr insertStatement = PrepareStatement(m_pDatabase, cInsertSql);
            if (!insertStatement)
            {
                spdlog::error("DropService: failed to prepare inventory snapshot insert: {}", sqlite3_errmsg(m_pDatabase));
                return false;
            }

            sqlite3_bind_int(insertStatement.get(), 1, static_cast<int>(aPlayerId));
            sqlite3_bind_int(insertStatement.get(), 2, static_cast<int>(acEntry.BaseId.ModId));
            sqlite3_bind_int(insertStatement.get(), 3, static_cast<int>(acEntry.BaseId.BaseId));
            sqlite3_bind_text(insertStatement.get(), 4, aOutStackId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(insertStatement.get(), 5, runtimeCount);
            sqlite3_bind_blob(insertStatement.get(), 6, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);

            if (sqlite3_step(insertStatement.get()) != SQLITE_DONE)
            {
                spdlog::error("DropService: failed to insert inventory snapshot: {}", sqlite3_errmsg(m_pDatabase));
                return false;
            }

            return true;
        }

        int32_t synthesizedCount = acEntry.Count;
        if (synthesizedCount < 0)
            synthesizedCount = -synthesizedCount;
        if (synthesizedCount == 0)
            synthesizedCount = 1;

        const Guid stackGuid = Guid::Random();
        aOutStackId = stackGuid.ToString();
        aOutCount = synthesizedCount;
        const std::string blob = SerializeEntryBlob(acSignature);

        constexpr const char* cInsertSql =
            "INSERT INTO player_inventory(player_id, item_mod_id, item_base_id, stack_id, count, entry_blob) VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
        StatementPtr insertStatement = PrepareStatement(m_pDatabase, cInsertSql);
        if (!insertStatement)
        {
            spdlog::error("DropService: failed to prepare synthesized stack insert: {}", sqlite3_errmsg(m_pDatabase));
            return false;
        }

        sqlite3_bind_int(insertStatement.get(), 1, static_cast<int>(aPlayerId));
        sqlite3_bind_int(insertStatement.get(), 2, static_cast<int>(acEntry.BaseId.ModId));
        sqlite3_bind_int(insertStatement.get(), 3, static_cast<int>(acEntry.BaseId.BaseId));
        sqlite3_bind_text(insertStatement.get(), 4, aOutStackId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insertStatement.get(), 5, synthesizedCount);
        sqlite3_bind_blob(insertStatement.get(), 6, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);

        if (sqlite3_step(insertStatement.get()) != SQLITE_DONE)
        {
            spdlog::error("DropService: failed to insert synthesized stack: {}", sqlite3_errmsg(m_pDatabase));
            return false;
        }

        spdlog::warn("DropService: synthesized stack for player {} item {:X}:{:X} with count {}", aPlayerId, acEntry.BaseId.ModId, acEntry.BaseId.BaseId, synthesizedCount);
        return true;
    }

    const Guid stackGuid = Guid::Random();
    aOutStackId = stackGuid.ToString();
    aOutCount = 0;
    const std::string blob = SerializeEntryBlob(acSignature);
    constexpr const char* cInsertSql =
        "INSERT INTO player_inventory(player_id, item_mod_id, item_base_id, stack_id, count, entry_blob) VALUES (?1, ?2, ?3, ?4, 0, ?5);";
    StatementPtr insertStatement = PrepareStatement(m_pDatabase, cInsertSql);
    if (!insertStatement)
    {
        spdlog::error("DropService: failed to prepare inventory stack insert: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_int(insertStatement.get(), 1, static_cast<int>(aPlayerId));
    sqlite3_bind_int(insertStatement.get(), 2, static_cast<int>(acEntry.BaseId.ModId));
    sqlite3_bind_int(insertStatement.get(), 3, static_cast<int>(acEntry.BaseId.BaseId));
    sqlite3_bind_text(insertStatement.get(), 4, aOutStackId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(insertStatement.get(), 5, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);

    if (sqlite3_step(insertStatement.get()) != SQLITE_DONE)
    {
        spdlog::error("DropService: failed to insert inventory stack: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    return true;
}
#endif

DropService::ActiveDrop* DropService::ResolveActiveDrop(uint64_t aDropId) noexcept
{
    const auto it = m_activeDrops.find(aDropId);
    if (it != m_activeDrops.end())
        return &it.value();

    if (auto dropOpt = FetchDropFromDatabase(aDropId))
    {
        TrackActiveDrop(*dropOpt);
        const auto inserted = m_activeDrops.find(aDropId);
        if (inserted != m_activeDrops.end())
            return &inserted.value();
    }

    return nullptr;
}

void DropService::TrackActiveDrop(const ActiveDrop& acDrop) noexcept
{
    if (const auto it = m_activeDrops.find(acDrop.DropId); it != m_activeDrops.end())
    {
        if (it->second.ReferenceId && it->second.ReferenceId != acDrop.ReferenceId)
            m_referenceDropIndex.erase(it->second.ReferenceId);
    }

    m_activeDrops[acDrop.DropId] = acDrop;
    IndexDrop(acDrop.DropId, acDrop.CellId);
    if (acDrop.ReferenceId)
        m_referenceDropIndex[acDrop.ReferenceId] = acDrop.DropId;
}

void DropService::RemoveActiveDrop(uint64_t aDropId) noexcept
{
    const auto it = m_activeDrops.find(aDropId);
    if (it == m_activeDrops.end())
        return;

    EraseDropFromIndex(aDropId, it->second.CellId);
    if (it->second.ReferenceId)
        m_referenceDropIndex.erase(it->second.ReferenceId);
    m_activeDrops.erase(it);
}

void DropService::IndexDrop(uint64_t aDropId, const GameId& acCellId) noexcept
{
    m_cellDropIndex[acCellId].push_back(aDropId);
}

void DropService::EraseDropFromIndex(uint64_t aDropId, const GameId& acCellId) noexcept
{
    auto indexIt = m_cellDropIndex.find(acCellId);
    if (indexIt == m_cellDropIndex.end())
        return;

    TiltedPhoques::Vector<uint64_t> filtered;
    filtered.reserve(indexIt->second.size());
    for (const auto dropId : indexIt->second)
    {
        if (dropId != aDropId)
            filtered.push_back(dropId);
    }

    m_cellDropIndex.erase(indexIt);
    if (!filtered.empty())
        m_cellDropIndex.emplace(acCellId, std::move(filtered));
}

void DropService::HandleUntrackedPickupRequest(const PacketEvent<RequestPickupDroppedItem>& acMessage) noexcept
{
    const auto& message = acMessage.Packet;

    const auto pickerEntity = m_world.TryResolveEntity(message.ServerId);
    if (!pickerEntity)
    {
        spdlog::warn("DropService: untracked pickup requested for missing entity {:X}", message.ServerId);
        return;
    }

    auto view = m_world.view<OwnerComponent>();
    const auto it = view.find(*pickerEntity);
    if (it == view.end())
    {
        spdlog::warn("DropService: untracked pickup requested for entity {:X} without OwnerComponent", message.ServerId);
        return;
    }

    auto& ownerComponent = view.get<OwnerComponent>(*it);
    if (ownerComponent.GetOwner() != acMessage.pPlayer)
    {
        spdlog::warn("DropService: untracked pickup denied for {:X}: player {:X} not owner", message.ServerId, acMessage.pPlayer->GetConnectionId());
        return;
    }

    Player* pPicker = ownerComponent.GetOwner();
    if (!pPicker)
    {
        spdlog::warn("DropService: untracked pickup denied for {:X}: missing owner player", message.ServerId);
        return;
    }

    if (!message.Item.BaseId)
    {
        spdlog::warn("DropService: untracked pickup missing item data for actor {:X}", message.ServerId);
        return;
    }

    Inventory::Entry pickupEntry = message.Item;
    if (pickupEntry.Count == 0)
        pickupEntry.Count = 1;
    else if (pickupEntry.Count < 0)
        pickupEntry.Count = -pickupEntry.Count;

    const bool hasEngineRef = message.ReferenceId != GameId{};
    const bool hasCell = message.CellId != GameId{};
    const bool shouldTrackPickup = hasEngineRef && hasCell && m_pCreationEngineDatabase;

    if (shouldTrackPickup && IsCreationEnginePickupRecorded(message.ReferenceId))
    {
        spdlog::info("DropService: rejecting duplicate creation engine pickup ref {:X}:{:X} in cell {:X}:{:X} for player {}", message.ReferenceId.ModId, message.ReferenceId.BaseId, message.CellId.ModId, message.CellId.BaseId, pPicker->GetId());

        NotifyInventoryChanges correction{};
        correction.ServerId = message.ServerId;
        correction.Item = pickupEntry;
        correction.Item.Count = -pickupEntry.Count;
        correction.Silent = true;
        acMessage.pPlayer->Send(correction);

        NotifyDroppedItemPickedUp notify{};
        notify.ServerId = message.ServerId;
        notify.Item = pickupEntry;
        notify.DropId = 0;
        notify.HasLocation = message.HasLocation;
        if (notify.HasLocation)
            notify.Location = message.Location;
        notify.HasRotation = message.HasRotation;
        if (notify.HasRotation)
            notify.Rotation = message.Rotation;
        notify.CellId = message.CellId;
        notify.WorldSpaceId = message.WorldSpaceId;
        notify.ReferenceId = message.ReferenceId;

        BroadcastPickup(notify);
        return;
    }

    if (shouldTrackPickup)
    {
        if (!RecordCreationEnginePickup(message.ReferenceId, message.CellId, message.WorldSpaceId, pPicker->GetId()))
        {
            if (IsCreationEnginePickupRecorded(message.ReferenceId))
            {
                spdlog::info("DropService: rejecting creation engine pickup ref {:X}:{:X} (race/duplicate) for player {}", message.ReferenceId.ModId, message.ReferenceId.BaseId, pPicker->GetId());

                NotifyInventoryChanges correction{};
                correction.ServerId = message.ServerId;
                correction.Item = pickupEntry;
                correction.Item.Count = -pickupEntry.Count;
                correction.Silent = true;
                acMessage.pPlayer->Send(correction);

                NotifyDroppedItemPickedUp notify{};
                notify.ServerId = message.ServerId;
                notify.Item = pickupEntry;
                notify.DropId = 0;
                notify.HasLocation = message.HasLocation;
                if (notify.HasLocation)
                    notify.Location = message.Location;
                notify.HasRotation = message.HasRotation;
                if (notify.HasRotation)
                    notify.Rotation = message.Rotation;
                notify.CellId = message.CellId;
                notify.WorldSpaceId = message.WorldSpaceId;
                notify.ReferenceId = message.ReferenceId;

                BroadcastPickup(notify);
                return;
            }

            spdlog::warn("DropService: failed to record creation engine pickup ref {:X}:{:X} for player {}, continuing without tracking", message.ReferenceId.ModId, message.ReferenceId.BaseId, pPicker->GetId());
        }
    }

    NotifyDroppedItemPickedUp notify{};
    notify.ServerId = message.ServerId;
    notify.Item = pickupEntry;
    notify.DropId = 0;
    notify.HasLocation = message.HasLocation;
    if (notify.HasLocation)
        notify.Location = message.Location;
    notify.HasRotation = message.HasRotation;
    if (notify.HasRotation)
        notify.Rotation = message.Rotation;
    notify.CellId = message.CellId;
    notify.WorldSpaceId = message.WorldSpaceId;
    notify.ReferenceId = message.ReferenceId;

    BroadcastPickup(notify);
    spdlog::debug("DropService: processed untracked pickup for actor {:X}", message.ServerId);
}

void DropService::BroadcastPickup(const NotifyDroppedItemPickedUp& acMessage) const noexcept
{
    GameServer::Get()->SendToPlayers(acMessage, nullptr);
}

void DropService::CleanupExpiredDrops() noexcept
{
    if (!m_pDatabase)
        return;

    constexpr const char* cSelectSql = "SELECT server_drop_id FROM server_drops WHERE is_active = 1 AND created_at < strftime('%s','now') - ?1;";
    StatementPtr statement = PrepareStatement(m_pDatabase, cSelectSql);
    if (!statement)
    {
        spdlog::error("DropService: failed to prepare cleanup query: {}", sqlite3_errmsg(m_pDatabase));
        return;
    }

    sqlite3_bind_int(statement.get(), 1, static_cast<int>(kDropExpirySeconds));

    TiltedPhoques::Vector<uint64_t> expiredDrops;
    while (sqlite3_step(statement.get()) == SQLITE_ROW)
        expiredDrops.push_back(static_cast<uint64_t>(sqlite3_column_int64(statement.get(), 0)));

    if (expiredDrops.empty())
        return;

    for (auto dropId : expiredDrops)
    {
        MarkDropInactive(dropId);
        RemoveActiveDrop(dropId);
    }

    spdlog::info("DropService: cleaned up {} expired drops", expiredDrops.size());
}
