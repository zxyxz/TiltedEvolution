#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include <TiltedCore/Stl.hpp>

namespace Sha256
{
std::array<uint8_t, 32> Hash(std::string_view aInput) noexcept;
std::array<uint8_t, 32> Hash(const uint8_t* apData, std::size_t aLength) noexcept;

TiltedPhoques::String HashHex(std::string_view aInput) noexcept;
TiltedPhoques::String HashHex(const uint8_t* apData, std::size_t aLength) noexcept;
} // namespace Sha256
