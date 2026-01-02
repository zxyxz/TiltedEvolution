#include <Messages/NotifyPartyPositions.h>

void NotifyPartyPositions::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept {
  // Opcode is written by ServerMessage::SerializeRaw
  aWriter.WriteBits(static_cast<uint64_t>(Entries.size()), 16);
  for (const auto& e : Entries) {
    aWriter.WriteBits(static_cast<uint64_t>(e.PlayerId), 32);
    e.Position.Serialize(aWriter);
    e.WorldSpaceId.Serialize(aWriter);
    e.CellId.Serialize(aWriter);
    aWriter.WriteBits(e.IsInterior ? 1u : 0u, 1);
  }
}

void NotifyPartyPositions::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept {
  ServerMessage::DeserializeRaw(aReader);

  uint64_t count = 0;
  aReader.ReadBits(count, 16);
  Entries.clear();
  Entries.reserve(count);
  for (uint64_t i = 0; i < count; ++i) {
    Entry e{};
    uint64_t pid = 0;
    aReader.ReadBits(pid, 32);
    e.PlayerId = static_cast<uint32_t>(pid);
    e.Position.Deserialize(aReader);
    e.WorldSpaceId.Deserialize(aReader);
    e.CellId.Deserialize(aReader);
    uint64_t interior = 0;
    aReader.ReadBits(interior, 1);
    e.IsInterior = interior != 0;
    Entries.push_back(e);
  }
}
