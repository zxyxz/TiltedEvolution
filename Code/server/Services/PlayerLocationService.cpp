#include <Services/PlayerLocationService.h>

#include <World.h>
#include <Game/Player.h>
#include <Events/PlayerJoinEvent.h>
#include <Events/PlayerLeaveEvent.h>
#include <Events/UpdateEvent.h>

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
constexpr char kCreateLocationsTableSql[] =
    R"SQL(
    CREATE TABLE IF NOT EXISTS player_locations(
        username TEXT PRIMARY KEY,
        player_id INTEGER NOT NULL DEFAULT 0,
        endpoint TEXT,
        pos_x REAL NOT NULL DEFAULT 0,
        pos_y REAL NOT NULL DEFAULT 0,
        pos_z REAL NOT NULL DEFAULT 0,
        cell_mod_id INTEGER NOT NULL DEFAULT 0,
        cell_base_id INTEGER NOT NULL DEFAULT 0,
        world_mod_id INTEGER NOT NULL DEFAULT 0,
        world_base_id INTEGER NOT NULL DEFAULT 0,
        last_seen INTEGER NOT NULL DEFAULT 0,
        source INTEGER NOT NULL DEFAULT 0,
        has_position INTEGER NOT NULL DEFAULT 0,
        ext_pos_x REAL NOT NULL DEFAULT 0,
        ext_pos_y REAL NOT NULL DEFAULT 0,
        ext_pos_z REAL NOT NULL DEFAULT 0,
        ext_cell_mod_id INTEGER NOT NULL DEFAULT 0,
        ext_cell_base_id INTEGER NOT NULL DEFAULT 0,
        ext_world_mod_id INTEGER NOT NULL DEFAULT 0,
        ext_world_base_id INTEGER NOT NULL DEFAULT 0,
        ext_last_seen INTEGER NOT NULL DEFAULT 0,
        has_exterior INTEGER NOT NULL DEFAULT 0
    );
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

std::filesystem::path ResolveLocationsDatabasePath() noexcept
{
    namespace fs = std::filesystem;

    if (auto exeDirectory = ResolveExecutableDirectory(); !exeDirectory.empty())
    {
        return exeDirectory / "locations.db";
    }

#if defined(_WIN32)
    if (char* localAppData = nullptr; _dupenv_s(&localAppData, nullptr, "LOCALAPPDATA") == 0 && localAppData)
    {
        fs::path path(localAppData);
        free(localAppData);
        path /= "SkyrimTogether";
        path /= "Server";
        path /= "locations.db";
        return path;
    }
#else
    if (const char* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && xdgDataHome[0] != '\0')
    {
        return fs::path(xdgDataHome) / "skyrimtogether" / "server" / "locations.db";
    }

    if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
    {
        return fs::path(home) / ".local" / "share" / "skyrimtogether" / "server" / "locations.db";
    }
#endif

    std::error_code ec;
    const auto current = fs::current_path(ec);
    if (!ec)
        return current / "data" / "locations.db";

    return fs::path("locations.db");
}

uint64_t GetEpochSeconds() noexcept
{
    using clock = std::chrono::system_clock;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(clock::now().time_since_epoch()).count());
}

bool EnsureColumnExists(sqlite3* apDatabase, const char* acSql) noexcept
{
    if (!apDatabase || !acSql)
        return false;

    char* pError = nullptr;
    const int result = sqlite3_exec(apDatabase, acSql, nullptr, nullptr, &pError);
    if (result == SQLITE_OK)
        return true;

    std::string_view message = pError ? pError : "";
    if (!message.empty())
    {
        const bool duplicate = message.find("duplicate column") != std::string_view::npos ||
                               message.find("already exists") != std::string_view::npos;
        sqlite3_free(pError);
        return duplicate;
    }

    sqlite3_free(pError);
    return false;
}
} // namespace

PlayerLocationService::PlayerLocationService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_playerJoinConnection = aDispatcher.sink<PlayerJoinEvent>().connect<&PlayerLocationService::OnPlayerJoin>(this);
    m_playerLeaveConnection = aDispatcher.sink<PlayerLeaveEvent>().connect<&PlayerLocationService::OnPlayerLeave>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&PlayerLocationService::OnUpdate>(this);

    if (!InitializeDatabase())
        spdlog::error("PlayerLocationService: failed to initialize locations database");
}

PlayerLocationService::~PlayerLocationService() noexcept
{
    ShutdownDatabase();
}

