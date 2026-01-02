#pragma once

// spdlog
#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <chrono>

#include <BuildInfo.h>
#include <uv.h>
#include <console/ConsoleRegistry.h>
#include <console/IniSettingsProvider.h>
#include <common/GameServerInstance.h>
#include <common/ServerStatusSnapshot.h>

#ifdef _WIN32
#define GS_IMPORT extern __declspec(dllimport)
#else
#define GS_IMPORT extern __attribute__((visibility("default")))
#endif

namespace fs = std::filesystem;

static constexpr char kConfigPathName[] = "config";
static constexpr char KCompilerStopThisBullshit[] = "ConOut";
static constexpr char kBuildTag[]{BUILD_BRANCH "@" BUILD_COMMIT};

// Frontend class for the dedicated terminal based server
struct DediRunner
{
    DediRunner(int argc, char** argv);
    ~DediRunner();

    void RunGSThread();
    void StartTerminalIO();
    void RequestKill();
    void HandleConsole(const TiltedPhoques::String& acCommand);
    void GetStatus(ServerStatusSnapshot& aOutStatus) const;
    struct SettingSnapshot
    {
        TiltedPhoques::String Name;
        TiltedPhoques::String Description;
        Console::SettingBase::Type Type;
        Console::SettingsFlags Flags;
        TiltedPhoques::String Value;
    };
    void GetSettingsSnapshot(TiltedPhoques::Vector<SettingSnapshot>& aOut);
    bool GetSettingValue(const TiltedPhoques::String& aName, TiltedPhoques::String& aOut);
    bool SetSettingValue(const TiltedPhoques::String& aName, const TiltedPhoques::String& aValue, TiltedPhoques::String* apError = nullptr);
    void QueueConsoleCommand(const TiltedPhoques::String& acCommand);
    bool IsRunning() const noexcept;
    bool IsListening() const noexcept;
    uint64_t GetTickCounter() const noexcept { return m_tickCounter.load(); }
    uint32_t GetUptimeSeconds() const noexcept;

private:
    static void PrintExecutorArrowHack();

    void LoadSettings(int argc, char** argv);

    void ProcessQueuedCommands();

    static void ReadStdin(uv_stream_t* apStream, ssize_t aRead, const uv_buf_t* acpBuffer);
    static void AllocateBuffer(uv_handle_t* apHandle, size_t aSuggestedSize, uv_buf_t* apBuffer);

private:
    // fs::path m_configPath;
    //  Order here matters for constructor calling order.
    uv_loop_t m_loop;
    uv_tty_t m_tty;
    fs::path m_SettingsPath;
    bool m_useIni{false};
    Console::ConsoleRegistry m_console;
    TiltedPhoques::UniquePtr<IGameServerInstance> m_pServerInstance;
    std::atomic<bool> m_consoleRunning{false};
    std::atomic<uint64_t> m_tickCounter{0};
    std::chrono::steady_clock::time_point m_startTime;
    std::mutex m_consoleMutex;
    std::queue<TiltedPhoques::String> m_consoleQueue;
    std::thread m_consoleThread;
};

DediRunner* GetDediRunner() noexcept;
