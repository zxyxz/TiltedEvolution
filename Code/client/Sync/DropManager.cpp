#include "DropManager.h"

#include <TiltedCore/Stl.hpp>
#include <spdlog/spdlog.h>

namespace DropManager
{
namespace
{
    TiltedPhoques::Map<Guid, LocalDropData> s_localDrops;
    TiltedPhoques::Map<uint64_t, ServerDropData> s_serverDrops;
    TiltedPhoques::Map<uint32_t, uint64_t> s_handleBindings;
    TiltedPhoques::Map<GameId, uint64_t> s_referenceBindings;
    StorageListener* s_pStorageListener = nullptr;

    void UnbindHandle(uint32_t handleBits) noexcept
    {
        if (handleBits == 0)
            return;

        s_handleBindings.erase(handleBits);
    }

    void BindHandle(uint32_t handleBits, uint64_t dropId) noexcept
    {
        if (handleBits == 0)
            return;
        s_handleBindings[handleBits] = dropId;
    }
} // namespace

Guid RegisterLocalDrop(LocalDropData data) noexcept
{
    Guid clientDropId = data.ClientDropId;
    if (clientDropId.IsEmpty())
        clientDropId = Guid::Random();

    data.ClientDropId = clientDropId;
    s_localDrops[clientDropId] = std::move(data);
    return clientDropId;
}

std::optional<LocalDropData> ConsumeLocalDrop(const Guid& clientDropId) noexcept
{
    const auto it = s_localDrops.find(clientDropId);
    if (it == s_localDrops.end())
        return std::nullopt;

    auto data = it->second;
    s_localDrops.erase(it);
    return data;
}

void TrackServerDrop(uint64_t dropId, const ServerDropData& data) noexcept
{
    ServerDropData merged = data;

    const bool existed = s_serverDrops.find(dropId) != std::end(s_serverDrops);
    GameId previousReference{};
    if (existed)
    {
        auto& current = s_serverDrops[dropId];
        previousReference = current.ReferenceId;
        if (!merged.ServerId)
            merged.ServerId = current.ServerId;
        if (!merged.HandleBits)
            merged.HandleBits = current.HandleBits;
        if (!merged.ActorFormId && current.ActorFormId)
            merged.ActorFormId = current.ActorFormId;
        if (!merged.ReferenceId && current.ReferenceId)
            merged.ReferenceId = current.ReferenceId;
        if (!merged.CellId && current.CellId)
            merged.CellId = current.CellId;
        if (!merged.WorldSpaceId && current.WorldSpaceId)
            merged.WorldSpaceId = current.WorldSpaceId;
        if (merged.Type == ServerItemType::Dropped && current.Type == ServerItemType::CreationEngine)
            merged.Type = current.Type;
        if (!merged.HasVelocity && current.HasVelocity)
        {
            merged.HasVelocity = true;
            merged.Velocity = current.Velocity;
        }
    }

    s_serverDrops[dropId] = merged;
    if (merged.HandleBits)
        BindHandle(merged.HandleBits, dropId);
    if (previousReference && previousReference != merged.ReferenceId)
        s_referenceBindings.erase(previousReference);
    if (merged.ReferenceId)
        s_referenceBindings[merged.ReferenceId] = dropId;

    if (s_pStorageListener)
        s_pStorageListener->OnServerDropTracked(dropId, merged);

    auto logLevel = existed ? spdlog::level::debug : spdlog::level::info;
    spdlog::log(logLevel, "DropManager: tracked drop {} server {:X} actor {:X} item {:X}:{:X} type {} loc ({:.2f}, {:.2f}, {:.2f}) handle {:X}", dropId, merged.ServerId, merged.ActorFormId,
                merged.Item.BaseId.ModId, merged.Item.BaseId.BaseId, merged.Type == ServerItemType::CreationEngine ? "ce" : "drop", merged.Location.x, merged.Location.y, merged.Location.z, merged.HandleBits);
}

bool BindHandleToServerDrop(uint64_t dropId, uint32_t actorFormId, uint32_t handleBits) noexcept
{
    auto dropIt = s_serverDrops.find(dropId);
    if (dropIt == s_serverDrops.end())
        return false;

    auto& drop = dropIt.value();
    if (drop.HandleBits != 0 && drop.HandleBits != handleBits)
        UnbindHandle(drop.HandleBits);
    drop.ActorFormId = actorFormId;
    drop.HandleBits = handleBits;
    BindHandle(handleBits, dropId);
    if (s_pStorageListener)
        s_pStorageListener->OnDropHandleBound(dropId, handleBits);

    spdlog::info("DropManager: bound handle {:X} to drop {} actor {:X}", handleBits, dropId, actorFormId);
    return true;
}

void ClearHandleBinding(uint64_t dropId) noexcept
{
    auto dropIt = s_serverDrops.find(dropId);
    if (dropIt == s_serverDrops.end())
        return;

    auto& drop = dropIt.value();
    if (drop.HandleBits == 0)
        return;

    const uint32_t previousHandle = drop.HandleBits;
    UnbindHandle(previousHandle);
    drop.HandleBits = 0;

    if (s_pStorageListener)
        s_pStorageListener->OnDropHandleBound(dropId, 0);

    spdlog::info("DropManager: cleared handle {:X} for drop {}", previousHandle, dropId);
}

void SetReferenceForDrop(uint64_t dropId, const GameId& referenceId) noexcept
{
    if (!referenceId)
        return;

    if (s_serverDrops.find(dropId) == s_serverDrops.end())
        return;

    auto& drop = s_serverDrops[dropId];
    if (drop.ReferenceId == referenceId)
        return;

    if (drop.ReferenceId)
        s_referenceBindings.erase(drop.ReferenceId);

    drop.ReferenceId = referenceId;
    s_referenceBindings[referenceId] = dropId;
    if (s_pStorageListener)
        s_pStorageListener->OnServerDropTracked(dropId, drop);
}

std::optional<uint64_t> GetDropIdForHandle(uint32_t handleBits) noexcept
{
    if (handleBits == 0)
        return std::nullopt;

    const auto handleIt = s_handleBindings.find(handleBits);
    if (handleIt == s_handleBindings.end())
        return std::nullopt;

    return handleIt->second;
}

std::optional<uint64_t> GetDropIdForReference(const GameId& referenceId) noexcept
{
    if (!referenceId)
        return std::nullopt;

    const auto it = s_referenceBindings.find(referenceId);
    if (it == s_referenceBindings.end())
        return std::nullopt;

    return it->second;
}

std::optional<uint32_t> GetHandleForDrop(uint64_t dropId) noexcept
{
    const auto it = s_serverDrops.find(dropId);
    if (it == s_serverDrops.end())
        return std::nullopt;

    if (it->second.HandleBits == 0)
        return std::nullopt;

    return it->second.HandleBits;
}

std::optional<ServerDropData> GetServerDrop(uint64_t dropId) noexcept
{
    const auto it = s_serverDrops.find(dropId);
    if (it == s_serverDrops.end())
        return std::nullopt;

    return it->second;
}

bool UpdateServerDropTransform(uint64_t dropId, const NiPoint3& acLocation, const NiPoint3& acRotation, const GameId& acCellId, const GameId& acWorldSpaceId, const GameId& acReferenceId, bool aHasVelocity,
                               const NiPoint3& acVelocity) noexcept
{
    if (s_serverDrops.find(dropId) == s_serverDrops.end())
        return false;

    auto& drop = s_serverDrops[dropId];
    const GameId previousReference = drop.ReferenceId;
    drop.Location = acLocation;
    drop.Rotation = acRotation;
    if (aHasVelocity)
    {
        drop.HasVelocity = true;
        drop.Velocity = acVelocity;
    }
    if (acCellId)
        drop.CellId = acCellId;
    if (acWorldSpaceId)
        drop.WorldSpaceId = acWorldSpaceId;
    if (acReferenceId)
    {
        drop.ReferenceId = acReferenceId;
        if (previousReference != drop.ReferenceId)
        {
            if (previousReference)
                s_referenceBindings.erase(previousReference);
            s_referenceBindings[drop.ReferenceId] = dropId;
        }
    }

    if (s_pStorageListener)
        s_pStorageListener->OnServerDropTracked(dropId, drop);

    return true;
}

std::optional<uint64_t> FindDropBySignature(const GameId& aBaseId, const NiPoint3& aLocation, float aRadiusSq) noexcept
{
    uint64_t bestDropId = 0;
    float bestDistanceSq = aRadiusSq;

    for (const auto& [dropId, data] : s_serverDrops)
    {
        if (data.Item.BaseId != aBaseId)
            continue;

        const float dx = data.Location.x - aLocation.x;
        const float dy = data.Location.y - aLocation.y;
        const float dz = data.Location.z - aLocation.z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq <= bestDistanceSq)
        {
            bestDistanceSq = distSq;
            bestDropId = dropId;
        }
    }

    if (bestDropId == 0)
    {
        spdlog::debug("DropManager: no drop match for {:X}:{:X} near ({:.2f}, {:.2f}, {:.2f})", aBaseId.ModId, aBaseId.BaseId, aLocation.x, aLocation.y, aLocation.z);
        return std::nullopt;
    }

    spdlog::info("DropManager: matched drop {} for {:X}:{:X} within {:.2f}", bestDropId, aBaseId.ModId, aBaseId.BaseId, std::sqrt(bestDistanceSq));
    return bestDropId;
}

void RemoveServerDrop(uint64_t dropId) noexcept
{
    const auto it = s_serverDrops.find(dropId);
    if (it == s_serverDrops.end())
        return;

    UnbindHandle(it->second.HandleBits);
    if (it->second.ReferenceId)
        s_referenceBindings.erase(it->second.ReferenceId);
    s_serverDrops.erase(it);
    if (s_pStorageListener)
        s_pStorageListener->OnServerDropRemoved(dropId);

    spdlog::info("DropManager: removed drop {}", dropId);
}

void SetStorageListener(StorageListener* apListener) noexcept
{
    s_pStorageListener = apListener;
}
} // namespace DropManager
