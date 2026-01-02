#include <Messages/NotifyActorDrop.h>
#include <TiltedCore/Serialization.hpp>

void NotifyActorDrop::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, ActorFormId);
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, DropId);
    Serialization::WriteVarInt(aWriter, SpawnEpoch);
    Serialization::WriteBool(aWriter, HasClientDropId);
    if (HasClientDropId)
        ClientDropId.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HasLocation);
    if (HasLocation)
        Location.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HasRotation);
    if (HasRotation)
        Rotation.Serialize(aWriter);

    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    ReferenceId.Serialize(aWriter);
}

void NotifyActorDrop::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    ActorFormId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Item.Deserialize(aReader);
    DropId = Serialization::ReadVarInt(aReader);
    SpawnEpoch = Serialization::ReadVarInt(aReader);
    HasClientDropId = Serialization::ReadBool(aReader);
    if (HasClientDropId)
        ClientDropId.Deserialize(aReader);
    HasLocation = Serialization::ReadBool(aReader);
    if (HasLocation)
        Location.Deserialize(aReader);
    HasRotation = Serialization::ReadBool(aReader);
    if (HasRotation)
        Rotation.Deserialize(aReader);

    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    ReferenceId.Deserialize(aReader);
}
