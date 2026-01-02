#pragma once

#include "Message.h"
#include <Structs/Vector3_NetQuantize.h>
#include <Structs/GameId.h>

struct NotifyPartyPositions final : ServerMessage {
  static constexpr ServerOpcode Opcode = kNotifyPartyPositions;

  NotifyPartyPositions() : ServerMessage(Opcode) {}

  struct Entry {
    uint32_t PlayerId{};
    Vector3_NetQuantize Position{};
    GameId WorldSpaceId{};
    GameId CellId{};
    bool IsInterior{false};

    bool operator==(const Entry& rhs) const noexcept {
      return PlayerId == rhs.PlayerId && Position == rhs.Position && WorldSpaceId == rhs.WorldSpaceId &&
             CellId == rhs.CellId && IsInterior == rhs.IsInterior;
    }
  };

  TiltedPhoques::Vector<Entry> Entries;

  void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
  void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;
};
