#include <Messages/NotifyPlayerSyncMode.h>

void NotifyPlayerSyncMode::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    aWriter.WriteBits(PlayerId, 32);
    aWriter.WriteBits(static_cast<uint8_t>(Mode), 8);
}

void NotifyPlayerSyncMode::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    uint64_t playerId = 0;
    aReader.ReadBits(playerId, 32);
    PlayerId = static_cast<uint32_t>(playerId & 0xFFFFFFFF);

    uint64_t mode = 0;
    aReader.ReadBits(mode, 8);
    Mode = static_cast<SyncMode>(mode & 0xFF);
}
