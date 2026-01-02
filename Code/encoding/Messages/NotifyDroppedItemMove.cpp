#include <Messages/NotifyDroppedItemMove.h>

void NotifyDroppedItemMove::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, DropId);
    Serialization::WriteBool(aWriter, HasLocation);
    if (HasLocation)
        Location.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HasRotation);
    if (HasRotation)
        Rotation.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HasVelocity);
    if (HasVelocity)
        Velocity.Serialize(aWriter);
    Serialization::WriteBool(aWriter, HasAngularVelocity);
    if (HasAngularVelocity)
        AngularVelocity.Serialize(aWriter);
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    ReferenceId.Serialize(aWriter);
}

void NotifyDroppedItemMove::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    DropId = Serialization::ReadVarInt(aReader);
    HasLocation = Serialization::ReadBool(aReader);
    if (HasLocation)
        Location.Deserialize(aReader);
    HasRotation = Serialization::ReadBool(aReader);
    if (HasRotation)
        Rotation.Deserialize(aReader);
    HasVelocity = Serialization::ReadBool(aReader);
    if (HasVelocity)
        Velocity.Deserialize(aReader);
    HasAngularVelocity = Serialization::ReadBool(aReader);
    if (HasAngularVelocity)
        AngularVelocity.Deserialize(aReader);
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    ReferenceId.Deserialize(aReader);
}
