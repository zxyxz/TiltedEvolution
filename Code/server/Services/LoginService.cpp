#include <Services/LoginService.h>

#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <CredentialHash.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string_view>

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
constexpr const char* kCreateUsersTableSql = R"SQL(
    CREATE TABLE IF NOT EXISTS users(
        username TEXT PRIMARY KEY,
        password_hash TEXT NOT NULL,
        password_salt TEXT NOT NULL,
        avatar TEXT DEFAULT '',
        created_at DATETIME DEFAULT CURRENT_TIMESTAMP
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

std::filesystem::path ResolveDatabasePath() noexcept
{
    namespace fs = std::filesystem;

    if (auto exeDirectory = ResolveExecutableDirectory(); !exeDirectory.empty())
    {
        return exeDirectory / "logins.db";
    }

#if defined(_WIN32)
    if (const char* localAppData = std::getenv("LOCALAPPDATA"); localAppData && localAppData[0] != '\0')
    {
        return fs::path(localAppData) / "SkyrimTogether" / "Server" / "logins.db";
    }
#else
    if (const char* xdgDataHome = std::getenv("XDG_DATA_HOME"); xdgDataHome && xdgDataHome[0] != '\0')
    {
        return fs::path(xdgDataHome) / "skyrimtogether" / "server" / "logins.db";
    }

    if (const char* home = std::getenv("HOME"); home && home[0] != '\0')
    {
        return fs::path(home) / ".local" / "share" / "skyrimtogether" / "server" / "logins.db";
    }
#endif

    std::error_code ec;
    const auto current = fs::current_path(ec);
    if (!ec)
        return current / "data" / "logins.db";

    return fs::path("logins.db");
}
} // namespace

LoginService::LoginService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
{
    (void)aWorld;
    (void)aDispatcher;

    const auto databasePath = ResolveDatabasePath();
    const auto dataDirectory = databasePath.parent_path();
    std::error_code ec;
    if (!dataDirectory.empty() && !std::filesystem::exists(dataDirectory, ec))
    {
        std::filesystem::create_directories(dataDirectory, ec);
        if (ec)
        {
            spdlog::error("LoginService: failed to create data directory '{}': {}", dataDirectory.string(), ec.message());
            return;
        }
    }
    else if (ec)
    {
        spdlog::error("LoginService: failed to access data directory '{}': {}", dataDirectory.string(), ec.message());
        return;
    }

    spdlog::info("LoginService: using login database at '{}'", databasePath.string());
    if (sqlite3_open(databasePath.string().c_str(), &m_pDatabase) != SQLITE_OK)
    {
        spdlog::error("LoginService: unable to open database at '{}': {}", databasePath.string(), sqlite3_errmsg(m_pDatabase));
        return;
    }

    if (!InitializeSchema())
        spdlog::error("LoginService: failed to initialize sqlite schema");
}

LoginService::~LoginService() noexcept
{
    if (m_pDatabase)
    {
        sqlite3_close(m_pDatabase);
        m_pDatabase = nullptr;
    }
}

