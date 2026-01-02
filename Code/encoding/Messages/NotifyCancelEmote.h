#pragma once

#include "Message.h"

struct NotifyCancelEmote final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCancelEmote;

    NotifyCancelEmote()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyCancelEmote& acRhs) const noexcept { return ServerId == acRhs.ServerId && GetOpcode() == acRhs.GetOpcode(); }

    uint32_t ServerId{};
};
