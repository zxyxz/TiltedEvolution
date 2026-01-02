#include <Messages/PlayerProfileImageUpdateRequest.h>

#include <TiltedCore/Serialization.hpp>

void PlayerProfileImageUpdateRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteString(aWriter, ImageData);
}

void PlayerProfileImageUpdateRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    ImageData = Serialization::ReadString(aReader);
}

