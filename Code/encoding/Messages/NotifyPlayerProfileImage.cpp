#include <Messages/NotifyPlayerProfileImage.h>

#include <TiltedCore/Serialization.hpp>

void NotifyPlayerProfileImage::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteString(aWriter, Avatar);
}

void NotifyPlayerProfileImage::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Avatar = Serialization::ReadString(aReader);
}

