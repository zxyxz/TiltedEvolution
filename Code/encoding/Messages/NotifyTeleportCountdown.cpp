#include <Messages/NotifyTeleportCountdown.h>

#include <TiltedCore/Serialization.hpp>

void NotifyTeleportCountdown::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, TargetPlayerId);
    Serialization::WriteString(aWriter, TargetName);
    Serialization::WriteVarInt(aWriter, DurationSeconds);
    Serialization::WriteBool(aWriter, Cancelled);
    Serialization::WriteString(aWriter, Reason);
}

void NotifyTeleportCountdown::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    TargetPlayerId = static_cast<uint16_t>(Serialization::ReadVarInt(aReader) & 0xFFFF);
    TargetName = Serialization::ReadString(aReader);
    DurationSeconds = static_cast<uint16_t>(Serialization::ReadVarInt(aReader) & 0xFFFF);
    Cancelled = Serialization::ReadBool(aReader);
    Reason = Serialization::ReadString(aReader);
}
