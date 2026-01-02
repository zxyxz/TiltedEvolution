#pragma once

#include <Events/PacketEvent.h>
#include <Messages/RequestActorDrop.h>
#include <Messages/RequestPickupDroppedItem.h>
#include <Messages/RequestDroppedItems.h>
#include <Messages/RequestDroppedItemMove.h>
#include <Messages/RequestDroppedItemPhysicsDisabled.h>
#include <Messages/NotifyDroppedItems.h>
#include <TiltedCore/Stl.hpp>
#include <Structs/Guid.h>
#include <Structs/ServerItemType.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <Structs/GameId.h>

struct NotifyActorDrop;
struct NotifyDroppedItemPickedUp;
struct sqlite3;

struct World;
struct OwnerComponent;
struct Player;
struct UpdateEvent;

class DropService
{
public:
    DropService(World& aWorld, entt::dispatcher& aDispatcher);
    ~DropService();

private:
    struct ActiveDrop
    {
        uint64_t DropId{};
        uint32_t ServerId{};
        uint32_t ActorFormId{};
        uint32_t OriginPlayerId{};
        ServerItemType Type{ServerItemType::Dropped};
        Inventory::Entry DropEntry{};
        Inventory::Entry PickupEntry{};
        bool HasLocation{false};
        Vector3_NetQuantize Location{};
        bool HasRotation{false};
        Vector3_NetQuantize Rotation{};
        bool HasVelocity{false};
        Vector3_NetQuantize Velocity{};
        bool HasAngularVelocity{false};
        Vector3_NetQuantize AngularVelocity{};
        GameId CellId{};
        GameId WorldSpaceId{};
        GameId ReferenceId{};
        Guid ClientDropId{};
        uint64_t SpawnEpoch{1};
        uint64_t Version{1};
    };

    void OnDropRequest(const PacketEvent<RequestActorDrop>& acMessage) noexcept;
    void OnPickupRequest(const PacketEvent<RequestPickupDroppedItem>& acMessage) noexcept;
    void OnDroppedItemsRequest(const PacketEvent<RequestDroppedItems>& acMessage) noexcept;
    void OnDropMoveRequest(const PacketEvent<RequestDroppedItemMove>& acMessage) noexcept;
    void OnDropPhysicsDisabledRequest(const PacketEvent<RequestDroppedItemPhysicsDisabled>& acMessage) noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    bool InitializeDatabase() noexcept;
    void ShutdownDatabase() noexcept;
    void LoadPersistedDrops() noexcept;
    bool BeginTransaction() noexcept;
    bool CommitTransaction() noexcept;
    void RollbackTransaction() noexcept;
    std::optional<uint64_t> FindExistingDropId(uint32_t aPlayerId, const Guid& acClientDropId) noexcept;
    std::optional<ActiveDrop> FetchDropFromDatabase(uint64_t aDropId) noexcept;
    bool InsertDrop(const ActiveDrop& acDrop, uint64_t& aOutDropId) noexcept;
    bool InsertDropHistory(uint64_t aDropId, std::string_view aAction, uint32_t aPerformedBy, const std::string& acDetails) noexcept;
    bool UpdateDropLocation(const ActiveDrop& acDrop) noexcept;
    bool MarkDropInactive(uint64_t aDropId) noexcept;
    ActiveDrop* ResolveActiveDrop(uint64_t aDropId) noexcept;
    void TrackActiveDrop(const ActiveDrop& acDrop) noexcept;
    void RemoveActiveDrop(uint64_t aDropId) noexcept;
    void IndexDrop(uint64_t aDropId, const GameId& acCellId) noexcept;
    void EraseDropFromIndex(uint64_t aDropId, const GameId& acCellId) noexcept;
    NotifyDroppedItems::Entry MakeNotifyEntry(const ActiveDrop& acDrop) const noexcept;
    void HandleUntrackedPickupRequest(const PacketEvent<RequestPickupDroppedItem>& acMessage) noexcept;
    void BroadcastPickup(const NotifyDroppedItemPickedUp& acMessage) const noexcept;
    void CleanupExpiredDrops() noexcept;
    bool InitializeCreationEngineDatabase() noexcept;
    void ShutdownCreationEngineDatabase() noexcept;
    bool IsCreationEnginePickupRecorded(const GameId& acEngineRefId) noexcept;
    bool RecordCreationEnginePickup(const GameId& acEngineRefId, const GameId& acCellId, const GameId& acWorldId, uint32_t aPickedBy) noexcept;
    void CleanupExpiredCreationEnginePickups() noexcept;

    World& m_world;
    entt::scoped_connection m_requestDropConnection;
    entt::scoped_connection m_requestPickupConnection;
    entt::scoped_connection m_requestDroppedItemsConnection;
    entt::scoped_connection m_requestDropMoveConnection;
    entt::scoped_connection m_requestDropPhysicsDisabledConnection;
    entt::scoped_connection m_updateConnection;

    TiltedPhoques::Map<uint64_t, ActiveDrop> m_activeDrops;
    TiltedPhoques::Map<GameId, TiltedPhoques::Vector<uint64_t>> m_cellDropIndex;
    TiltedPhoques::Map<GameId, uint64_t> m_referenceDropIndex;
    sqlite3* m_pDatabase{nullptr};
    std::filesystem::path m_databasePath;
    sqlite3* m_pCreationEngineDatabase{nullptr};
    std::filesystem::path m_creationEngineDatabasePath;
    double m_cleanupAccumulator{0.0};
};
