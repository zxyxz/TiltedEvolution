#pragma once

#include "Message.h"

using TiltedPhoques::String;

struct NotifyPlayerList final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPlayerList;

    NotifyPlayerList()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyPlayerList() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    struct PlayerListEntry
    {
        String Name{};
        String Avatar{};

        bool operator==(const PlayerListEntry& aRhs) const noexcept
        {
            return Name == aRhs.Name && Avatar == aRhs.Avatar;
        }
    };

    bool operator==(const NotifyPlayerList& acRhs) const noexcept { return Players == acRhs.Players && GetOpcode() == acRhs.GetOpcode(); }

    TiltedPhoques::Map<uint32_t, PlayerListEntry> Players{};
};
