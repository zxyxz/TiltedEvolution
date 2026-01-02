#pragma once

#include <TiltedCore/Buffer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

using TiltedPhoques::Buffer;

struct Guid
{
    std::array<uint8_t, 16> Bytes{};

    static Guid Random() noexcept;

    bool operator==(const Guid& acRhs) const noexcept { return Bytes == acRhs.Bytes; }
    bool operator!=(const Guid& acRhs) const noexcept { return !(*this == acRhs); }

    bool IsEmpty() const noexcept;
    void Clear() noexcept;

    void Serialize(Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Buffer::Reader& aReader) noexcept;

    std::string ToString() const;
};

namespace std
{
template <> struct hash<Guid>
{
    size_t operator()(const Guid& acValue) const noexcept
    {
        static_assert(sizeof(Guid::Bytes) == 16, "Guid must be 128-bit");

        size_t hash = 0;
        // combine two 64-bit halves to generate a stable hash
        for (size_t i = 0; i < acValue.Bytes.size(); i += 8)
        {
            uint64_t part = 0;
            for (size_t j = 0; j < 8; ++j)
            {
                part <<= 8;
                part |= static_cast<uint64_t>(acValue.Bytes[i + j]);
            }
            hash ^= std::hash<uint64_t>{}(part) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
        }

        return hash;
    }
};
} // namespace std
