#pragma once

#include <Sync/DropManager.h>
#include <Services/Generic/CoSaveStorage.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class DropStorage final : public DropManager::StorageListener
{
public:
    using CachedDrop = CoSaveStorage::Entry;

    DropStorage() = default;
    ~DropStorage() override;

    void SetActiveUser(std::string aUsername) noexcept;
    void PrepareInMemory() noexcept;
    void OnLoadGameReset() noexcept;
    void Shutdown() noexcept;
    bool LoadFromPath(const std::filesystem::path& aSavePath) noexcept;
    bool SaveToPath(const std::filesystem::path& aSavePath) noexcept;
    std::vector<CachedDrop> GetDropsForCell(const GameId& aCellId, const GameId& aWorldId) const noexcept;
    std::vector<CachedDrop> GetAllDrops() const noexcept;
    std::optional<uint32_t> GetRefFormId(uint64_t aDropId) const noexcept;
    std::optional<uint64_t> FindDropIdByRefFormId(uint32_t aRefFormId, const GameId& aCellId, const GameId& aWorldId) const noexcept;
    void SetLocalPlayerLocation(const CoSaveStorage::LocalPlayerLocation& aLocation) noexcept;
    std::optional<CoSaveStorage::LocalPlayerLocation> GetLocalPlayerLocation() const noexcept;
    void RemoveCachedDrop(uint64_t aDropId) noexcept;

    void OnServerDropTracked(uint64_t aDropId, const DropManager::ServerDropData& acData) noexcept override;
    void OnDropHandleBound(uint64_t aDropId, uint32_t aHandleBits) noexcept override;
    void OnServerDropRemoved(uint64_t aDropId) noexcept override;

private:
    static std::string SanitizeUser(const std::string& aUsername);
    void EnsureDirectories(const std::filesystem::path& aPath) const noexcept;
    uint32_t ResolveRefFormId(uint32_t aHandleBits) const noexcept;

    std::filesystem::path m_storagePath;
    std::string m_activeUser{};
    bool m_initialized{false};
    bool m_dirty{false};
    TiltedPhoques::Map<uint64_t, CachedDrop> m_cachedDrops;
    std::optional<CoSaveStorage::LocalPlayerLocation> m_localPlayerLocation;
};
