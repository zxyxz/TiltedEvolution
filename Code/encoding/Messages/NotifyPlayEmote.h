#pragma once

#include "Message.h"

using TiltedPhoques::String;

struct NotifyPlayEmote final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlayEmote;

    NotifyPlayEmote()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayEmote& acRhs) const noexcept { return ServerId == acRhs.ServerId && EventName == acRhs.EventName && GetOpcode() == acRhs.GetOpcode(); }

    uint32_t ServerId{};
    String EventName{};
};
