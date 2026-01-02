#include <Services/AdminService.h>

#include <World.h>

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
constexpr char kCreateAdminsTableSql[] =
    R"SQL(
    CREATE TABLE IF NOT EXISTS admins(
        username TEXT PRIMARY KEY
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

TiltedPhoques::String Normalize(const TiltedPhoques::String& value)
{
    TiltedPhoques::String normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return normalized;
}
} // namespace

AdminService::AdminService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    (void)aDispatcher;
    if (!InitializeDatabase())
        spdlog::error("AdminService: failed to initialize admin database");
}

AdminService::~AdminService() noexcept
{
    ShutdownDatabase();
}

TiltedPhoques::String AdminService::NormalizeUsername(const TiltedPhoques::String& aUsername)
{
    return Normalize(aUsername);
}

std::filesystem::path AdminService::ResolveAdminDatabasePath() const noexcept
{
    const auto itemsPath = ResolveItemsDatabasePath();
    if (!itemsPath.empty())
        return itemsPath.parent_path() / "admins.db";

    return std::filesystem::path("admins.db");
}

bool AdminService::InitializeDatabase() noexcept
{
    m_databasePath = ResolveAdminDatabasePath();
    const auto dataDirectory = m_databasePath.parent_path();

    std::error_code ec;
    if (!dataDirectory.empty() && !std::filesystem::exists(dataDirectory, ec))
    {
        std::filesystem::create_directories(dataDirectory, ec);
        if (ec)
        {
            spdlog::error("AdminService: failed to create data directory '{}': {}", dataDirectory.string(), ec.message());
            return false;
        }
    }

    spdlog::info("AdminService: using admin database at '{}'", m_databasePath.string());
    if (sqlite3_open(m_databasePath.string().c_str(), &m_pDatabase) != SQLITE_OK)
    {
        spdlog::error("AdminService: unable to open admin database at '{}': {}", m_databasePath.string(), sqlite3_errmsg(m_pDatabase));
        return false;
    }

    char* pError = nullptr;
    const int execResult = sqlite3_exec(m_pDatabase, kCreateAdminsTableSql, nullptr, nullptr, &pError);
    if (execResult != SQLITE_OK)
    {
        spdlog::error("AdminService: failed to initialize admin schema: {}", pError ? pError : "unknown");
        sqlite3_free(pError);
        return false;
    }

    return true;
}

void AdminService::ShutdownDatabase() noexcept
{
    if (m_pDatabase)
    {
        sqlite3_close(m_pDatabase);
        m_pDatabase = nullptr;
    }
}

bool AdminService::IsAdmin(const TiltedPhoques::String& aUsername) const noexcept
{
    if (!m_pDatabase)
        return false;

    const TiltedPhoques::String normalized = NormalizeUsername(aUsername);
    if (normalized.empty())
        return false;

    constexpr const char* cSelectSql = "SELECT 1 FROM admins WHERE username = ?1 LIMIT 1;";
    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(m_pDatabase, cSelectSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("AdminService: failed to prepare admin lookup: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_text(pStatement, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);
    const int stepResult = sqlite3_step(pStatement);
    sqlite3_finalize(pStatement);
    return stepResult == SQLITE_ROW;
}

bool AdminService::AddAdmin(const TiltedPhoques::String& aUsername) noexcept
{
    if (!m_pDatabase)
        return false;

    const TiltedPhoques::String normalized = NormalizeUsername(aUsername);
    if (normalized.empty())
        return false;

    constexpr const char* cInsertSql = "INSERT OR IGNORE INTO admins(username) VALUES(?1);";
    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(m_pDatabase, cInsertSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("AdminService: failed to prepare admin insert: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_text(pStatement, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);
    const int stepResult = sqlite3_step(pStatement);
    sqlite3_finalize(pStatement);
    if (stepResult != SQLITE_DONE)
    {
        spdlog::error("AdminService: failed to insert admin '{}': {}", normalized.c_str(), sqlite3_errmsg(m_pDatabase));
        return false;
    }

    return true;
}

bool AdminService::RemoveAdmin(const TiltedPhoques::String& aUsername) noexcept
{
    if (!m_pDatabase)
        return false;

    const TiltedPhoques::String normalized = NormalizeUsername(aUsername);
    if (normalized.empty())
        return false;

    constexpr const char* cDeleteSql = "DELETE FROM admins WHERE username = ?1;";
    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(m_pDatabase, cDeleteSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("AdminService: failed to prepare admin delete: {}", sqlite3_errmsg(m_pDatabase));
        return false;
    }

    sqlite3_bind_text(pStatement, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);
    const int stepResult = sqlite3_step(pStatement);
    sqlite3_finalize(pStatement);
    if (stepResult != SQLITE_DONE)
    {
        spdlog::error("AdminService: failed to delete admin '{}': {}", normalized.c_str(), sqlite3_errmsg(m_pDatabase));
        return false;
    }

    return true;
}

void AdminService::GetAdmins(TiltedPhoques::Vector<TiltedPhoques::String>& aOutAdmins) const noexcept
{
    aOutAdmins.clear();
    if (!m_pDatabase)
        return;

    constexpr const char* cSelectSql = "SELECT username FROM admins ORDER BY username;";
    sqlite3_stmt* pStatement = nullptr;
    if (sqlite3_prepare_v2(m_pDatabase, cSelectSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("AdminService: failed to prepare admin list: {}", sqlite3_errmsg(m_pDatabase));
        return;
    }

    while (sqlite3_step(pStatement) == SQLITE_ROW)
    {
        if (const auto* pText = reinterpret_cast<const char*>(sqlite3_column_text(pStatement, 0)))
            aOutAdmins.emplace_back(pText);
    }

    sqlite3_finalize(pStatement);
}
