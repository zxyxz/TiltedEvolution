#include <Messages/NotifyDroppedItemPickedUp.h>
#include <TiltedCore/Serialization.hpp>

void NotifyDroppedItemPickedUp::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    // Base fields
    Serialization::WriteVarInt(aWriter, ServerId);
    Item.Serialize(aWriter);
    Serialization::WriteVarInt(aWriter, DropId);

    // Optional location information
    Serialization::WriteBool(aWriter, HasLocation);
    if (HasLocation)
        Location.Serialize(aWriter);

    // Optional rotation information
    Serialization::WriteBool(aWriter, HasRotation);
    if (HasRotation)
        Rotation.Serialize(aWriter);

    // Cell and world‑space identifiers (always present)
    CellId.Serialize(aWriter);
    WorldSpaceId.Serialize(aWriter);
    ReferenceId.Serialize(aWriter);
}

void NotifyDroppedItemPickedUp::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    // Base fields
    ServerMessage::DeserializeRaw(aReader);
    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    Item.Deserialize(aReader);
    DropId = Serialization::ReadVarInt(aReader);

    // Optional location information
    HasLocation = Serialization::ReadBool(aReader);
    if (HasLocation)
        Location.Deserialize(aReader);

    // Optional rotation information
    HasRotation = Serialization::ReadBool(aReader);
    if (HasRotation)
        Rotation.Deserialize(aReader);

    // Cell and world‑space identifiers
    CellId.Deserialize(aReader);
    WorldSpaceId.Deserialize(aReader);
    ReferenceId.Deserialize(aReader);
}
