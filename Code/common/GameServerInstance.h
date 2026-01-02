#pragma once

#include <console/ConsoleRegistry.h>
#include <common/ServerStatusSnapshot.h>

class IGameServerInstance
{
public:
    virtual ~IGameServerInstance() = default;

    // lifetime control
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;

    virtual bool IsListening() = 0;
    virtual bool IsRunning() = 0;

    // update the server logic
    virtual void Update() = 0;
    virtual Console::ConsoleRegistry::ExecutionResult ExecuteConsoleCommand(const TiltedPhoques::String& aCommand) = 0;

    virtual void GetStatus(ServerStatusSnapshot& aOutStatus) const = 0;
};
