
#include <TiltedCore/Filesystem.hpp>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Setting.h>
#include <base/simpleini/SimpleIni.h>
#include <base/threading/ThreadUtils.h>

#include "DediRunner.h"
#include "ServerLogSink.h"
#include "ServerUi.h"
#include <crash_handler/CrashHandler.h>

#ifdef _WIN32
#include <Windows.h>
#include <base/dialogues/win/TaskDialog.h>
#pragma comment(lib, "Comctl32.lib")
#elif defined(__linux__)
#include <signal.h>
#endif

namespace
{
constexpr char kLogFileName[] = "STServerOut.log";
// Its fine for us if several potential server instances read this, since its a tilted platform thing
// and therefore not considered game specific.
constexpr char kEULAName[] = "EULA.txt";
constexpr char kEULAText[] = ";Please indicate your agreement to the Tilted platform service agreement\n"
                             ";by setting bConfirmEULA to true\n"
                             "[EULA]\n";
constexpr char kEULATextFalse[] = "bConfirmEULA=false";
constexpr char kEULATextTrue[] = "bConfirmEULA=true";

namespace fs = std::filesystem;

Console::StringSetting sLogLevel{"sLogLevel", "Log level to print", "info"};
using namespace std::chrono_literals;
} // namespace

extern Console::Setting<bool> bConsole;

GS_IMPORT void SetDefaultLogger(std::shared_ptr<spdlog::logger> aLogger);
GS_IMPORT void RegisterLogger(std::shared_ptr<spdlog::logger> aLogger);
GS_IMPORT bool CheckBuildTag(const char* apBuildTag);
GS_IMPORT void SetUiLogCallback(void (*aCallback)(const char*));

struct LogInstance
{
    static constexpr size_t kLogFileSizeCap = 1048576 * 5;

    LogInstance()
    {
        using namespace spdlog;

#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
#endif

        std::error_code ec;
        const auto logPath = TiltedPhoques::GetPath() / "logs";
        fs::create_directory(logPath, ec);

        auto fileOut = std::make_shared<sinks::rotating_file_sink_mt>((logPath / kLogFileName).string(), kLogFileSizeCap, 3);
        auto consoleSink = std::make_shared<sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("%^[%H:%M:%S.%e] [%l] [tid %t] %$ %v");

        auto consoleOut = std::make_shared<logger>(KCompilerStopThisBullshit, sinks_init_list{consoleSink, fileOut});
        consoleOut->set_level(level::from_str(sLogLevel.value()));

        if (!spdlog::get(KCompilerStopThisBullshit))
            spdlog::register_logger(consoleOut);

        RegisterLogger(consoleOut);

        auto globalOut = std::make_shared<logger>("", sinks_init_list{consoleSink, fileOut});
        globalOut->set_level(level::from_str(sLogLevel.value()));

        // as the library is compiled into the client + server we have to do this twice
        spdlog::set_default_logger(globalOut);

        // also make the client aware of the file loggers
        SetDefaultLogger(globalOut);

        AttachServerLogSinkToAllLoggers();
    }

    ~LogInstance() { spdlog::shutdown(); }
};

static bool RegisterQuitHandler()
{
#if defined(_WIN32)
    static auto CtrlHandler = ([](DWORD aType) {
        switch (aType)
        {
        case CTRL_C_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT: {
            if (auto* pRunner = GetDediRunner())
            {
                pRunner->RequestKill();
                return TRUE;
            }
            // if the user kills during the ctor we deny the request
            // to save our dear life.
            return FALSE;
        }
        default:
            return FALSE;
        }
    });

    return SetConsoleCtrlHandler(CtrlHandler, TRUE);

#elif defined(__linux__)
    static auto CtrlHandler = ([](int aSig) {
        if (auto* pRunner = GetDediRunner())
        {
            pRunner->RequestKill();
        }
    });

    signal(SIGINT, CtrlHandler);
    signal(SIGTERM, CtrlHandler);
    signal(SIGKILL, CtrlHandler);
#else
    return true;
#endif
}

