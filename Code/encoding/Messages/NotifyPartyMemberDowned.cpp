#include <Messages/NotifyPartyMemberDowned.h>

void NotifyPartyMemberDowned::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, ServerId);
    Serialization::WriteVarInt(aWriter, PlayerId);
    Serialization::WriteBool(aWriter, IsDowned);
    Serialization::WriteFloat(aWriter, PositionX);
    Serialization::WriteFloat(aWriter, PositionY);
    Serialization::WriteFloat(aWriter, PositionZ);
    Serialization::WriteVarInt(aWriter, WorldSpaceId.ModId);
    Serialization::WriteVarInt(aWriter, WorldSpaceId.BaseId);
    Serialization::WriteVarInt(aWriter, CellId.ModId);
    Serialization::WriteVarInt(aWriter, CellId.BaseId);
}

void NotifyPartyMemberDowned::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    ServerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    IsDowned = Serialization::ReadBool(aReader);
    PositionX = Serialization::ReadFloat(aReader);
    PositionY = Serialization::ReadFloat(aReader);
    PositionZ = Serialization::ReadFloat(aReader);
    WorldSpaceId.ModId = Serialization::ReadVarInt(aReader) & 0xFF;
    WorldSpaceId.BaseId = Serialization::ReadVarInt(aReader) & 0xFFFFFF;
    CellId.ModId = Serialization::ReadVarInt(aReader) & 0xFF;
    CellId.BaseId = Serialization::ReadVarInt(aReader) & 0xFFFFFF;
}