void PlayerLocationService::UpdateLocation(Player* apPlayer, const Vector3_NetQuantize& acPos, const GameId& acWorldSpaceId, const GameId& acCellId,
                                           PlayerLocation::Source aSource, bool aForcePersist) noexcept
{
    if (!apPlayer)
        return;

    const uint32_t playerId = apPlayer->GetId();
    auto& location = m_locations[playerId];
    location.Position = acPos;
    location.WorldSpaceId = acWorldSpaceId;
    location.CellId = acCellId;
    location.LastSeenEpoch = GetEpochSeconds();
    location.SourceTag = aSource;
    location.HasPosition = true;
    location.Username = apPlayer->GetUsername();
    location.Endpoint = apPlayer->GetEndPoint();
    if (acWorldSpaceId)
    {
        location.LastExteriorPosition = acPos;
        location.LastExteriorWorldSpaceId = acWorldSpaceId;
        location.LastExteriorCellId = acCellId;
        location.LastExteriorEpoch = location.LastSeenEpoch;
        location.HasExterior = true;
    }

    PersistLocation(location, playerId, aForcePersist ? "force" : "update");
}

void PlayerLocationService::UpdateCell(Player* apPlayer, const GameId& acWorldSpaceId, const GameId& acCellId, PlayerLocation::Source aSource,
                                       bool aForcePersist) noexcept
{
    if (!apPlayer)
        return;

    const uint32_t playerId = apPlayer->GetId();
    auto it = m_locations.find(playerId);
    if (it != m_locations.end())
    {
        auto& location = it->second;
        location.WorldSpaceId = acWorldSpaceId;
        location.CellId = acCellId;
        location.LastSeenEpoch = GetEpochSeconds();
        location.SourceTag = aSource;
        location.Endpoint = apPlayer->GetEndPoint();
        if (location.HasPosition)
            PersistLocation(location, playerId, aForcePersist ? "force-cell" : "cell");
        return;
    }

    PlayerLocation location{};
    location.Username = apPlayer->GetUsername();
    location.Endpoint = apPlayer->GetEndPoint();
    location.WorldSpaceId = acWorldSpaceId;
    location.CellId = acCellId;
    location.LastSeenEpoch = GetEpochSeconds();
    location.SourceTag = aSource;
    location.HasPosition = false;
    location.HasExterior = false;
    m_locations.emplace(playerId, std::move(location));
}

bool PlayerLocationService::TryGetLocation(uint32_t aPlayerId, PlayerLocation& aOut) const noexcept
{
    const auto it = m_locations.find(aPlayerId);
    if (it == m_locations.end())
        return false;

    aOut = it->second;
    return true;
}

void PlayerLocationService::OnPlayerJoin(const PlayerJoinEvent& aEvent) noexcept
{
    LoadFromDatabase(aEvent.pPlayer);
}

void PlayerLocationService::OnPlayerLeave(const PlayerLeaveEvent& aEvent) noexcept
{
    if (!aEvent.pPlayer)
        return;

    const uint32_t playerId = aEvent.pPlayer->GetId();
    auto it = m_locations.find(playerId);
    if (it != m_locations.end())
    {
        PersistLocation(it->second, playerId, "leave");
        m_locations.erase(it);
    }
}

void PlayerLocationService::OnUpdate(const UpdateEvent&) noexcept
{
    // No periodic tasks yet; reserved for future throttling if needed.
}

bool PlayerLocationService::InitializeDatabase() noexcept
{
    m_databasePath = ResolveLocationsDatabasePath();
    const auto dataDirectory = m_databasePath.parent_path();

    std::error_code ec;
    if (!dataDirectory.empty() && !std::filesystem::exists(dataDirectory, ec))
    {
        std::filesystem::create_directories(dataDirectory, ec);
        if (ec)
        {
            spdlog::error("PlayerLocationService: failed to create data directory '{}': {}", dataDirectory.string(), ec.message());
            return false;
        }
    }

    spdlog::info("PlayerLocationService: using locations database at '{}'", m_databasePath.string());
    if (sqlite3_open(m_databasePath.string().c_str(), &m_pDatabase) != SQLITE_OK)
    {
        spdlog::error("PlayerLocationService: unable to open database at '{}': {}", m_databasePath.string(), sqlite3_errmsg(m_pDatabase));
        return false;
    }

    char* pError = nullptr;
    const int execResult = sqlite3_exec(m_pDatabase, kCreateLocationsTableSql, nullptr, nullptr, &pError);
    if (execResult != SQLITE_OK)
    {
        spdlog::error("PlayerLocationService: failed to initialize schema: {}", pError ? pError : "unknown");
        sqlite3_free(pError);
        return false;
    }

    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN has_position INTEGER NOT NULL DEFAULT 0;");
    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN ext_pos_x REAL NOT NULL DEFAULT 0;");
    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN ext_pos_y REAL NOT NULL DEFAULT 0;");
    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN ext_pos_z REAL NOT NULL DEFAULT 0;");
    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN ext_cell_mod_id INTEGER NOT NULL DEFAULT 0;");
    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN ext_cell_base_id INTEGER NOT NULL DEFAULT 0;");
    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN ext_world_mod_id INTEGER NOT NULL DEFAULT 0;");
    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN ext_world_base_id INTEGER NOT NULL DEFAULT 0;");
    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN ext_last_seen INTEGER NOT NULL DEFAULT 0;");
    EnsureColumnExists(m_pDatabase, "ALTER TABLE player_locations ADD COLUMN has_exterior INTEGER NOT NULL DEFAULT 0;");

    return true;
}

