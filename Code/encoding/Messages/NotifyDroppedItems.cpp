#include <Messages/NotifyDroppedItems.h>
#include <TiltedCore/Serialization.hpp>

void NotifyDroppedItems::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, RequestId);
    Serialization::WriteVarInt(aWriter, Entries.size());
    for (const auto& entry : Entries)
    {
        Serialization::WriteVarInt(aWriter, entry.DropId);
        Serialization::WriteVarInt(aWriter, entry.ServerId);
        Serialization::WriteVarInt(aWriter, entry.ActorFormId);
        Serialization::WriteVarInt(aWriter, static_cast<uint8_t>(entry.Type));
        Serialization::WriteVarInt(aWriter, entry.SpawnEpoch);
        entry.Item.Serialize(aWriter);

        Serialization::WriteBool(aWriter, entry.HasLocation);
        if (entry.HasLocation)
            entry.Location.Serialize(aWriter);

        Serialization::WriteBool(aWriter, entry.HasRotation);
        if (entry.HasRotation)
            entry.Rotation.Serialize(aWriter);

        entry.CellId.Serialize(aWriter);
        entry.WorldSpaceId.Serialize(aWriter);
        entry.ReferenceId.Serialize(aWriter);
    }

    Serialization::WriteVarInt(aWriter, CreationEnginePickedUpReferences.size());
    for (const auto& referenceId : CreationEnginePickedUpReferences)
        referenceId.Serialize(aWriter);
}

void NotifyDroppedItems::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    RequestId = Serialization::ReadVarInt(aReader);
    const auto count = Serialization::ReadVarInt(aReader);
    Entries.clear();
    Entries.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        Entry entry{};
        entry.DropId = Serialization::ReadVarInt(aReader);
        entry.ServerId = Serialization::ReadVarInt(aReader);
        entry.ActorFormId = Serialization::ReadVarInt(aReader);
        const auto typeValue = Serialization::ReadVarInt(aReader);
        entry.Type = typeValue == static_cast<uint64_t>(ServerItemType::CreationEngine) ? ServerItemType::CreationEngine : ServerItemType::Dropped;
        entry.SpawnEpoch = Serialization::ReadVarInt(aReader);
        entry.Item.Deserialize(aReader);

        entry.HasLocation = Serialization::ReadBool(aReader);
        if (entry.HasLocation)
            entry.Location.Deserialize(aReader);

        entry.HasRotation = Serialization::ReadBool(aReader);
        if (entry.HasRotation)
            entry.Rotation.Deserialize(aReader);

        entry.CellId.Deserialize(aReader);
        entry.WorldSpaceId.Deserialize(aReader);
        entry.ReferenceId.Deserialize(aReader);

        Entries.push_back(std::move(entry));
    }

    const auto pickupCount = Serialization::ReadVarInt(aReader);
    CreationEnginePickedUpReferences.clear();
    CreationEnginePickedUpReferences.reserve(pickupCount);
    for (size_t i = 0; i < pickupCount; ++i)
    {
        GameId referenceId{};
        referenceId.Deserialize(aReader);
        CreationEnginePickedUpReferences.push_back(std::move(referenceId));
    }
}