#ifdef _WIN32
static bool ShowEULADialog()
{
    Base::TaskDialog dia(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(102)), L"Tilted Platform Agreement", L"Confirm the Tilted Platform EULA", L"TODO: Link to EULA", nullptr);
    dia.AppendButton(100, L"Accept EULA");
    dia.AppendButton(101, L"Deny EULA");
    dia.SetDefaultButton(101 /*So they have to think about it*/);

    return dia.Show() == 100;
}
#endif

// The eula can be accepted in 3 ways:
// - Either confirm the dialog on startup(windows only)
// - Or signal agreement using an environment variable
// - Or Simply set bConfirmEULA in EULA.txt to true
static bool IsEULAAccepted()
{
    const auto path = fs::current_path() / kConfigPathName / kEULAName;

    bool preAccept = false;
    char* pValue = nullptr;
#ifdef _WIN32
    size_t envLength = 0;
    if (_dupenv_s(&pValue, &envLength, "TILTED_ACCEPT_EULA") == 0 && pValue)
#else
    if ((pValue = std::getenv("TILTED_ACCEPT_EULA")) != nullptr)
#endif
    {
        std::string_view env(pValue);
        preAccept = env == "true" || env == "1" || env == "TRUE";
    }
#ifdef _WIN32
    if (pValue)
        free(pValue);
#endif

    auto saveFile = [&]()
    {
#ifdef _WIN32
        // try using the dialog
        if (!preAccept)
            preAccept = ShowEULADialog();
#endif
        fs::create_directory(fs::current_path() / kConfigPathName);

        TiltedPhoques::String eulaText = kEULAText;
        eulaText += preAccept ? kEULATextTrue : kEULATextFalse;

        TiltedPhoques::SaveFile(path, eulaText);
        return preAccept;
    };

    if (!exists(path))
    {
        return saveFile();
    }

    const auto data = TiltedPhoques::LoadFile(path);

    CSimpleIni si;

    if (si.LoadData(data.c_str()) != SI_OK)
        return preAccept;

    if (!si.GetBoolValue("EULA", "bConfirmEULA", false))
    {
#ifdef _WIN32
        preAccept = false;
        return saveFile();
#else
        return false;
#endif
    }

    return true;
}

static void UiLogCallback(const char* aLine)
{
    if (!aLine)
        return;

    if (auto sink = GetServerLogSink())
        sink->PushExternalLine(aLine);
}

void ConfigureConsoleMode()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    // Set the input code page to UTF-8
    SetConsoleCP(CP_UTF8);
#endif
}

int main(int argc, char** argv)
{
    ConfigureConsoleMode();

    LogInstance logger;
    (void)logger;

    // the binaries are not from the same commit.
    if (!CheckBuildTag(kBuildTag))
    {
#ifdef _WIN32
        MessageBoxW(nullptr, L"Server runner and server DLL build tags do not match.", L"SkyrimTogetherServer", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    SetUiLogCallback(&UiLogCallback);

    Base::SetCurrentThreadName("ServerRunnerMain");

#ifdef _WIN32
    if (auto* hwnd = GetConsoleWindow())
        ShowWindow(hwnd, SW_HIDE);
#endif

    // Disabled EULA check since we have no EULA contents yet
    /*
    if (!IsEULAAccepted())
    {
        spdlog::error("Please accept the EULA by setting bConfirmEULA to true in EULA.txt");
        return 2;
    }
    */

    //ScopedCrashHandler _(true, true);

    // Note(Vince): This started crashing on 1.7+ lets disable it for now.
    // RegisterQuitHandler(); 

    // Keep stack free.
    const auto cpRunner{std::make_unique<DediRunner>(argc, argv)};

#ifdef _WIN32
    ServerUi ui(*cpRunner);
    if (ui.Initialize())
    {
#ifdef _WIN32
        // Console input is hidden; UI handles commands.
        std::thread serverThread([&]() {
#if defined(_WIN32)
            __try
            {
                cpRunner->RunGSThread();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
#else
            cpRunner->RunGSThread();
#endif
        });
#endif
        ui.Run();
        serverThread.join();
    }
    else
    {
        MessageBoxW(nullptr, L"Server UI failed to initialize.", L"SkyrimTogetherServer", MB_OK | MB_ICONERROR);
    }
#endif

    cpRunner->RequestKill();

    return 0;
}
