#pragma once

#include <Structs/Guid.h>
#include <Structs/Inventory.h>
#include <Structs/GameId.h>
#include <Structs/ServerItemType.h>
#include <Games/Primitives.h>
#include <optional>

namespace DropManager
{
struct LocalDropData
{
    uint32_t ActorFormId{};
    Inventory::Entry Item{};
    NiPoint3 Location{};
    NiPoint3 Rotation{};
    uint32_t HandleBits{};
    GameId CellId{};
    GameId WorldSpaceId{};
    Guid ClientDropId{};
    GameId ReferenceId{};
};

struct ServerDropData
{
    uint32_t ServerId{};
    uint32_t ActorFormId{};
    ServerItemType Type{ServerItemType::Dropped};
    Inventory::Entry Item{};
    NiPoint3 Location{};
    NiPoint3 Rotation{};
    NiPoint3 Velocity{};
    bool HasVelocity{false};
    uint32_t HandleBits{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
};

struct StorageListener
{
    virtual ~StorageListener() = default;
    virtual void OnServerDropTracked(uint64_t aDropId, const ServerDropData& acData) noexcept = 0;
    virtual void OnDropHandleBound(uint64_t aDropId, uint32_t aHandleBits) noexcept = 0;
    virtual void OnServerDropRemoved(uint64_t aDropId) noexcept = 0;
};

Guid RegisterLocalDrop(LocalDropData data) noexcept;
std::optional<LocalDropData> ConsumeLocalDrop(const Guid& clientDropId) noexcept;

void TrackServerDrop(uint64_t dropId, const ServerDropData& data) noexcept;
bool BindHandleToServerDrop(uint64_t dropId, uint32_t actorFormId, uint32_t handleBits) noexcept;
void ClearHandleBinding(uint64_t dropId) noexcept;
void SetReferenceForDrop(uint64_t dropId, const GameId& referenceId) noexcept;

std::optional<uint64_t> GetDropIdForHandle(uint32_t handleBits) noexcept;
std::optional<uint64_t> GetDropIdForReference(const GameId& referenceId) noexcept;
std::optional<uint32_t> GetHandleForDrop(uint64_t dropId) noexcept;
std::optional<ServerDropData> GetServerDrop(uint64_t dropId) noexcept;
std::optional<uint64_t> FindDropBySignature(const GameId& aBaseId, const NiPoint3& aLocation, float aRadiusSq) noexcept;
bool UpdateServerDropTransform(uint64_t dropId, const NiPoint3& acLocation, const NiPoint3& acRotation, const GameId& acCellId, const GameId& acWorldSpaceId, const GameId& acReferenceId, bool aHasVelocity = false,
                               const NiPoint3& acVelocity = {}) noexcept;

void RemoveServerDrop(uint64_t dropId) noexcept;
void SetStorageListener(StorageListener* apListener) noexcept;
} // namespace DropManager
