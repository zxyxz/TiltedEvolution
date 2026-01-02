#include "ServerUi.h"

#include "DediRunner.h"
#include "ServerLogSink.h"

#include <imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>
#include <spdlog/spdlog.h>

#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// According to imgui documentation we have to do it this way in order to avoid link conflicts with windows.h
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


namespace
{
constexpr float kClearColor[4] = {0.08f, 0.09f, 0.11f, 1.0f};
constexpr const char* kLogLevelOptions[] = {"trace", "debug", "info", "warn", "error", "critical", "off"};

struct LogColor
{
    bool Use{false};
    ImVec4 Color{};
};

bool WantsQuit(const MSG& msg)
{
    return msg.message == WM_QUIT;
}

LogColor GetLogColor(std::string_view line)
{
    size_t levelStart = std::string_view::npos;
    size_t levelEnd = std::string_view::npos;

    if (!line.empty() && line[0] == '[')
    {
        levelStart = 1;
        levelEnd = line.find(']', levelStart);
    }
    else
    {
        const size_t marker = line.find("] [");
        if (marker != std::string_view::npos)
        {
            levelStart = marker + 3;
            levelEnd = line.find(']', levelStart);
        }
    }

    if (levelEnd == std::string_view::npos || levelEnd <= levelStart)
        return {};

    std::string level(line.substr(levelStart, levelEnd - levelStart));
    for (auto& c : level)
        c = static_cast<char>(std::tolower(c));

    if (level == "error" || level == "critical")
        return {true, ImVec4(0.96f, 0.35f, 0.30f, 1.0f)};
    if (level == "warning" || level == "warn")
        return {true, ImVec4(0.98f, 0.76f, 0.35f, 1.0f)};
    if (level == "info")
        return {true, ImVec4(0.85f, 0.90f, 0.98f, 1.0f)};
    if (level == "debug")
        return {true, ImVec4(0.55f, 0.82f, 0.90f, 1.0f)};
    if (level == "trace")
        return {true, ImVec4(0.62f, 0.65f, 0.72f, 1.0f)};

    return {};
}

TiltedPhoques::String ToLowerCopy(std::string_view text)
{
    TiltedPhoques::String out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

int FindLogLevelIndex(std::string_view level)
{
    TiltedPhoques::String lowered = ToLowerCopy(level);
    for (int i = 0; i < IM_ARRAYSIZE(kLogLevelOptions); ++i)
    {
        if (lowered == kLogLevelOptions[i])
            return i;
    }
    return 2;
}

bool MatchesFilter(std::string_view value, const TiltedPhoques::String& filterLower)
{
    if (filterLower.empty())
        return true;
    return ToLowerCopy(value).find(filterLower) != TiltedPhoques::String::npos;
}

TiltedPhoques::String GetSettingGroup(const TiltedPhoques::String& name)
{
    const auto pos = name.find(':');
    if (pos == TiltedPhoques::String::npos || pos == 0)
        return "General";
    return name.substr(0, pos);
}

TiltedPhoques::String GetSettingLabel(const TiltedPhoques::String& name)
{
    const auto pos = name.find(':');
    if (pos == TiltedPhoques::String::npos || pos + 1 >= name.size())
        return name;
    return name.substr(pos + 1);
}

int32_t ParseInt32(const TiltedPhoques::String& text, int32_t fallback)
{
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (!end || end == text.c_str())
        return fallback;
    return static_cast<int32_t>(value);
}

int64_t ParseInt64(const TiltedPhoques::String& text, int64_t fallback)
{
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (!end || end == text.c_str())
        return fallback;
    return static_cast<int64_t>(value);
}

uint32_t ParseUInt32(const TiltedPhoques::String& text, uint32_t fallback)
{
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (!end || end == text.c_str())
        return fallback;
    return static_cast<uint32_t>(value);
}

uint64_t ParseUInt64(const TiltedPhoques::String& text, uint64_t fallback)
{
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (!end || end == text.c_str())
        return fallback;
    return static_cast<uint64_t>(value);
}

float ParseFloat(const TiltedPhoques::String& text, float fallback)
{
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (!end || end == text.c_str())
        return fallback;
    return value;
}

} // namespace

ServerUi::ServerUi(DediRunner& aRunner)
    : m_runner(aRunner)
{
}

ServerUi::~ServerUi()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    CleanupRenderTarget();
    CleanupDevice();
}

