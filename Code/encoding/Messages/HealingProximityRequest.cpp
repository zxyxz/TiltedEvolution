#include <Messages/HealingProximityRequest.h>

void HealingProximityRequest::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, CasterId);
    Serialization::WriteFloat(aWriter, CasterX);
    Serialization::WriteFloat(aWriter, CasterY);
    Serialization::WriteFloat(aWriter, CasterZ);
    Serialization::WriteVarInt(aWriter, SpellFormId.ModId);
    Serialization::WriteVarInt(aWriter, SpellFormId.BaseId);
    Serialization::WriteVarInt(aWriter, CasterRestorationLevel);
}

void HealingProximityRequest::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    CasterId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
    CasterX = Serialization::ReadFloat(aReader);
    CasterY = Serialization::ReadFloat(aReader);
    CasterZ = Serialization::ReadFloat(aReader);
    SpellFormId.ModId = Serialization::ReadVarInt(aReader) & 0xFF;
    SpellFormId.BaseId = Serialization::ReadVarInt(aReader) & 0xFFFFFF;
    CasterRestorationLevel = Serialization::ReadVarInt(aReader) & 0xFFFF;
}
