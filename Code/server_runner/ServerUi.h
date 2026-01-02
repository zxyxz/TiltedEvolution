#pragma once

#include <common/ServerStatusSnapshot.h>
#include <console/Setting.h>
#include <imgui/ImGuiDriver.h>

#include <array>
#include <atomic>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <Windows.h>
#endif

struct DediRunner;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ImVec2;

struct ServerLogSink;

struct ServerUi
{
    explicit ServerUi(DediRunner& aRunner);
    ~ServerUi();

    bool Initialize();
    void Run();
    void RequestClose();

private:
    enum class View
    {
        Console,
        Players,
        Settings
    };

    struct SettingUiState
    {
        Console::SettingBase::Type type{Console::SettingBase::Type::kNone};
        bool initialized{false};
        bool editing{false};
        TiltedPhoques::String lastValue;
        TiltedPhoques::String textValue;
        std::array<char, 512> textBuffer{};
        int32_t intValue{0};
        int64_t int64Value{0};
        uint32_t uintValue{0};
        uint64_t uint64Value{0};
        float floatValue{0.0f};
    };

    bool CreateDevice();
    void CleanupDevice();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void RenderFrame();
    void DrawUi();
    void DrawConsoleView(const ImVec2& display);
    void DrawPlayersView(const ServerStatusSnapshot& snapshot);
    void DrawSettingsView();
    bool ApplySettingValue(const TiltedPhoques::String& name, const TiltedPhoques::String& value);

    static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    DediRunner& m_runner;
    HWND m_hwnd{};
    ID3D11Device* m_device{nullptr};
    ID3D11DeviceContext* m_deviceContext{nullptr};
    IDXGISwapChain* m_swapChain{nullptr};
    ID3D11RenderTargetView* m_renderTargetView{nullptr};

    ImGuiImpl::ImGuiDriver m_imguiDriver;
    std::shared_ptr<ServerLogSink> m_logSink;

    std::atomic<bool> m_running{true};
    TiltedPhoques::Vector<TiltedPhoques::String> m_logLines;
    int m_selectedLogIndex{-1};
    int m_selectionAnchor{-1};
    char m_commandBuffer[4096]{};
    bool m_autoScroll{true};
    View m_activeView{View::Console};
    char m_settingsSearch[256]{};
    TiltedPhoques::String m_settingsError;
    std::unordered_map<TiltedPhoques::String, SettingUiState> m_settingState;
};
