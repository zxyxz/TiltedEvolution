#pragma once

#include <TiltedCore/Stl.hpp>

#include <filesystem>

#include <entt/fwd.hpp>

struct sqlite3;
struct World;

struct AdminService
{
    AdminService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~AdminService() noexcept;

    TP_NOCOPYMOVE(AdminService);

    bool IsAdmin(const TiltedPhoques::String& aUsername) const noexcept;
    bool AddAdmin(const TiltedPhoques::String& aUsername) noexcept;
    bool RemoveAdmin(const TiltedPhoques::String& aUsername) noexcept;
    void GetAdmins(TiltedPhoques::Vector<TiltedPhoques::String>& aOutAdmins) const noexcept;

private:
    static TiltedPhoques::String NormalizeUsername(const TiltedPhoques::String& aUsername);
    std::filesystem::path ResolveAdminDatabasePath() const noexcept;
    bool InitializeDatabase() noexcept;
    void ShutdownDatabase() noexcept;

    World& m_world;
    sqlite3* m_pDatabase{nullptr};
    std::filesystem::path m_databasePath;
};
