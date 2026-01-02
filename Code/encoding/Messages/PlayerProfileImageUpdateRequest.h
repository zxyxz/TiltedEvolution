#pragma once

#include "Message.h"

struct PlayerProfileImageUpdateRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kPlayerProfileImageUpdateRequest;

    PlayerProfileImageUpdateRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerProfileImageUpdateRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && ImageData == acRhs.ImageData; }

    String ImageData{};
};