bool ServerUi::Initialize()
{
    WNDCLASSEXW wc = {
        sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"STServerUI", nullptr};
    if (!RegisterClassExW(&wc))
    {
        return false;
    }

    m_hwnd = CreateWindowW(wc.lpszClassName, L"Skyrim Together Server", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 720, nullptr, nullptr, wc.hInstance, this);
    if (!m_hwnd)
    {
        return false;
    }

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (!CreateDevice())
    {
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);

    m_imguiDriver.Initialize(m_hwnd);
    if (!ImGui_ImplWin32_Init(m_hwnd))
    {
        return false;
    }

    ImGui_ImplDX11_Init(m_device, m_deviceContext);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowPadding = ImVec2(16, 16);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(12, 10);
    style.ItemInnerSpacing = ImVec2(8, 6);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.58f, 0.63f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.12f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.17f, 0.26f, 0.32f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.22f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.23f, 0.28f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.26f, 0.30f, 0.36f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.30f, 0.34f, 0.41f, 1.00f);
    m_logSink = GetServerLogSink();
    AttachServerLogSinkToAllLoggers();

    if (auto conOut = spdlog::get(KCompilerStopThisBullshit))
    {
        conOut->info("Server UI attached.");
        conOut->info("Server ready. Type /help for commands.");
    }
    else
    {
        spdlog::info("Server UI attached.");
        spdlog::info("Server ready. Type /help for commands.");
    }

    m_logLines.emplace_back("Server UI attached (local).");
    m_logLines.emplace_back("Server ready. Type /help for commands.");
    return true;
}

void ServerUi::Run()
{
    MSG msg{};
    while (m_running.load())
    {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (WantsQuit(msg))
            {
                m_running.store(false);
                break;
            }
        }

        if (!m_running.load())
            break;

        RenderFrame();
    }
}

void ServerUi::RequestClose()
{
    m_running.store(false);
}

void ServerUi::RenderFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    DrawUi();

    ImGui::Render();
    const float clearColor[4] = {kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3]};
    m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, nullptr);
    m_deviceContext->ClearRenderTargetView(m_renderTargetView, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swapChain->Present(1, 0);
}

