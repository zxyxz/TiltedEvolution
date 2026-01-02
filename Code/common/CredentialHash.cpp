#include "CredentialHash.h"

#include "Sha256.h"

#include <algorithm>
#include <cctype>
#include <random>
#include <vector>
#include <sstream>
#include <string>

namespace
{
constexpr char kHexAlphabet[] = "0123456789abcdef";

TiltedPhoques::String ToHex(const uint8_t* apData, size_t aSize) noexcept
{
    TiltedPhoques::String result;
    result.reserve(aSize * 2);

    for (size_t i = 0; i < aSize; ++i)
    {
        const uint8_t value = apData[i];
        result.push_back(kHexAlphabet[(value >> 4) & 0x0F]);
        result.push_back(kHexAlphabet[value & 0x0F]);
    }

    return result;
}
} // namespace

namespace Credential
{
TiltedPhoques::String HashPassword(std::string_view aPlainPassword) noexcept
{
    return Sha256::HashHex(aPlainPassword);
}

TiltedPhoques::String DeriveServerPassword(std::string_view aClientHash, std::string_view aSalt) noexcept
{
    std::string combined;
    combined.reserve(aClientHash.size() + aSalt.size());
    combined.append(aSalt.data(), aSalt.size());
    combined.append(aClientHash.data(), aClientHash.size());

    return Sha256::HashHex(combined);
}

TiltedPhoques::String GenerateSalt(std::size_t aBytes) noexcept
{
    if (aBytes == 0)
        aBytes = 16;

    std::vector<uint8_t> buffer(aBytes);

    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<uint16_t> distribution(0, 255);

    for (auto& byte : buffer)
        byte = static_cast<uint8_t>(distribution(generator));

    return ToHex(buffer.data(), buffer.size());
}

bool LooksLikePasswordHash(std::string_view aValue) noexcept
{
    if (aValue.length() != 64)
        return false;

    return std::all_of(aValue.begin(), aValue.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}
} // namespace Credential
