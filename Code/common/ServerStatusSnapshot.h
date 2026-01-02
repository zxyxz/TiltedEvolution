#pragma once

#include <TiltedCore/Stl.hpp>

struct ServerPlayerStatusSnapshot
{
    uint32_t PlayerId{};
    TiltedPhoques::String Username;
    uint32_t CellBaseId{};
    uint32_t CellModId{};
    uint32_t WorldBaseId{};
    uint32_t WorldModId{};
    int32_t GridX{};
    int32_t GridY{};
    bool HasPosition{false};
    float PositionX{};
    float PositionY{};
    float PositionZ{};
};

struct ServerStatusSnapshot
{
    uint32_t UptimeSeconds{};
    TiltedPhoques::Vector<ServerPlayerStatusSnapshot> Players;
};
