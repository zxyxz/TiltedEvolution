#include "DropStorage.h"

#include <TESObjectREFR.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <chrono>

namespace
{

uint64_t GetEpochSeconds() noexcept
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
} // namespace

DropStorage::~DropStorage()
{
    Shutdown();
}

void DropStorage::SetActiveUser(std::string aUsername) noexcept
{
    const auto sanitized = SanitizeUser(aUsername);
    if (sanitized == m_activeUser)
        return;

    Shutdown();
    m_activeUser = sanitized.empty() ? "default" : sanitized;
}

void DropStorage::PrepareInMemory() noexcept
{
    if (m_activeUser.empty())
        m_activeUser = "default";
    m_initialized = true;
}

void DropStorage::OnLoadGameReset() noexcept
{
    m_cachedDrops.clear();
    m_localPlayerLocation.reset();
    m_storagePath.clear();
    m_initialized = false;
    m_dirty = false;
}

void DropStorage::Shutdown() noexcept
{
    m_cachedDrops.clear();
    m_localPlayerLocation.reset();
    m_storagePath.clear();
    m_initialized = false;
    m_dirty = false;
}

std::vector<DropStorage::CachedDrop> DropStorage::GetDropsForCell(const GameId& aCellId, const GameId& aWorldId) const noexcept
{
    std::vector<CachedDrop> drops;
    drops.reserve(m_cachedDrops.size());

    for (const auto& [dropId, drop] : m_cachedDrops)
    {
        if (drop.CellId == aCellId && drop.WorldSpaceId == aWorldId)
            drops.push_back(drop);
    }

    return drops;
}

std::vector<DropStorage::CachedDrop> DropStorage::GetAllDrops() const noexcept
{
    std::vector<CachedDrop> drops;
    drops.reserve(m_cachedDrops.size());
    for (const auto& [_, drop] : m_cachedDrops)
        drops.push_back(drop);
    return drops;
}

std::optional<uint32_t> DropStorage::GetRefFormId(uint64_t aDropId) const noexcept
{
    const auto it = m_cachedDrops.find(aDropId);
    if (it == m_cachedDrops.end())
        return std::nullopt;

    const auto& drop = it.value();
    if (drop.RefFormId == 0)
        return std::nullopt;

    return drop.RefFormId;
}

std::optional<uint64_t> DropStorage::FindDropIdByRefFormId(uint32_t aRefFormId, const GameId& aCellId, const GameId& aWorldId) const noexcept
{
    if (aRefFormId == 0)
        return std::nullopt;

    for (const auto& [dropId, drop] : m_cachedDrops)
    {
        if (drop.RefFormId != aRefFormId)
            continue;

        if (aCellId && drop.CellId && drop.CellId != aCellId)
            continue;

        if (aWorldId && drop.WorldSpaceId && drop.WorldSpaceId != aWorldId)
            continue;

        return dropId;
    }

    return std::nullopt;
}

void DropStorage::SetLocalPlayerLocation(const CoSaveStorage::LocalPlayerLocation& aLocation) noexcept
{
    PrepareInMemory();
    if (!m_localPlayerLocation || *m_localPlayerLocation != aLocation)
    {
        m_localPlayerLocation = aLocation;
        m_dirty = true;
    }
}

std::optional<CoSaveStorage::LocalPlayerLocation> DropStorage::GetLocalPlayerLocation() const noexcept
{
    return m_localPlayerLocation;
}

void DropStorage::RemoveCachedDrop(uint64_t aDropId) noexcept
{
    if (m_cachedDrops.erase(aDropId) > 0)
        m_dirty = true;
}

