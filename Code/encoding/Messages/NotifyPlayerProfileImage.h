#pragma once

#include "Message.h"

struct NotifyPlayerProfileImage final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlayerProfileImage;

    NotifyPlayerProfileImage()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPlayerProfileImage& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && PlayerId == acRhs.PlayerId && Avatar == acRhs.Avatar;
    }

    uint32_t PlayerId{};
    String Avatar{};
};

