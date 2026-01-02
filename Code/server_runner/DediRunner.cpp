
#include "DediRunner.h"

#include <console/CommandSettingsProvider.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <spdlog/spdlog.h>
#include <base/threading/ThreadUtils.h>
#include <console/ConsoleUtils.h>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
constexpr char kSettingsFileName[] = "STServer.ini";

DediRunner* s_pRunner{nullptr};
} // namespace

// needs to be global
Console::Setting bConsole{"bConsole", "Enable the console", true};

// imports
GS_IMPORT TiltedPhoques::UniquePtr<IGameServerInstance> CreateGameServer(Console::ConsoleRegistry& conReg, const std::function<void()>& aCallback);

DediRunner* GetDediRunner() noexcept
{
    return s_pRunner;
}

DediRunner::DediRunner(int argc, char** argv)
    : m_console(KCompilerStopThisBullshit)
{
    s_pRunner = this;

    uv_loop_init(&m_loop);
    m_startTime = std::chrono::steady_clock::now();

    m_pServerInstance = std::move(CreateGameServer(m_console, [this, argc, argv]() { LoadSettings(argc, argv); }));

    // it is here for now..
    m_pServerInstance->Initialize();
    SaveSettingsToIni(m_console, m_SettingsPath);
}

DediRunner::~DediRunner()
{
    m_consoleRunning.store(false);
    if (m_useIni)
        SaveSettingsToIni(m_console, m_SettingsPath);
    uv_loop_close(&m_loop);
}

void DediRunner::LoadSettings(int argc, char** argv)
{
    // If we have line args load, don't use the ini
    if (argc > 1)
    {
        LoadSettingsFromCommand(m_console, argc, argv);
    }
    else
    {
        m_useIni = true;
        m_SettingsPath = fs::current_path() / kConfigPathName / kSettingsFileName;
        if (!exists(m_SettingsPath))
        {
            // there is a bug in here... waiting to be found
            // since we dont register our settings till later, so the server settings might be... missing??
            create_directory(fs::current_path() / kConfigPathName);
            SaveSettingsToIni(m_console, m_SettingsPath);
            return;
        }
        LoadSettingsFromIni(m_console, m_SettingsPath);
    }
}

struct Context
{
    TiltedPhoques::String data;
    DediRunner* instance;
};

void DediRunner::ReadStdin(uv_stream_t* apStream, ssize_t aRead, const uv_buf_t* acpBuffer)
{
    auto* ctx = static_cast<Context*>(apStream->data);

    if (aRead < 0)
    {
        if (aRead == UV_EOF)
            uv_close(reinterpret_cast<uv_handle_t*>(&apStream), nullptr);
    }
    else if (aRead > 0)
    {
        for (auto i = 0; i < aRead; ++i)
        {
            if (acpBuffer->base[i] == '\n')
            {
                ctx->instance->HandleConsole(ctx->data);
                ctx->data = "";
            }
            else
                ctx->data += acpBuffer->base[i];
        }
    }

    // OK to free buffer as write_data copies it.
    if (acpBuffer->base)
        TiltedPhoques::Allocator::GetDefault()->Free(acpBuffer->base);
}

void DediRunner::AllocateBuffer(uv_handle_t* apHandle, size_t aSuggestedSize, uv_buf_t* apBuffer)
{
    *apBuffer = uv_buf_init(static_cast<char*>(TiltedPhoques::Allocator::GetDefault()->Allocate(aSuggestedSize)), static_cast<uint32_t>(aSuggestedSize));
}

void DediRunner::PrintExecutorArrowHack()
{
    // Force:
    // This is a hack to get the executor arrow.
    // If you find a way to do this through the ConOut log channel
    // please let me know (The issue is the forced formatting for that channel and the forced null termination)
    // fmt::print(">>>");
}

void DediRunner::RunGSThread()
{
    if (!m_pServerInstance)
        return;

    while (m_pServerInstance->IsRunning())
    {
        m_tickCounter.fetch_add(1, std::memory_order_relaxed);
        m_pServerInstance->Update();
#ifdef _WIN32
        ProcessQueuedCommands();
#else
        uv_run(&m_loop, UV_RUN_NOWAIT);
#endif
        if (m_console.Update())
            PrintExecutorArrowHack();
    }
}

