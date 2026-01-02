#pragma once

#include "Message.h"

struct PartyMemberDownedRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPartyMemberDownedRequest;

    PartyMemberDownedRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PartyMemberDownedRequest& acRhs) const noexcept { return IsDowned == acRhs.IsDowned && GetOpcode() == acRhs.GetOpcode(); }

    bool IsDowned{false};
};
