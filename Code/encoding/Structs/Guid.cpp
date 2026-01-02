#include "Guid.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <random>
#include <thread>

namespace
{
constexpr std::array<size_t, 4> kHyphenOffsets{4, 6, 8, 10};

std::array<uint32_t, 8> MakeSeedMaterial() noexcept
{
    std::array<uint32_t, 8> material{};
    bool seeded = false;

    try
    {
        std::random_device rd;
        for (auto& value : material)
        {
            value = rd();
        }
        seeded = true;
    }
    catch (...)
    {
    }

    if (!seeded)
    {
        const auto now = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const auto tidHash = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        const auto addr = reinterpret_cast<uintptr_t>(&material);

        material[0] ^= static_cast<uint32_t>(now);
        material[1] ^= static_cast<uint32_t>(now >> 32);
        material[2] ^= tidHash;
        material[3] ^= static_cast<uint32_t>(addr);
        material[4] ^= static_cast<uint32_t>(addr >> 32);

        uint32_t stir = 0x9E3779B9u;
        for (size_t i = 5; i < material.size(); ++i)
        {
            stir ^= (stir << 13);
            stir ^= (stir >> 17);
            stir ^= (stir << 5);
            material[i] ^= stir;
        }
    }

    return material;
}

std::mt19937_64 CreateSeededRng() noexcept
{
    auto seeds = MakeSeedMaterial();
    std::seed_seq seq(seeds.begin(), seeds.end());
    return std::mt19937_64(seq);
}

std::mt19937_64& GetRng() noexcept
{
    static std::mt19937_64 g_rng = CreateSeededRng();
    return g_rng;
}

std::mutex& GetRngMutex() noexcept
{
    static std::mutex g_mutex;
    return g_mutex;
}

} // namespace

Guid Guid::Random() noexcept
{
    Guid guid{};

    auto& rng = GetRng();
    auto& mutex = GetRngMutex();
    std::lock_guard<std::mutex> _(mutex);
    for (size_t i = 0; i < guid.Bytes.size(); i += 8)
    {
        uint64_t chunk = rng();
        for (size_t j = 0; j < 8; ++j)
        {
            guid.Bytes[i + j] = static_cast<uint8_t>(chunk & 0xFF);
            chunk >>= 8;
        }
    }

    // RFC 4122 version 4 + variant bits
    guid.Bytes[6] = static_cast<uint8_t>((guid.Bytes[6] & 0x0F) | 0x40);
    guid.Bytes[8] = static_cast<uint8_t>((guid.Bytes[8] & 0x3F) | 0x80);

    return guid;
}

bool Guid::IsEmpty() const noexcept
{
    for (const auto byte : Bytes)
    {
        if (byte != 0)
            return false;
    }

    return true;
}

void Guid::Clear() noexcept
{
    Bytes.fill(0);
}

void Guid::Serialize(Buffer::Writer& aWriter) const noexcept
{
    auto* pData = const_cast<uint8_t*>(Bytes.data());
    aWriter.WriteBytes(pData, Bytes.size());
}

void Guid::Deserialize(Buffer::Reader& aReader) noexcept
{
    aReader.ReadBytes(Bytes.data(), Bytes.size());
}

std::string Guid::ToString() const
{
    std::string result;
    result.reserve(36);
    static constexpr char kHexDigits[] = "0123456789abcdef";

    for (size_t i = 0; i < Bytes.size(); ++i)
    {
        result.push_back(kHexDigits[(Bytes[i] >> 4) & 0x0F]);
        result.push_back(kHexDigits[Bytes[i] & 0x0F]);
        if (std::find(kHyphenOffsets.begin(), kHyphenOffsets.end(), i + 1) != kHyphenOffsets.end())
            result.push_back('-');
    }

    return result;
}