void PlayerLocationService::ShutdownDatabase() noexcept
{
    if (m_pDatabase)
    {
        sqlite3_close(m_pDatabase);
        m_pDatabase = nullptr;
    }
}

void PlayerLocationService::LoadFromDatabase(Player* apPlayer) noexcept
{
    if (!apPlayer || !m_pDatabase)
        return;

    const auto& username = apPlayer->GetUsername();
    if (username.empty())
        return;

    constexpr const char* cSelectSql =
        "SELECT player_id, endpoint, pos_x, pos_y, pos_z, cell_mod_id, cell_base_id, world_mod_id, world_base_id, last_seen, source, has_position, "
        "ext_pos_x, ext_pos_y, ext_pos_z, ext_cell_mod_id, ext_cell_base_id, ext_world_mod_id, ext_world_base_id, ext_last_seen, has_exterior "
        "FROM player_locations WHERE username = ?1 LIMIT 1;";

    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(m_pDatabase, cSelectSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("PlayerLocationService: failed to prepare location lookup: {}", sqlite3_errmsg(m_pDatabase));
        return;
    }

    sqlite3_bind_text(pStatement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(pStatement) == SQLITE_ROW)
    {
        PlayerLocation location{};
        location.Username = username;
        location.Endpoint = apPlayer->GetEndPoint();
        location.Position.x = static_cast<float>(sqlite3_column_double(pStatement, 2));
        location.Position.y = static_cast<float>(sqlite3_column_double(pStatement, 3));
        location.Position.z = static_cast<float>(sqlite3_column_double(pStatement, 4));
        location.CellId.ModId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 5));
        location.CellId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 6));
        location.WorldSpaceId.ModId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 7));
        location.WorldSpaceId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 8));
        location.LastSeenEpoch = static_cast<uint64_t>(sqlite3_column_int64(pStatement, 9));
        location.SourceTag = static_cast<PlayerLocation::Source>(sqlite3_column_int64(pStatement, 10));
        const bool storedHasPosition = sqlite3_column_int64(pStatement, 11) != 0;
        const bool hasWorld = location.WorldSpaceId || location.CellId;
        const bool hasPosData = (location.Position.x != 0.0f || location.Position.y != 0.0f || location.Position.z != 0.0f);
        location.HasPosition = storedHasPosition || hasWorld || hasPosData;
        location.LastExteriorPosition.x = static_cast<float>(sqlite3_column_double(pStatement, 12));
        location.LastExteriorPosition.y = static_cast<float>(sqlite3_column_double(pStatement, 13));
        location.LastExteriorPosition.z = static_cast<float>(sqlite3_column_double(pStatement, 14));
        location.LastExteriorCellId.ModId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 15));
        location.LastExteriorCellId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 16));
        location.LastExteriorWorldSpaceId.ModId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 17));
        location.LastExteriorWorldSpaceId.BaseId = static_cast<uint32_t>(sqlite3_column_int64(pStatement, 18));
        location.LastExteriorEpoch = static_cast<uint64_t>(sqlite3_column_int64(pStatement, 19));
        const bool storedHasExterior = sqlite3_column_int64(pStatement, 20) != 0;
        const bool hasExtWorld = location.LastExteriorWorldSpaceId || location.LastExteriorCellId;
        const bool hasExtPos = (location.LastExteriorPosition.x != 0.0f || location.LastExteriorPosition.y != 0.0f || location.LastExteriorPosition.z != 0.0f);
        location.HasExterior = storedHasExterior || hasExtWorld || hasExtPos;
        if (!location.HasExterior && location.HasPosition && location.WorldSpaceId)
        {
            location.HasExterior = true;
            location.LastExteriorPosition = location.Position;
            location.LastExteriorWorldSpaceId = location.WorldSpaceId;
            location.LastExteriorCellId = location.CellId;
            location.LastExteriorEpoch = location.LastSeenEpoch;
        }

        m_locations[apPlayer->GetId()] = std::move(location);
    }

    sqlite3_finalize(pStatement);
}

