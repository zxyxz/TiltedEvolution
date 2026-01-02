#include <Messages/NotifyTeleportRequest.h>

#include <TiltedCore/Serialization.hpp>

void NotifyTeleportRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RequesterId);
    Serialization::WriteString(aWriter, RequesterName);
}

void NotifyTeleportRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    RequesterId = static_cast<uint16_t>(Serialization::ReadVarInt(aReader) & 0xFFFF);
    RequesterName = Serialization::ReadString(aReader);
}
