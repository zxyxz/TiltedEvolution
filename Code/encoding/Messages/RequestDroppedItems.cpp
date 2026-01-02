#include <Messages/RequestDroppedItems.h>
#include <TiltedCore/Serialization.hpp>

void RequestDroppedItems::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteBool(aWriter, RequestAll);
    Serialization::WriteBool(aWriter, HasCellFilter);
    if (HasCellFilter)
        CellId.Serialize(aWriter);

    Serialization::WriteBool(aWriter, HasWorldSpaceFilter);
    if (HasWorldSpaceFilter)
        WorldSpaceId.Serialize(aWriter);

    Serialization::WriteVarInt(aWriter, Discoveries.size());
    for (const auto& entry : Discoveries)
    {
        entry.ReferenceId.Serialize(aWriter);
        entry.CellId.Serialize(aWriter);
        entry.WorldSpaceId.Serialize(aWriter);
        entry.Item.Serialize(aWriter);

        Serialization::WriteBool(aWriter, entry.HasLocation);
        if (entry.HasLocation)
            entry.Location.Serialize(aWriter);

        Serialization::WriteBool(aWriter, entry.HasRotation);
        if (entry.HasRotation)
            entry.Rotation.Serialize(aWriter);
    }
}

void RequestDroppedItems::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    RequestId = Serialization::ReadVarInt(aReader);
    RequestAll = Serialization::ReadBool(aReader);
    HasCellFilter = Serialization::ReadBool(aReader);
    if (HasCellFilter)
        CellId.Deserialize(aReader);

    HasWorldSpaceFilter = Serialization::ReadBool(aReader);
    if (HasWorldSpaceFilter)
        WorldSpaceId.Deserialize(aReader);

    const auto discoveryCount = Serialization::ReadVarInt(aReader);
    Discoveries.clear();
    Discoveries.reserve(discoveryCount);
    for (size_t i = 0; i < discoveryCount; ++i)
    {
        DiscoveryEntry entry{};
        entry.ReferenceId.Deserialize(aReader);
        entry.CellId.Deserialize(aReader);
        entry.WorldSpaceId.Deserialize(aReader);
        entry.Item.Deserialize(aReader);

        entry.HasLocation = Serialization::ReadBool(aReader);
        if (entry.HasLocation)
            entry.Location.Deserialize(aReader);

        entry.HasRotation = Serialization::ReadBool(aReader);
        if (entry.HasRotation)
            entry.Rotation.Deserialize(aReader);

        Discoveries.push_back(std::move(entry));
    }
}
