#include <Messages/PartyPositionUpdateRequest.h>
#include <TiltedCore/Serialization.hpp>

void PartyPositionUpdateRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Position.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    CellId.Serialize(aWriter);
}

void PartyPositionUpdateRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Position.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    CellId.Deserialize(aReader);
}

