#include <Messages/NotifyCommandList.h>
#include <TiltedCore/Serialization.hpp>

void NotifyCommandList::SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    Serialization::WriteVarInt(aWriter, Commands.size());

    for (const auto& command : Commands)
    {
        Serialization::WriteString(aWriter, command.Name);
        Serialization::WriteString(aWriter, command.Description);
    }
}

void NotifyCommandList::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ServerMessage::DeserializeRaw(aReader);

    const auto count = Serialization::ReadVarInt(aReader) & 0xFFFF;
    Commands.clear();
    Commands.reserve(count);

    for (auto i = 0u; i < count; ++i)
    {
        CommandEntry entry{};
        entry.Name = Serialization::ReadString(aReader);
        entry.Description = Serialization::ReadString(aReader);
        Commands.push_back(std::move(entry));
    }
}
