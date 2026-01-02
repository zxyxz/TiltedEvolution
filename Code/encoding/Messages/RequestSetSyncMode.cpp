#include <Messages/RequestSetSyncMode.h>

void RequestSetSyncMode::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(static_cast<uint8_t>(Mode), 8);
}

void RequestSetSyncMode::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    uint64_t mode = 0;
    aReader.ReadBits(mode, 8);
    Mode = static_cast<SyncMode>(mode & 0xFF);
}
