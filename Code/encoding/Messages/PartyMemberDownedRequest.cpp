#include <Messages/PartyMemberDownedRequest.h>

#include <TiltedCore/Serialization.hpp>

void PartyMemberDownedRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteBool(aWriter, IsDowned);
}

void PartyMemberDownedRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);
    IsDowned = Serialization::ReadBool(aReader);
}
