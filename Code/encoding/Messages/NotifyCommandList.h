#pragma once

#include "Message.h"

using TiltedPhoques::String;

struct NotifyCommandList final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyCommandList;

    NotifyCommandList()
        : ServerMessage(Opcode)
    {
    }

    virtual ~NotifyCommandList() = default;

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    struct CommandEntry
    {
        String Name{};
        String Description{};

        bool operator==(const CommandEntry& aRhs) const noexcept
        {
            return Name == aRhs.Name && Description == aRhs.Description;
        }
    };

    bool operator==(const NotifyCommandList& acRhs) const noexcept { return Commands == acRhs.Commands && GetOpcode() == acRhs.GetOpcode(); }

    TiltedPhoques::Vector<CommandEntry> Commands{};
};
