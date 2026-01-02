#include <Messages/RequestPickupDroppedItem.h>
#include <TiltedCore/Serialization.hpp>

void RequestPickupDroppedItem::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, DropId);
    Item.Serialize(aWriter);

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

void RequestPickupDroppedItem::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    DropId = Serialization::ReadVarInt(aReader);
    Item.Deserialize(aReader);

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
