#pragma once

#include "Message.h"

struct NotifyTeleportRequest final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyTeleportRequest;

    NotifyTeleportRequest()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyTeleportRequest() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyTeleportRequest& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && RequesterId == acRhs.RequesterId && RequesterName == acRhs.RequesterName;
    }

    uint16_t RequesterId{};
    TiltedPhoques::String RequesterName{};
};