void ServerUi::DrawUi()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);

    if (!ImGui::Begin("Server Console", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::End();
        return;
    }

    ServerStatusSnapshot snapshot;
    m_runner.GetStatus(snapshot);

    const uint32_t totalSeconds = m_runner.GetUptimeSeconds();
    const uint32_t days = totalSeconds / 86400;
    const uint32_t hours = (totalSeconds % 86400) / 3600;
    const uint32_t minutes = (totalSeconds % 3600) / 60;
    const uint32_t seconds = totalSeconds % 60;

    if (ImGui::BeginChild("TopBar", ImVec2(0, 64), true))
    {
        ImGui::TextUnformatted("Skyrim Together Server");
        ImGui::SameLine();
        ImGui::TextDisabled("Uptime %ud %02uh %02um %02us", days, hours, minutes, seconds);
        ImGui::SameLine();
        ImGui::TextDisabled("Players %zu", snapshot.Players.size());
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextUnformatted(m_runner.IsRunning() ? "Running" : "Stopped");
    }
    ImGui::EndChild();

    const float sidebarWidth = 220.0f;
    if (ImGui::BeginChild("Body", ImVec2(0, 0), false))
    {
        if (ImGui::BeginChild("Sidebar", ImVec2(sidebarWidth, 0), true))
        {
            ImGui::TextUnformatted("Control Panel");
            ImGui::Separator();
            auto drawSidebarButton = [&](const char* label, View view)
            {
                const bool selected = m_activeView == view;
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.28f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.26f, 0.32f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.30f, 0.38f, 1.00f));
                }

                if (ImGui::Button(label, ImVec2(-FLT_MIN, 0)))
                    m_activeView = view;

                if (selected)
                    ImGui::PopStyleColor(3);
            };

            drawSidebarButton("Console", View::Console);
            drawSidebarButton("Players", View::Players);
            drawSidebarButton("Settings", View::Settings);
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("Main", ImVec2(0, 0), false))
        {
            switch (m_activeView)
            {
            case View::Console: DrawConsoleView(display); break;
            case View::Players: DrawPlayersView(snapshot); break;
            case View::Settings: DrawSettingsView(); break;
            default: DrawConsoleView(display); break;
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::End();
}

void ServerUi::DrawConsoleView(const ImVec2& display)
{
    (void)display;
    if (ImGui::BeginChild("ConsoleCard", ImVec2(0, 0), true))
    {
        ImGui::TextUnformatted("Console");
        ImGui::Separator();

        ImGui::Checkbox("Auto-scroll", &m_autoScroll);
        ImGui::SameLine();
        if (ImGui::Button("Clear Output"))
            m_logLines.clear();
        ImGui::SameLine();
        TiltedPhoques::String logLevelValue;
        int logLevelIndex = 2;
        if (m_runner.GetSettingValue("sLogLevel", logLevelValue))
        logLevelIndex = FindLogLevelIndex(logLevelValue);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Log level", &logLevelIndex, kLogLevelOptions, IM_ARRAYSIZE(kLogLevelOptions)))
        {
            ApplySettingValue("sLogLevel", kLogLevelOptions[logLevelIndex]);
        }

        ImGui::BeginChild("LogOutput", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true);
        {
            if (m_logSink)
                m_logSink->ConsumeLines(m_logLines);

            ImGuiListClipper clipper;
            const int lineCount = m_logLines.size() > static_cast<size_t>(std::numeric_limits<int>::max())
                                      ? std::numeric_limits<int>::max()
                                      : static_cast<int>(m_logLines.size());
            clipper.Begin(lineCount);
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const auto& line = m_logLines[i];
                    const LogColor color = GetLogColor(line.c_str());
                    if (color.Use)
                        ImGui::PushStyleColor(ImGuiCol_Text, color.Color);

                    const bool isSelected = (m_selectedLogIndex >= 0 && m_selectionAnchor >= 0)
                                                ? (i >= std::min(m_selectedLogIndex, m_selectionAnchor) && i <= std::max(m_selectedLogIndex, m_selectionAnchor))
                                                : (m_selectedLogIndex == i);

                    ImGui::PushID(i);
                    if (ImGui::Selectable(line.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            m_selectedLogIndex = -1;
                            m_selectionAnchor = -1;
                        }
                        else if (ImGui::GetIO().KeyShift && m_selectedLogIndex >= 0)
                        {
                            m_selectionAnchor = m_selectedLogIndex;
                        }
                        else
                        {
                            m_selectionAnchor = -1;
                        }
                        m_selectedLogIndex = i;
                    }
                    ImGui::PopID();

                    if (color.Use)
                        ImGui::PopStyleColor();
                }
            }

            if (m_selectedLogIndex >= 0 && m_selectedLogIndex < static_cast<int>(m_logLines.size()))
            {
                if (ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl)
                {
                    int start = m_selectedLogIndex;
                    int end = m_selectedLogIndex;
                    if (m_selectionAnchor >= 0)
                    {
                        start = std::min(m_selectionAnchor, m_selectedLogIndex);
                        end = std::max(m_selectionAnchor, m_selectedLogIndex);
                    }

                    std::string combined;
                    for (int i = start; i <= end && i < static_cast<int>(m_logLines.size()); ++i)
                    {
                        combined.append(m_logLines[i].c_str());
                        if (i != end)
                            combined.append("\n");
                    }
                    ImGui::SetClipboardText(combined.c_str());
                }
            }

            if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##CommandInput", "Type a command (/help) or server message", m_commandBuffer, sizeof(m_commandBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
        {
            TiltedPhoques::String command = m_commandBuffer;
            if (!command.empty())
            {
                m_runner.QueueConsoleCommand(command);
                TiltedPhoques::String echo = "> ";
                echo += command;
                m_logLines.emplace_back(std::move(echo));
            }
            m_commandBuffer[0] = '\0';
        }
    }
    ImGui::EndChild();
}

