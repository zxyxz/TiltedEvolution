
#include "GameServer.h"

#include <common/GameServerInstance.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#ifdef _WIN32
#define GS_EXPORT __declspec(dllexport)
#else
#define GS_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
constexpr char kBuildTag[]{BUILD_BRANCH "@" BUILD_COMMIT};

using UiLogCallback = void (*)(const char*);
UiLogCallback s_uiLogCallback = nullptr;

class UiCallbackSink : public spdlog::sinks::base_sink<std::mutex>
{
public:
    explicit UiCallbackSink(UiLogCallback aCallback)
        : m_callback(aCallback)
    {
        set_pattern("[%l] %v");
        set_level(spdlog::level::trace);
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if (!m_callback)
            return;

        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);

        m_callback(fmt::to_string(formatted).c_str());
    }

    void flush_() override {}

private:
    UiLogCallback m_callback;
};

spdlog::sink_ptr s_uiSink;
} // namespace

struct GameServerInstance final : IGameServerInstance
{
    GameServerInstance(Console::ConsoleRegistry& aConsole)
        : m_gameServer(aConsole)
    {
    }

    // to make sure our dtor is called.
    ~GameServerInstance() override = default;

    // Inherited via IGameServerInstance
    bool Initialize() override;
    void Shutdown() override;
    bool IsListening() override;
    bool IsRunning() override;
    void Update() override;
    Console::ConsoleRegistry::ExecutionResult ExecuteConsoleCommand(const TiltedPhoques::String& aCommand) override;
    void GetStatus(ServerStatusSnapshot& aOutStatus) const override;

private:
    GameServer m_gameServer;
};

bool GameServerInstance::Initialize()
{
    m_gameServer.Initialize();
    return true;
}

void GameServerInstance::Shutdown()
{
    m_gameServer.Kill();
}

bool GameServerInstance::IsListening()
{
    return m_gameServer.IsListening();
}

bool GameServerInstance::IsRunning()
{
    return m_gameServer.IsRunning();
}

void GameServerInstance::Update()
{
    m_gameServer.Update();
}

Console::ConsoleRegistry::ExecutionResult GameServerInstance::ExecuteConsoleCommand(const TiltedPhoques::String& aCommand)
{
    return m_gameServer.ExecuteConsoleCommand(aCommand);
}

void GameServerInstance::GetStatus(ServerStatusSnapshot& aOutStatus) const
{
    m_gameServer.GetStatusSnapshot(aOutStatus);
}


// NOTE(Vince): For now we use this to compare the dll to the server.
GS_EXPORT const char* GetBuildTag()
{
    return kBuildTag;
}

GS_EXPORT bool CheckBuildTag(const char* apBuildTag)
{
    return std::strcmp(apBuildTag, kBuildTag) == 0;
}

GS_EXPORT UniquePtr<IGameServerInstance> CreateGameServer(Console::ConsoleRegistry& aConReg, const std::function<void()>& aCallback)
{
    BASE_ASSERT(aCallback, "CreateGameServer(): Callback was not provided");

    // register static variables before they become available to the server
    aConReg.BindStaticItems();

    // this is a special callback to notify the runner once all settings become available
    aCallback();

    return TiltedPhoques::CastUnique<IGameServerInstance>(TiltedPhoques::MakeUnique<GameServerInstance>(aConReg));
}

// cxx symbol

// These two are windows specific implementation details, since all symbols & insances are private there, so we must hand over
// the control.
// if not compiling with -fvisibility=hidden, hide these

GS_EXPORT void SetDefaultLogger(std::shared_ptr<spdlog::logger> aLogger)
{
    // #ifdef _WIN32
    spdlog::set_default_logger(std::move(aLogger));
    // #endif
}

GS_EXPORT void RegisterLogger(std::shared_ptr<spdlog::logger> aLogger)
{
    // #ifdef _WIN32
    //  yes this needs to be here, else the dedirunner dies
    spdlog::register_logger(std::move(aLogger));
    // #endif
}

GS_EXPORT void SetUiLogCallback(void (*aCallback)(const char*))
{
    s_uiLogCallback = aCallback;
    if (!s_uiLogCallback)
        return;

    if (!s_uiSink)
        s_uiSink = std::static_pointer_cast<spdlog::sinks::sink>(std::make_shared<UiCallbackSink>(s_uiLogCallback));

    spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& logger)
    {
        if (!logger)
            return;
        logger->flush_on(logger->level());
        logger->sinks().push_back(s_uiSink);
    });
}

#ifdef _WIN32
// Before you think about moving logic in here...
// There are significant limits on what you can safely do in a DLL entry point. See General Best Practices for specific
// Windows APIs that are unsafe to call in DllMain. If you need anything but the simplest initialization then do that in
// an initialization function for the DLL. You can require applications to call the initialization function after
// DllMain has run and before they call any other functions in the DLL.
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH: break;
    case DLL_PROCESS_DETACH: break;
    }
    return TRUE;
}
#endif
