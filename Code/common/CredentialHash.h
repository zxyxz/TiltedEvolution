#pragma once

#include <TiltedCore/Stl.hpp>

#include <string_view>

namespace Credential
{
TiltedPhoques::String HashPassword(std::string_view aPlainPassword) noexcept;
TiltedPhoques::String DeriveServerPassword(std::string_view aClientHash, std::string_view aSalt) noexcept;
TiltedPhoques::String GenerateSalt(std::size_t aBytes = 16) noexcept;
bool LooksLikePasswordHash(std::string_view aValue) noexcept;
} // namespace Credential
