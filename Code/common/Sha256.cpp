#include "Sha256.h"

#include <TiltedCore/Stl.hpp>

#include <cstdint>
#include <cstring>

namespace
{
constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t RotateRight(uint32_t aValue, uint32_t aCount)
{
    return (aValue >> aCount) | (aValue << (32 - aCount));
}

void ProcessChunk(const uint8_t* apChunk, uint32_t (&aState)[8])
{
    uint32_t w[64];

    for (uint32_t i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<uint32_t>(apChunk[i * 4]) << 24) | (static_cast<uint32_t>(apChunk[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(apChunk[i * 4 + 2]) << 8) | (static_cast<uint32_t>(apChunk[i * 4 + 3]));
    }

    for (uint32_t i = 16; i < 64; ++i)
    {
        const uint32_t s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = aState[0];
    uint32_t b = aState[1];
    uint32_t c = aState[2];
    uint32_t d = aState[3];
    uint32_t e = aState[4];
    uint32_t f = aState[5];
    uint32_t g = aState[6];
    uint32_t h = aState[7];

    for (uint32_t i = 0; i < 64; ++i)
    {
        const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + s1 + ch + kSha256K[i] + w[i];
        const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    aState[0] += a;
    aState[1] += b;
    aState[2] += c;
    aState[3] += d;
    aState[4] += e;
    aState[5] += f;
    aState[6] += g;
    aState[7] += h;
}

std::array<uint8_t, 32> FinaliseHash(uint32_t (&aState)[8])
{
    std::array<uint8_t, 32> digest{};

    for (uint32_t i = 0; i < 8; ++i)
    {
        digest[i * 4] = static_cast<uint8_t>((aState[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((aState[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((aState[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(aState[i] & 0xFF);
    }

    return digest;
}
} // namespace

namespace Sha256
{
std::array<uint8_t, 32> Hash(const uint8_t* apData, std::size_t aLength) noexcept
{
    uint32_t state[8] = {
        0x6a09e667,
        0xbb67ae85,
        0x3c6ef372,
        0xa54ff53a,
        0x510e527f,
        0x9b05688c,
        0x1f83d9ab,
        0x5be0cd19,
    };

    const std::size_t fullChunks = aLength / 64;

    for (std::size_t i = 0; i < fullChunks; ++i)
        ProcessChunk(apData + i * 64, state);

    uint8_t buffer[64];
    const std::size_t remaining = aLength % 64;
    std::memset(buffer, 0, sizeof(buffer));
    if (remaining > 0)
        std::memcpy(buffer, apData + fullChunks * 64, remaining);

    buffer[remaining] = 0x80;

    if (remaining >= 56)
    {
        ProcessChunk(buffer, state);
        std::memset(buffer, 0, sizeof(buffer));
    }

    const uint64_t bitLength = static_cast<uint64_t>(aLength) * 8;
    buffer[63] = static_cast<uint8_t>(bitLength);
    buffer[62] = static_cast<uint8_t>(bitLength >> 8);
    buffer[61] = static_cast<uint8_t>(bitLength >> 16);
    buffer[60] = static_cast<uint8_t>(bitLength >> 24);
    buffer[59] = static_cast<uint8_t>(bitLength >> 32);
    buffer[58] = static_cast<uint8_t>(bitLength >> 40);
    buffer[57] = static_cast<uint8_t>(bitLength >> 48);
    buffer[56] = static_cast<uint8_t>(bitLength >> 56);

    ProcessChunk(buffer, state);

    return FinaliseHash(state);
}

std::array<uint8_t, 32> Hash(std::string_view aInput) noexcept
{
    return Hash(reinterpret_cast<const uint8_t*>(aInput.data()), aInput.size());
}

TiltedPhoques::String HashHex(const uint8_t* apData, std::size_t aLength) noexcept
{
    const auto hash = Hash(apData, aLength);

    TiltedPhoques::String hex;
    hex.reserve(hash.size() * 2);

    static constexpr char kHexDigits[] = "0123456789abcdef";
    for (uint8_t byte : hash)
    {
        hex.push_back(kHexDigits[(byte >> 4) & 0x0F]);
        hex.push_back(kHexDigits[byte & 0x0F]);
    }

    return hex;
}

TiltedPhoques::String HashHex(std::string_view aInput) noexcept
{
    return HashHex(reinterpret_cast<const uint8_t*>(aInput.data()), aInput.size());
}
} // namespace Sha256