void ServerUi::DrawPlayersView(const ServerStatusSnapshot& snapshot)
{
    if (ImGui::BeginChild("PlayersCard", ImVec2(0, 0), true))
    {
        ImGui::TextUnformatted("Players");
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu online)", snapshot.Players.size());
        ImGui::Separator();

        if (snapshot.Players.empty())
        {
            ImGui::TextDisabled("No players connected.");
        }
        else if (ImGui::BeginTable("Players", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Cell");
            ImGui::TableSetupColumn("Grid");
            ImGui::TableSetupColumn("Pos");
            ImGui::TableHeadersRow();

            for (const auto& player : snapshot.Players)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(player.Username.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%08X:%08X", player.CellModId, player.CellBaseId);
                if (player.WorldBaseId || player.WorldModId)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("ws %08X:%08X", player.WorldModId, player.WorldBaseId);
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d, %d", player.GridX, player.GridY);

                ImGui::TableSetColumnIndex(3);
                if (player.HasPosition)
                    ImGui::Text("%.1f %.1f %.1f", player.PositionX, player.PositionY, player.PositionZ);
                else
                    ImGui::TextUnformatted("--");
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void ServerUi::DrawSettingsView()
{
    if (ImGui::BeginChild("SettingsCard", ImVec2(0, 0), true))
    {
        ImGui::TextUnformatted("Server Settings");
        ImGui::Separator();

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##SettingSearch", "Search settings (name or description)", m_settingsSearch, sizeof(m_settingsSearch));

        if (!m_settingsError.empty())
        {
            ImGui::TextColored(ImVec4(0.96f, 0.35f, 0.30f, 1.0f), "%s", m_settingsError.c_str());
        }
        else
        {
            ImGui::TextDisabled("Changes apply immediately. Locked settings are read-only.");
        }

        TiltedPhoques::Vector<DediRunner::SettingSnapshot> settings;
        m_runner.GetSettingsSnapshot(settings);
        std::sort(settings.begin(), settings.end(), [](const auto& lhs, const auto& rhs) { return lhs.Name < rhs.Name; });

        const TiltedPhoques::String filterLower = ToLowerCopy(m_settingsSearch);

        if (ImGui::BeginTable("SettingsTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("Setting");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Description");
            ImGui::TableHeadersRow();

            TiltedPhoques::String currentGroup;
            for (const auto& setting : settings)
            {
                if (setting.Flags & Console::SettingsFlags::kHidden)
                    continue;

                const auto groupName = GetSettingGroup(setting.Name);
                const auto label = GetSettingLabel(setting.Name);

                if (!MatchesFilter(setting.Name, filterLower) && !MatchesFilter(setting.Description, filterLower) && !MatchesFilter(label, filterLower))
                    continue;

                if (groupName != currentGroup)
                {
                    currentGroup = groupName;
                    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", currentGroup.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TableSetColumnIndex(2);
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label.c_str());
                if (setting.Flags & Console::SettingsFlags::kLocked)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(locked)");
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(setting.Name.c_str());
                ImGui::SetNextItemWidth(-FLT_MIN);

                auto& state = m_settingState[setting.Name];
                if (!state.initialized || state.type != setting.Type)
                {
                    state.type = setting.Type;
                    state.initialized = true;
                    state.lastValue = setting.Value;
                    state.textValue = setting.Value;
                    std::snprintf(state.textBuffer.data(), state.textBuffer.size(), "%s", state.textValue.c_str());
                    state.intValue = ParseInt32(setting.Value, state.intValue);
                    state.int64Value = ParseInt64(setting.Value, state.int64Value);
                    state.uintValue = ParseUInt32(setting.Value, state.uintValue);
                    state.uint64Value = ParseUInt64(setting.Value, state.uint64Value);
                    state.floatValue = ParseFloat(setting.Value, state.floatValue);
                }
                else if (!state.editing && state.lastValue != setting.Value)
                {
                    state.lastValue = setting.Value;
                    state.textValue = setting.Value;
                    std::snprintf(state.textBuffer.data(), state.textBuffer.size(), "%s", state.textValue.c_str());
                    state.intValue = ParseInt32(setting.Value, state.intValue);
                    state.int64Value = ParseInt64(setting.Value, state.int64Value);
                    state.uintValue = ParseUInt32(setting.Value, state.uintValue);
                    state.uint64Value = ParseUInt64(setting.Value, state.uint64Value);
                    state.floatValue = ParseFloat(setting.Value, state.floatValue);
                }

                const bool isLocked = setting.Flags & Console::SettingsFlags::kLocked;
                if (isLocked)
                    ImGui::BeginDisabled(true);

                bool apply = false;
                TiltedPhoques::String newValue = setting.Value;
                switch (setting.Type)
                {
                case Console::SettingBase::Type::kBoolean:
                {
                    bool value = setting.Value == "true" || setting.Value == "1";
                    if (ImGui::Checkbox("##bool", &value))
                    {
                        newValue = value ? "true" : "false";
                        apply = true;
                    }
                    break;
                }
                case Console::SettingBase::Type::kInt:
                    if (ImGui::InputInt("##int", &state.intValue, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue))
                        newValue = std::to_string(state.intValue);
                    apply = ImGui::IsItemDeactivatedAfterEdit();
                    break;
                case Console::SettingBase::Type::kUInt:
                    if (ImGui::InputScalar("##uint", ImGuiDataType_U32, &state.uintValue, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue))
                        newValue = std::to_string(state.uintValue);
                    apply = ImGui::IsItemDeactivatedAfterEdit();
                    break;
                case Console::SettingBase::Type::kInt64:
                    if (ImGui::InputScalar("##int64", ImGuiDataType_S64, &state.int64Value, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue))
                        newValue = std::to_string(state.int64Value);
                    apply = ImGui::IsItemDeactivatedAfterEdit();
                    break;
                case Console::SettingBase::Type::kUInt64:
                    if (ImGui::InputScalar("##uint64", ImGuiDataType_U64, &state.uint64Value, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue))
                        newValue = std::to_string(state.uint64Value);
                    apply = ImGui::IsItemDeactivatedAfterEdit();
                    break;
                case Console::SettingBase::Type::kFloat:
                    if (ImGui::InputFloat("##float", &state.floatValue, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        std::ostringstream stream;
                        stream << state.floatValue;
                        newValue = stream.str();
                    }
                    apply = ImGui::IsItemDeactivatedAfterEdit();
                    break;
                case Console::SettingBase::Type::kString:
                    if (ImGui::InputText("##string", state.textBuffer.data(), state.textBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        state.textValue = state.textBuffer.data();
                        newValue = state.textValue;
                        apply = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        state.textValue = state.textBuffer.data();
                        newValue = state.textValue;
                        apply = true;
                    }
                    break;
                default:
                    ImGui::TextDisabled("--");
                    break;
                }

                state.editing = ImGui::IsItemActive();

                if (isLocked)
                    ImGui::EndDisabled();

                if (apply && !isLocked && newValue != setting.Value)
                {
                    if (!ApplySettingValue(setting.Name, newValue))
                    {
                        state.textValue = setting.Value;
                        std::snprintf(state.textBuffer.data(), state.textBuffer.size(), "%s", state.textValue.c_str());
                    }
                }

                ImGui::PopID();

                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", setting.Description.c_str());
            }

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

bool ServerUi::ApplySettingValue(const TiltedPhoques::String& name, const TiltedPhoques::String& value)
{
    TiltedPhoques::String error;
    if (!m_runner.SetSettingValue(name, value, &error))
    {
        m_settingsError = error;
        return false;
    }

    m_settingsError.clear();
    return true;
}

bool ServerUi::CreateDevice()
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_swapChain, &m_device,
        &featureLevel, &m_deviceContext);
    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    return true;
}

void ServerUi::CleanupDevice()
{
    CleanupRenderTarget();
    if (m_swapChain)
    {
        m_swapChain->Release();
        m_swapChain = nullptr;
    }
    if (m_deviceContext)
    {
        m_deviceContext->Release();
        m_deviceContext = nullptr;
    }
    if (m_device)
    {
        m_device->Release();
        m_device = nullptr;
    }
}

void ServerUi::CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer)
    {
        m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
        backBuffer->Release();
    }
}

void ServerUi::CleanupRenderTarget()
{
    if (m_renderTargetView)
    {
        m_renderTargetView->Release();
        m_renderTargetView = nullptr;
    }
}

LRESULT WINAPI ServerUi::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            auto* ui = reinterpret_cast<ServerUi*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            if (ui && ui->m_swapChain)
            {
                ui->CleanupRenderTarget();
                ui->m_swapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                ui->CreateRenderTarget();
            }
        }
        return 0;
    case WM_CLOSE:
        if (auto* ui = reinterpret_cast<ServerUi*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA)))
        {
            ui->RequestClose();
            ui->m_runner.RequestKill();
        }
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        if (auto* ui = reinterpret_cast<ServerUi*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA)))
        {
            ui->RequestClose();
            ui->m_runner.RequestKill();
        }
        PostQuitMessage(0);
        return 0;
    default: break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