LoginService::LoginResult LoginService::VerifyOrCreateUser(const TiltedPhoques::String& aUsername, const TiltedPhoques::String& aPassword) noexcept
{
    if (!m_pDatabase)
        return LoginResult::InternalError;

    if (aUsername.empty() || aPassword.empty())
        return LoginResult::InvalidCredentials;

    if (!Credential::LooksLikePasswordHash(aPassword.c_str()))
    {
        spdlog::error("LoginService: rejecting authentication for '{}' due to unhashed password payload", aUsername.c_str());
        return LoginResult::InvalidCredentials;
    }

    sqlite3_stmt* pStatement = nullptr;
    constexpr const char* cLookupSql = "SELECT password_hash, password_salt FROM users WHERE username = ?1;";
    if (sqlite3_prepare_v2(m_pDatabase, cLookupSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("LoginService: failed to prepare lookup statement: {}", sqlite3_errmsg(m_pDatabase));
        return LoginResult::InternalError;
    }

    sqlite3_bind_text(pStatement, 1, aUsername.c_str(), -1, SQLITE_TRANSIENT);

    const int stepResult = sqlite3_step(pStatement);
    if (stepResult == SQLITE_ROW)
    {
        const auto* storedHash = reinterpret_cast<const char*>(sqlite3_column_text(pStatement, 0));
        const auto* storedSalt = reinterpret_cast<const char*>(sqlite3_column_text(pStatement, 1));
        bool match = false;
        if (storedHash && storedSalt)
        {
            auto derived = Credential::DeriveServerPassword(aPassword.c_str(), storedSalt);
            match = derived == storedHash;
        }
        sqlite3_finalize(pStatement);
        return match ? LoginResult::Ok : LoginResult::InvalidCredentials;
    }

    sqlite3_finalize(pStatement);

    if (stepResult != SQLITE_DONE)
    {
        spdlog::error("LoginService: unexpected sqlite step result {}", stepResult);
        return LoginResult::InternalError;
    }

    return InsertUser(aUsername, aPassword);
}

TiltedPhoques::String LoginService::GetAvatar(const TiltedPhoques::String& aUsername) const noexcept
{
    TiltedPhoques::String avatar;
    if (!m_pDatabase || aUsername.empty())
        return avatar;

    sqlite3_stmt* pStatement = nullptr;
    constexpr const char* cSelectSql = "SELECT avatar FROM users WHERE username = ?1;";
    if (sqlite3_prepare_v2(m_pDatabase, cSelectSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("LoginService: failed to prepare avatar lookup for '{}': {}", aUsername.c_str(), sqlite3_errmsg(m_pDatabase));
        return avatar;
    }

    sqlite3_bind_text(pStatement, 1, aUsername.c_str(), -1, SQLITE_TRANSIENT);

    const int stepResult = sqlite3_step(pStatement);
    if (stepResult == SQLITE_ROW)
    {
        if (const auto* pText = reinterpret_cast<const char*>(sqlite3_column_text(pStatement, 0)))
            avatar = pText;
    }
    else if (stepResult != SQLITE_DONE)
    {
        spdlog::error("LoginService: avatar lookup for '{}' failed with sqlite code {}", aUsername.c_str(), stepResult);
    }

    sqlite3_finalize(pStatement);
    return avatar;
}

void LoginService::SetAvatar(const TiltedPhoques::String& aUsername, const TiltedPhoques::String& aAvatar) noexcept
{
    if (!m_pDatabase || aUsername.empty())
        return;

    sqlite3_stmt* pStatement = nullptr;
    constexpr const char* cUpdateSql = "UPDATE users SET avatar = ?2 WHERE username = ?1;";
    if (sqlite3_prepare_v2(m_pDatabase, cUpdateSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("LoginService: failed to prepare avatar update for '{}': {}", aUsername.c_str(), sqlite3_errmsg(m_pDatabase));
        return;
    }

    sqlite3_bind_text(pStatement, 1, aUsername.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pStatement, 2, aAvatar.c_str(), -1, SQLITE_TRANSIENT);

    const int stepResult = sqlite3_step(pStatement);
    if (stepResult != SQLITE_DONE)
        spdlog::error("LoginService: failed to update avatar for '{}': {}", aUsername.c_str(), sqlite3_errmsg(m_pDatabase));

    sqlite3_finalize(pStatement);
}

bool LoginService::InitializeSchema() noexcept
{
    if (!m_pDatabase)
        return false;

    char* pErrorMessage = nullptr;
    if (sqlite3_exec(m_pDatabase, kCreateUsersTableSql, nullptr, nullptr, &pErrorMessage) != SQLITE_OK)
    {
        spdlog::error("LoginService: failed to create users table: {}", pErrorMessage ? pErrorMessage : "unknown error");
        sqlite3_free(pErrorMessage);
        return false;
    }

    pErrorMessage = nullptr;
    if (sqlite3_exec(m_pDatabase, "ALTER TABLE users ADD COLUMN password_salt TEXT;", nullptr, nullptr, &pErrorMessage) != SQLITE_OK)
    {
        if (pErrorMessage)
        {
            std::string_view message(pErrorMessage);
            if (message.find("duplicate column name") == std::string_view::npos)
                spdlog::error("LoginService: failed to ensure password_salt column: {}", message);
            sqlite3_free(pErrorMessage);
        }
    }

    pErrorMessage = nullptr;
    if (sqlite3_exec(m_pDatabase, "ALTER TABLE users ADD COLUMN avatar TEXT DEFAULT '';", nullptr, nullptr, &pErrorMessage) != SQLITE_OK)
    {
        if (pErrorMessage)
        {
            std::string_view message(pErrorMessage);
            if (message.find("duplicate column name") == std::string_view::npos)
                spdlog::error("LoginService: failed to ensure avatar column: {}", message);
            sqlite3_free(pErrorMessage);
        }
    }

    return true;
}

LoginService::LoginResult LoginService::InsertUser(const TiltedPhoques::String& aUsername, const TiltedPhoques::String& aPasswordHash) noexcept
{
    sqlite3_stmt* pStatement = nullptr;
    constexpr const char* cInsertSql = "INSERT INTO users(username, password_hash, password_salt) VALUES(?1, ?2, ?3);";
    if (sqlite3_prepare_v2(m_pDatabase, cInsertSql, -1, &pStatement, nullptr) != SQLITE_OK)
    {
        spdlog::error("LoginService: failed to prepare insert statement: {}", sqlite3_errmsg(m_pDatabase));
        return LoginResult::InternalError;
    }

    const auto salt = Credential::GenerateSalt();
    const auto storedHash = Credential::DeriveServerPassword(aPasswordHash.c_str(), salt.c_str());

    sqlite3_bind_text(pStatement, 1, aUsername.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pStatement, 2, storedHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(pStatement, 3, salt.c_str(), -1, SQLITE_TRANSIENT);

    const int stepResult = sqlite3_step(pStatement);
    sqlite3_finalize(pStatement);

    if (stepResult != SQLITE_DONE)
    {
        spdlog::error("LoginService: failed to insert user '{}': {}", aUsername.c_str(), sqlite3_errmsg(m_pDatabase));
        return LoginResult::InternalError;
    }

    spdlog::info("LoginService: registered new user '{}'", aUsername.c_str());
    return LoginResult::Ok;
}