void DropStorage::OnServerDropTracked(uint64_t aDropId, const DropManager::ServerDropData& acData) noexcept
{
    PrepareInMemory();

    CachedDrop drop{};
    drop.DropId = aDropId;
    drop.Type = acData.Type;
    drop.ServerId = acData.ServerId;
    drop.CellId = acData.CellId;
    drop.WorldSpaceId = acData.WorldSpaceId;
    drop.ReferenceId = acData.ReferenceId;
    drop.Item = acData.Item;
    drop.Location = acData.Location;
    drop.Rotation = acData.Rotation;
    drop.LastSeenTimestamp = GetEpochSeconds();

    if (auto it = m_cachedDrops.find(aDropId); it != m_cachedDrops.end())
    {
        drop.RefFormId = it->second.RefFormId;
        if (drop.Type == ServerItemType::Dropped)
            drop.Type = it->second.Type;
        if (drop.LastSeenTimestamp == 0)
            drop.LastSeenTimestamp = it->second.LastSeenTimestamp;
    }
    else
        drop.RefFormId = 0;

    m_cachedDrops[aDropId] = drop;
    spdlog::info("DropStorage: cached drop {} for cell {:X}:{:X}", aDropId, drop.CellId.ModId, drop.CellId.BaseId);
    m_dirty = true;
}

void DropStorage::OnDropHandleBound(uint64_t aDropId, uint32_t aHandleBits) noexcept
{
    PrepareInMemory();

    if (aHandleBits == 0)
        return;

    auto it = m_cachedDrops.find(aDropId);
    if (it == m_cachedDrops.end())
        return;

    auto& drop = it.value();
    drop.RefFormId = ResolveRefFormId(aHandleBits);
    m_dirty = true;
}

void DropStorage::OnServerDropRemoved(uint64_t aDropId) noexcept
{
    spdlog::debug("DropStorage: server drop {} removed (retaining cache for later cleanup)", aDropId);
}

std::string DropStorage::SanitizeUser(const std::string& aUsername)
{
    std::string result;
    result.reserve(aUsername.size());
    for (const char ch : aUsername)
    {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')
            result.push_back(ch);
        else
            result.push_back('_');
    }
    return result;
}

void DropStorage::EnsureDirectories(const std::filesystem::path& aPath) const noexcept
{
    if (aPath.empty())
        return;

    std::error_code ec;
    if (!std::filesystem::exists(aPath, ec))
        std::filesystem::create_directories(aPath, ec);

    if (ec)
        spdlog::warn("DropStorage: failed to ensure directory '{}': {}", aPath.string(), ec.message());
}

bool DropStorage::LoadFromPath(const std::filesystem::path& aSavePath) noexcept
{
    m_cachedDrops.clear();
    m_localPlayerLocation.reset();
    m_dirty = false;
    m_initialized = true;

    if (aSavePath.empty())
        return false;

    m_storagePath = aSavePath;

    CoSaveStorage::LocalPlayerLocation location{};
    if (!CoSaveStorage::Load(m_storagePath, m_cachedDrops, &location))
    {
        spdlog::warn("DropStorage: failed to load cache '{}'", m_storagePath.string());
        return false;
    }
    else
    {
        spdlog::info("DropStorage: loaded cache '{}' ({} entries)", m_storagePath.string(), m_cachedDrops.size());
    }

    if (location.HasLocation)
        m_localPlayerLocation = location;

    return true;
}

bool DropStorage::SaveToPath(const std::filesystem::path& aSavePath) noexcept
{
    if (aSavePath.empty())
        return false;

    PrepareInMemory();
    m_storagePath = aSavePath;
    if (!m_dirty)
        return true;

    EnsureDirectories(m_storagePath.parent_path());

    CoSaveStorage::LocalPlayerLocation location{};
    if (m_localPlayerLocation)
        location = *m_localPlayerLocation;
    else
        location.HasLocation = false;

    if (!CoSaveStorage::Save(m_storagePath, m_cachedDrops, &location))
    {
        spdlog::warn("DropStorage: failed to save cache '{}'", m_storagePath.string());
        return false;
    }

    spdlog::info("DropStorage: saved cache '{}' ({} entries)", m_storagePath.string(), m_cachedDrops.size());
    m_dirty = false;
    return true;
}

uint32_t DropStorage::ResolveRefFormId(uint32_t aHandleBits) const noexcept
{
    if (!aHandleBits)
        return 0;

    if (auto* pRef = TESObjectREFR::GetByHandle(aHandleBits))
        return pRef->formID;

    return 0;
}