void DediRunner::StartTerminalIO()
{
    spdlog::get("ConOut")->info("Server started, type /help for a list of commands.");
    PrintExecutorArrowHack();

#ifdef _WIN32
    const auto inputHandle = GetStdHandle(STD_INPUT_HANDLE);
    if (inputHandle != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(inputHandle, &mode))
        {
            mode |= ENABLE_EXTENDED_FLAGS | ENABLE_INSERT_MODE | ENABLE_QUICK_EDIT_MODE | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT;
            mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
            SetConsoleMode(inputHandle, mode);
        }
    }

    m_consoleRunning.store(true);
    m_consoleThread = std::thread([this]()
    {
        std::string line;
        while (m_consoleRunning.load() && std::getline(std::cin, line))
        {
            if (line.empty())
                continue;

            std::lock_guard<std::mutex> lock(m_consoleMutex);
            m_consoleQueue.emplace(line.c_str());
        }
    });
    m_consoleThread.detach();
#else
    uv_tty_init(&m_loop, &m_tty, 0, 1);
    uv_tty_set_mode(&m_tty, UV_TTY_MODE_NORMAL);

    static Context ctx;
    ctx.instance = this;

    m_tty.data = &ctx;

    uv_read_start(reinterpret_cast<uv_stream_t*>(&m_tty), AllocateBuffer, ReadStdin);
#endif
}

void DediRunner::RequestKill()
{
    m_pServerInstance->Shutdown();

#if defined(_WIN32)
    // work around Control Handler exception (Control-C) being set
    // https://cdn.discordapp.com/attachments/675107843573022779/941772837339930674/unknown.png
    // being set.
    if (IsDebuggerPresent())
    {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(300ms);
    }
#endif
}

void DediRunner::GetStatus(ServerStatusSnapshot& aOutStatus) const
{
    if (m_pServerInstance)
        m_pServerInstance->GetStatus(aOutStatus);
    else
    {
        aOutStatus.UptimeSeconds = 0;
        aOutStatus.Players.clear();
    }
}

void DediRunner::GetSettingsSnapshot(TiltedPhoques::Vector<SettingSnapshot>& aOut)
{
    aOut.clear();
    m_console.ForAllSettings(
        [&](Console::SettingBase* setting)
        {
            if (!setting)
                return;

            SettingSnapshot snapshot{};
            snapshot.Name = setting->name;
            snapshot.Description = setting->desc;
            snapshot.Type = setting->type;
            snapshot.Flags = setting->flags;

            switch (setting->type)
            {
            case Console::SettingBase::Type::kBoolean: snapshot.Value = setting->data.as_boolean ? "true" : "false"; break;
            case Console::SettingBase::Type::kInt: snapshot.Value = std::to_string(setting->data.as_int32); break;
            case Console::SettingBase::Type::kUInt: snapshot.Value = std::to_string(setting->data.as_uint32); break;
            case Console::SettingBase::Type::kInt64: snapshot.Value = std::to_string(setting->data.as_int64); break;
            case Console::SettingBase::Type::kUInt64: snapshot.Value = std::to_string(setting->data.as_uint64); break;
            case Console::SettingBase::Type::kFloat:
            {
                std::ostringstream stream;
                stream << setting->data.as_float;
                snapshot.Value = stream.str();
                break;
            }
            case Console::SettingBase::Type::kString: snapshot.Value = setting->c_str(); break;
            default: snapshot.Value = ""; break;
            }

            aOut.push_back(std::move(snapshot));
        });
}

bool DediRunner::GetSettingValue(const TiltedPhoques::String& aName, TiltedPhoques::String& aOut)
{
    auto* setting = m_console.FindSetting(aName.c_str());
    if (!setting)
        return false;

    switch (setting->type)
    {
    case Console::SettingBase::Type::kBoolean: aOut = setting->data.as_boolean ? "true" : "false"; break;
    case Console::SettingBase::Type::kInt: aOut = std::to_string(setting->data.as_int32); break;
    case Console::SettingBase::Type::kUInt: aOut = std::to_string(setting->data.as_uint32); break;
    case Console::SettingBase::Type::kInt64: aOut = std::to_string(setting->data.as_int64); break;
    case Console::SettingBase::Type::kUInt64: aOut = std::to_string(setting->data.as_uint64); break;
    case Console::SettingBase::Type::kFloat:
    {
        std::ostringstream stream;
        stream << setting->data.as_float;
        aOut = stream.str();
        break;
    }
    case Console::SettingBase::Type::kString: aOut = setting->c_str(); break;
    default: aOut.clear(); break;
    }

    return true;
}

