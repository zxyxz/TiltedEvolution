#pragma once

#include <Structs/GameId.h>
#include <Structs/Vector3_NetQuantize.h>

#include <entt/entt.hpp>

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <TiltedCore/Stl.hpp>

struct Player;
struct PlayerJoinEvent;
struct PlayerLeaveEvent;
struct UpdateEvent;
struct World;

struct PlayerLocation
{
    enum class Source : uint8_t
    {
        Unknown = 0,
        Movement = 1,
        ClientReport = 2,
        CellChange = 3,
        Respawn = 4,
        Teleport = 5
    };

    Vector3_NetQuantize Position{};
    GameId WorldSpaceId{};
    GameId CellId{};
    uint64_t LastSeenEpoch{0};
    Source SourceTag{Source::Unknown};
    bool HasPosition{false};
    Vector3_NetQuantize LastExteriorPosition{};
    GameId LastExteriorWorldSpaceId{};
    GameId LastExteriorCellId{};
    uint64_t LastExteriorEpoch{0};
    bool HasExterior{false};
    TiltedPhoques::String Username{};
    TiltedPhoques::String Endpoint{};

    std::chrono::steady_clock::time_point LastPersist{};
};

struct PlayerLocationService
{
    PlayerLocationService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PlayerLocationService() noexcept;

    TP_NOCOPYMOVE(PlayerLocationService);

    void UpdateLocation(Player* apPlayer, const Vector3_NetQuantize& acPos, const GameId& acWorldSpaceId, const GameId& acCellId,
                        PlayerLocation::Source aSource, bool aForcePersist = false) noexcept;
    void UpdateCell(Player* apPlayer, const GameId& acWorldSpaceId, const GameId& acCellId, PlayerLocation::Source aSource, bool aForcePersist = false) noexcept;
    bool TryGetLocation(uint32_t aPlayerId, PlayerLocation& aOut) const noexcept;

private:
    void OnPlayerJoin(const PlayerJoinEvent& aEvent) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& aEvent) noexcept;
    void OnUpdate(const UpdateEvent& aEvent) noexcept;

    bool InitializeDatabase() noexcept;
    void ShutdownDatabase() noexcept;
    void LoadFromDatabase(Player* apPlayer) noexcept;
    void PersistLocation(PlayerLocation& aLocation, uint32_t aPlayerId, const char* apReason) noexcept;

    World& m_world;
    sqlite3* m_pDatabase{nullptr};
    std::filesystem::path m_databasePath{};

    std::unordered_map<uint32_t, PlayerLocation> m_locations;

    entt::scoped_connection m_playerJoinConnection;
    entt::scoped_connection m_playerLeaveConnection;
    entt::scoped_connection m_updateConnection;
};