void PlayerLocationService::PersistLocation(PlayerLocation& aLocation, uint32_t aPlayerId, const char* apReason) noexcept
{
    if (!m_pDatabase || aLocation.Username.empty())
        return;

    constexpr auto cPersistInterval = std::chrono::seconds(5);
    const auto now = std::chrono::steady_clock::now();
    if (apReason && std::string_view(apReason) != "force" && std::string_view(apReason) != "force-cell")
    {
        if (aLocation.LastPersist.time_since_epoch().count() != 0 && (now - aLocation.LastPersist) < cPersistInterval)
            return;
    }

    constexpr const char* cUpsertSql =
        "INSERT INTO player_locations(username, player_id, endpoint, pos_x, pos_y, pos_z, cell_mod_id, cell_base_id, world_mod_id, world_base_id, last_seen, source, has_position, "
        "ext_pos_x, ext_pos_y, ext_pos_z, ext_cell_mod_id, ext_cell_base_id, ext_world_mod_id, ext_world_base_id, ext_last_seen, has_exterior) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22) "
        "ON CONFLICT(username) DO UPDATE SET "
        "player_id = excluded.player_id, endpoint = excluded.endpoint, pos_x = excluded.pos_x, pos_y = excluded.pos_y, pos_z = excluded.pos_z, "
        "cell_mod_id = excluded.cell_mod_id, cell_base_id = excluded.cell_base_id, world_mod_id = excluded.world_mod_id, world_base_id = excluded.world_base_id, "
        "last_seen = excluded.last_seen, source = excluded.source, has_position = excluded.has_position, "
        "ext_pos_x = excluded.ext_pos_x, ext_pos_y = excluded.ext_pos_y, ext_pos_z = excluded.ext_pos_z, "
        "ext_cell_mod_id = excluded.ext_cell_mod_id, ext_cell_base_id = excluded.ext_cell_base_id, "
        "ext_world_mod_id = excluded.ext_world_mod_id, ext_world_base_id = excluded.ext_world_base_id, "
        "ext_last_seen = excluded.ext_last_seen, has_exterior = excluded.has_exterior;";

    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(m_pDatabase, cUpsertSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("PlayerLocationService: failed to prepare location upsert: {}", sqlite3_errmsg(m_pDatabase));
        return;
    }

    sqlite3_bind_text(pStatement, 1, aLocation.Username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(pStatement, 2, static_cast<sqlite3_int64>(aPlayerId));
    if (!aLocation.Endpoint.empty())
        sqlite3_bind_text(pStatement, 3, aLocation.Endpoint.c_str(), -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(pStatement, 3);
    sqlite3_bind_double(pStatement, 4, aLocation.Position.x);
    sqlite3_bind_double(pStatement, 5, aLocation.Position.y);
    sqlite3_bind_double(pStatement, 6, aLocation.Position.z);
    sqlite3_bind_int(pStatement, 7, static_cast<int>(aLocation.CellId.ModId));
    sqlite3_bind_int(pStatement, 8, static_cast<int>(aLocation.CellId.BaseId));
    sqlite3_bind_int(pStatement, 9, static_cast<int>(aLocation.WorldSpaceId.ModId));
    sqlite3_bind_int(pStatement, 10, static_cast<int>(aLocation.WorldSpaceId.BaseId));
    sqlite3_bind_int64(pStatement, 11, static_cast<sqlite3_int64>(aLocation.LastSeenEpoch));
    sqlite3_bind_int(pStatement, 12, static_cast<int>(aLocation.SourceTag));
    sqlite3_bind_int(pStatement, 13, aLocation.HasPosition ? 1 : 0);
    sqlite3_bind_double(pStatement, 14, aLocation.LastExteriorPosition.x);
    sqlite3_bind_double(pStatement, 15, aLocation.LastExteriorPosition.y);
    sqlite3_bind_double(pStatement, 16, aLocation.LastExteriorPosition.z);
    sqlite3_bind_int(pStatement, 17, static_cast<int>(aLocation.LastExteriorCellId.ModId));
    sqlite3_bind_int(pStatement, 18, static_cast<int>(aLocation.LastExteriorCellId.BaseId));
    sqlite3_bind_int(pStatement, 19, static_cast<int>(aLocation.LastExteriorWorldSpaceId.ModId));
    sqlite3_bind_int(pStatement, 20, static_cast<int>(aLocation.LastExteriorWorldSpaceId.BaseId));
    sqlite3_bind_int64(pStatement, 21, static_cast<sqlite3_int64>(aLocation.LastExteriorEpoch));
    sqlite3_bind_int(pStatement, 22, aLocation.HasExterior ? 1 : 0);

    if (sqlite3_step(pStatement) != SQLITE_DONE)
    {
        spdlog::error("PlayerLocationService: failed to persist location for {}: {}", aLocation.Username.c_str(), sqlite3_errmsg(m_pDatabase));
    }

    sqlite3_finalize(pStatement);
    aLocation.LastPersist = now;
}
