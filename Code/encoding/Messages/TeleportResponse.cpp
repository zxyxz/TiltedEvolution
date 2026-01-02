#include <Messages/TeleportResponse.h>

#include <TiltedCore/Serialization.hpp>

void TeleportResponse::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RequesterId);
    Serialization::WriteBool(aWriter, Accepted);
}

void TeleportResponse::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequesterId = static_cast<uint16_t>(Serialization::ReadVarInt(aReader) & 0xFFFF);
    Accepted = Serialization::ReadBool(aReader);
}
