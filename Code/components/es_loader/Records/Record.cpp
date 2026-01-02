#include "Record.h"

#include <cstdlib>
#include <limits>
#include <string>
#include <zlib.h>

namespace
{
size_t GetMaxDecompressedSize() noexcept
{
    static size_t limit = []() {
        constexpr size_t kDefaultLimit = 256 * 1024 * 1024; // 256 MiB
        const char* env = std::getenv("ESLOADER_MAX_DECOMPRESS");
        if (!env || env[0] == '\0')
            return kDefaultLimit;
        char* end = nullptr;
        const unsigned long long value = std::strtoull(env, &end, 10);
        if (!end || end == env || value == 0)
            return kDefaultLimit;
        if (value > std::numeric_limits<size_t>::max())
            return std::numeric_limits<size_t>::max();
        return static_cast<size_t>(value);
    }();
    return limit;
}

std::string FourCC(uint32_t aValue)
{
    char text[5] = {
        static_cast<char>(aValue & 0xFF),
        static_cast<char>((aValue >> 8) & 0xFF),
        static_cast<char>((aValue >> 16) & 0xFF),
        static_cast<char>((aValue >> 24) & 0xFF),
        '\0',
    };
    return std::string(text);
}
} // namespace

void Record::CopyRecordData(Record& aRhs)
{
    m_formType = aRhs.m_formType;
    m_dataSize = aRhs.m_dataSize;
    m_flags = aRhs.m_flags;
    m_formId = aRhs.m_formId;
    m_versionControl = aRhs.m_versionControl;
    m_formVersion = aRhs.m_formVersion;
    m_vcVersion = aRhs.m_vcVersion;
}

void Record::SetBaseId(uint32_t aBaseId)
{
    const uint32_t mask = ((aBaseId & 0xFF000000u) == 0xFE000000u) ? 0xFFFu : 0x00FFFFFFu;
    m_formId &= mask;
    m_formId += aBaseId;
}

void Record::IterateChunks(const std::function<void(ChunkId, Buffer::Reader&)>& aCallback)
{
    Buffer buffer(reinterpret_cast<uint8_t*>(this) + sizeof(Record), m_dataSize);
    Buffer::Reader reader(&buffer);

    Buffer pDecompressed;
    if (Compressed())
    {
        if (m_dataSize < 4)
        {
            spdlog::error("Record {} form {:08X} has invalid compressed size {}", FourCC(static_cast<uint32_t>(m_formType)), m_formId, m_dataSize);
            return;
        }

        uint32_t size = 0;
        reader.ReadBytes(reinterpret_cast<uint8_t*>(&size), 4);
        const size_t maxSize = GetMaxDecompressedSize();
        if (size == 0 || size > maxSize)
        {
            spdlog::error("Record {} form {:08X} requested {} bytes (limit {})", FourCC(static_cast<uint32_t>(m_formType)), m_formId, size, maxSize);
            return;
        }
        pDecompressed.Resize(size);
        const uint32_t fieldSize = m_dataSize - 4;

        DecompressChunkData(reader.GetDataAtPosition(), fieldSize, pDecompressed.GetWriteData(), pDecompressed.GetSize());

        reader = Buffer::Reader(&pDecompressed);
    }

    uint32_t largeDataSize = 0;
    size_t chunkCount = 0;
    const size_t readerLimit = Compressed() ? pDecompressed.GetSize() : m_dataSize;

    while (!reader.Eof())
    {
        const size_t chunkHeaderPos = reader.GetBytePosition();
        if (chunkHeaderPos + sizeof(Record::Chunk) > readerLimit)
        {
            spdlog::error("Record {} form {:08X} chunk header overflows buffer at {}", FourCC(static_cast<uint32_t>(m_formType)), m_formId, chunkHeaderPos);
            break;
        }
        Record::Chunk* pChunk = reinterpret_cast<Chunk*>(reader.GetDataAtPosition());
        reader.Advance(sizeof(Record::Chunk));

        uint32_t dataSize = pChunk->m_dataSize;
        if (pChunk->m_dataSize == 0)
        {
            dataSize = largeDataSize;
        }
        if (dataSize == 0)
        {
            break;
        }

        // Chunk XXXX will have the next data size stored at the start of the field
        // Doesn't ever seem to trigger, but it's in the spec so might as well leave it in
        // https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format#Fields
        if (pChunk->m_chunkId == ChunkId::XXXX_ID)
        {
            if (reader.GetBytePosition() + 4 > readerLimit)
            {
                spdlog::error("Record {} form {:08X} XXXX chunk overflows buffer at {}", FourCC(static_cast<uint32_t>(m_formType)), m_formId, reader.GetBytePosition());
                break;
            }
            reader.ReadBytes(reinterpret_cast<uint8_t*>(&largeDataSize), 4);
            reader.Reverse(4);
        }

        const size_t chunkDataPos = reader.GetBytePosition();
        const size_t remaining = (chunkDataPos <= readerLimit) ? (readerLimit - chunkDataPos) : 0;
        if (dataSize > remaining)
        {
            spdlog::error("Record {} form {:08X} chunk {} size {} exceeds remaining {}", FourCC(static_cast<uint32_t>(m_formType)),
                          m_formId, FourCC(static_cast<uint32_t>(pChunk->m_chunkId)), dataSize, remaining);
            break;
        }

        Buffer::Reader chunk(reader);

        reader.Advance(dataSize);

        aCallback(pChunk->m_chunkId, chunk);

        if (++chunkCount > 1000000)
        {
            spdlog::error("Record {} form {:08X} exceeded max chunk count", FourCC(static_cast<uint32_t>(m_formType)), m_formId);
            break;
        }
    }
}

void Record::DecompressChunkData(const void* apCompressedData, size_t aCompressedSize, void* apDecompressedData, size_t aDecompressedSize)
{
    z_stream compressionStream;
    compressionStream.next_in = (Bytef*)apCompressedData;
    compressionStream.avail_in = (uInt)(aCompressedSize);
    compressionStream.next_out = (Bytef*)apDecompressedData;
    compressionStream.avail_out = (uInt)aDecompressedSize;
    compressionStream.zalloc = Z_NULL;
    compressionStream.zfree = Z_NULL;
    compressionStream.opaque = Z_NULL;

    inflateInit(&compressionStream);
    int res = inflate(&compressionStream, Z_NO_FLUSH);
    if (res < Z_OK)
        spdlog::error("Failed to decompress chunk of data (inflate): {}.", res);
    res = inflateEnd(&compressionStream);
    if (res < Z_OK)
        spdlog::error("Failed to decompress chunk of data: {}", res);
}

void Record::DiscoverChunks()
{
    IterateChunks(
        [&](ChunkId aChunkId, Buffer::Reader& aReader)
        {
            switch (aChunkId)
            {
                /*
            case ChunkId::XMRK_ID:
                spdlog::info("XMRK found in form {:X}", m_formId);
                break;
                */
            }
        });
}
