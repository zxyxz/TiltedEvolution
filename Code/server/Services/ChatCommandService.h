#pragma once

#include <ChatMessageTypes.h>
#include <Messages/NotifyCommandList.h>
#include <TiltedCore/Stl.hpp>

#include <entt/fwd.hpp>

#include <functional>
#include <string_view>
#include <unordered_map>

struct World;
struct Player;

struct ChatCommandContext
{
    TiltedPhoques::String Raw;
    TiltedPhoques::String NameLower;
    TiltedPhoques::Vector<TiltedPhoques::String> Args;
};

class ChatCommand
{
public:
    virtual ~ChatCommand() = default;

    virtual const char* GetName() const noexcept = 0;
    virtual const char* GetDescription() const noexcept = 0;
    virtual bool RequiresAdmin() const noexcept { return false; }
    virtual bool Execute(World& world, Player* player, const ChatCommandContext& context) const = 0;
};

class ChatCommandService
{
public:
    ChatCommandService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~ChatCommandService() noexcept = default;

    TP_NOCOPYMOVE(ChatCommandService);

    bool TryHandle(Player* player, const TiltedPhoques::String& message) const;
    void SendCommandList(Player* player) const noexcept;
    TiltedPhoques::Vector<NotifyCommandList::CommandEntry> BuildCommandList(Player* player) const;

    void SendSystemMessage(Player* player, std::string_view message) const noexcept;
    void BroadcastSystemMessage(std::string_view message) const noexcept;
    void SendChatMessage(ChatMessageType type, const TiltedPhoques::String& content, Player* sender) const noexcept;

private:
    bool ParseCommand(const TiltedPhoques::String& raw, ChatCommandContext& out) const;
    const ChatCommand* FindCommand(std::string_view nameLower) const;
    bool IsAdmin(Player* player) const;

    void RegisterCommand(TiltedPhoques::UniquePtr<ChatCommand> command);

private:
    World& m_world;
    TiltedPhoques::Vector<TiltedPhoques::UniquePtr<ChatCommand>> m_commands;
    std::unordered_map<TiltedPhoques::String, const ChatCommand*> m_commandLookup;
};
