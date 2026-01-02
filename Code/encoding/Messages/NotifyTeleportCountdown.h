#pragma once

#include "Message.h"

struct NotifyTeleportCountdown final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyTeleportCountdown;

    NotifyTeleportCountdown()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyTeleportCountdown() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyTeleportCountdown& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && TargetPlayerId == acRhs.TargetPlayerId && TargetName == acRhs.TargetName && DurationSeconds == acRhs.DurationSeconds && Cancelled == acRhs.Cancelled && Reason == acRhs.Reason;
    }

    uint16_t TargetPlayerId{};
    TiltedPhoques::String TargetName{};
    uint16_t DurationSeconds{};
    bool Cancelled{};
    TiltedPhoques::String Reason{};
};