bool DediRunner::SetSettingValue(const TiltedPhoques::String& aName, const TiltedPhoques::String& aValue, TiltedPhoques::String* apError)
{
    auto* setting = m_console.FindSetting(aName.c_str());
    if (!setting)
    {
        if (apError)
            *apError = "Setting not found.";
        return false;
    }

    if (setting->IsLocked())
    {
        if (apError)
            *apError = "Setting is locked.";
        return false;
    }

    switch (setting->type)
    {
    case Console::SettingBase::Type::kBoolean:
    {
        TiltedPhoques::String lowered = aValue;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lowered == "true" || lowered == "1")
            setting->data.as_boolean = true;
        else if (lowered == "false" || lowered == "0")
            setting->data.as_boolean = false;
        else
        {
            if (apError)
                *apError = "Invalid boolean value.";
            return false;
        }
        break;
    }
    case Console::SettingBase::Type::kInt: setting->data.as_int32 = Console::ConvertStringValue(aValue.c_str(), setting->data.as_int32); break;
    case Console::SettingBase::Type::kUInt: setting->data.as_uint32 = Console::ConvertStringValue(aValue.c_str(), setting->data.as_uint32); break;
    case Console::SettingBase::Type::kInt64: setting->data.as_int64 = Console::ConvertStringValue(aValue.c_str(), setting->data.as_int64); break;
    case Console::SettingBase::Type::kUInt64: setting->data.as_uint64 = Console::ConvertStringValue(aValue.c_str(), setting->data.as_uint64); break;
    case Console::SettingBase::Type::kFloat: setting->data.as_float = Console::ConvertStringValue(aValue.c_str(), setting->data.as_float); break;
    case Console::SettingBase::Type::kString:
        static_cast<Console::StringSetting*>(setting)->StoreValue(*setting, aValue.c_str());
        break;
    default:
        if (apError)
            *apError = "Unsupported setting type.";
        return false;
    }

    m_console.MarkDirty();
    if (m_useIni)
        SaveSettingsToIni(m_console, m_SettingsPath);

    if (aName == "sLogLevel")
    {
        const auto level = spdlog::level::from_str(aValue.c_str());
        spdlog::set_level(level);
        spdlog::apply_all([level](const std::shared_ptr<spdlog::logger>& logger)
        {
            if (!logger)
                return;
            logger->set_level(level);
            logger->flush_on(level);
        });
    }

    return true;
}

void DediRunner::QueueConsoleCommand(const TiltedPhoques::String& acCommand)
{
    std::lock_guard<std::mutex> lock(m_consoleMutex);
    m_consoleQueue.emplace(acCommand);
}

uint32_t DediRunner::GetUptimeSeconds() const noexcept
{
    const auto elapsed = std::chrono::steady_clock::now() - m_startTime;
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
}

bool DediRunner::IsRunning() const noexcept
{
    return m_pServerInstance ? m_pServerInstance->IsRunning() : false;
}

bool DediRunner::IsListening() const noexcept
{
    return m_pServerInstance ? m_pServerInstance->IsListening() : false;
}

void DediRunner::HandleConsole(const TiltedPhoques::String& acCommand)
{
    using exr = Console::ConsoleRegistry::ExecutionResult;

    if (auto conOut = spdlog::get(KCompilerStopThisBullshit))
        conOut->info("> {}", acCommand.c_str());

    exr r = m_pServerInstance ? m_pServerInstance->ExecuteConsoleCommand(acCommand) : exr::kFailure;
    if (auto conOut = spdlog::get(KCompilerStopThisBullshit))
        conOut->info("Command executed.");

    PrintExecutorArrowHack();

    if (r == exr::kFailure)
    {
        if (auto conOut = spdlog::get(KCompilerStopThisBullshit))
            conOut->error("Command failed: {}", acCommand.c_str());
    }

    if (r == exr::kDirty && m_useIni)
        SaveSettingsToIni(m_console, m_SettingsPath);
}

void DediRunner::ProcessQueuedCommands()
{
    std::queue<TiltedPhoques::String> pending;
    {
        std::lock_guard<std::mutex> lock(m_consoleMutex);
        pending.swap(m_consoleQueue);
    }

    while (!pending.empty())
    {
        HandleConsole(pending.front());
        pending.pop();
    }
}
