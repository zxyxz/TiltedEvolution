#pragma once

#include <TiltedCore/Platform.hpp>
#include <TiltedCore/Stl.hpp>
#include <string_view>
#include <entt/fwd.hpp>

struct sqlite3;
struct World;

/**
 * @brief Persists account credentials for password-based login.
 */
class LoginService
{
public:
    enum class LoginResult
    {
        Ok,
        InvalidCredentials,
        InternalError,
    };

    LoginService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~LoginService() noexcept;

    TP_NOCOPYMOVE(LoginService);

    [[nodiscard]] LoginResult VerifyOrCreateUser(const TiltedPhoques::String& aUsername, const TiltedPhoques::String& aPassword) noexcept;
    [[nodiscard]] TiltedPhoques::String GetAvatar(const TiltedPhoques::String& aUsername) const noexcept;
    void SetAvatar(const TiltedPhoques::String& aUsername, const TiltedPhoques::String& aAvatar) noexcept;

private:
    [[nodiscard]] bool InitializeSchema() noexcept;
    [[nodiscard]] LoginResult InsertUser(const TiltedPhoques::String& aUsername, const TiltedPhoques::String& aPasswordHash) noexcept;

private:
    sqlite3* m_pDatabase{nullptr};
};
