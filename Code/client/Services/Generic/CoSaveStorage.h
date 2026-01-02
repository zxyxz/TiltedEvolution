#pragma once

#include <Structs/Inventory.h>
#include <Structs/GameId.h>
#include <Structs/ServerItemType.h>
#include <Structs/Vector3_NetQuantize.h>
#include <Games/Primitives.h>
#include <TiltedCore/Stl.hpp>

#include <filesystem>

namespace CoSaveStorage
{
struct Entry
{
    uint64_t DropId{};
    ServerItemType Type{ServerItemType::Dropped};
    uint32_t ServerId{};
    GameId CellId{};
    GameId WorldSpaceId{};
    GameId ReferenceId{};
    Inventory::Entry Item{};
    NiPoint3 Location{};
    NiPoint3 Rotation{};
    uint32_t RefFormId{};
    uint64_t LastSeenTimestamp{};
};

struct LocalPlayerLocation
{
    bool HasLocation{false};
    bool HasExterior{false};
    Vector3_NetQuantize Position{};
    GameId WorldSpaceId{};
    GameId CellId{};
    uint64_t LastSeenEpoch{0};
    Vector3_NetQuantize ExteriorPosition{};
    GameId ExteriorWorldSpaceId{};
    GameId ExteriorCellId{};
    uint64_t ExteriorLastSeenEpoch{0};

    bool operator==(const LocalPlayerLocation& rhs) const noexcept
    {
        return HasLocation == rhs.HasLocation && HasExterior == rhs.HasExterior && Position == rhs.Position &&
               WorldSpaceId == rhs.WorldSpaceId && CellId == rhs.CellId && LastSeenEpoch == rhs.LastSeenEpoch &&
               ExteriorPosition == rhs.ExteriorPosition && ExteriorWorldSpaceId == rhs.ExteriorWorldSpaceId &&
               ExteriorCellId == rhs.ExteriorCellId && ExteriorLastSeenEpoch == rhs.ExteriorLastSeenEpoch;
    }
    bool operator!=(const LocalPlayerLocation& rhs) const noexcept { return !(*this == rhs); }
};

bool Load(const std::filesystem::path& aPath, TiltedPhoques::Map<uint64_t, Entry>& aOut) noexcept;
bool Load(const std::filesystem::path& aPath, TiltedPhoques::Map<uint64_t, Entry>& aOut, LocalPlayerLocation* apOutLocation) noexcept;
bool Save(const std::filesystem::path& aPath, const TiltedPhoques::Map<uint64_t, Entry>& aIn) noexcept;
bool Save(const std::filesystem::path& aPath, const TiltedPhoques::Map<uint64_t, Entry>& aIn, const LocalPlayerLocation* apLocation) noexcept;
} // namespace CoSaveStorage
