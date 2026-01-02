#pragma once

#include "OverlayClient.hpp"

struct TransportService;

namespace TiltedPhoques
{
struct OverlayRenderHandler;
}

/**
 * @brief Renders the UI overlay.
 */
struct OverlayClient : TiltedPhoques::OverlayClient
{
    OverlayClient(TransportService& aTransport, TiltedPhoques::OverlayRenderHandler* apHandler);
    virtual ~OverlayClient() noexcept;

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message) override;

    TP_NOCOPYMOVE(OverlayClient);

private:
    void ProcessConnectMessage(CefRefPtr<CefListValue> aEventArgs);
    void ProcessDisconnectMessage();
    void ProcessRevealPlayersMessage();
    void ProcessChatMessage(CefRefPtr<CefListValue> aEventArgs);
    void ProcessSetTimeCommand(CefRefPtr<CefListValue> aEventArgs);
    void ProcessTeleportRequestMessage(CefRefPtr<CefListValue> aEventArgs);
    void ProcessTeleportResponseMessage(CefRefPtr<CefListValue> aEventArgs);
    void ProcessSetProfilePicture(CefRefPtr<CefListValue> aEventArgs);
    void ProcessSetNameTagMode(CefRefPtr<CefListValue> aEventArgs);
    void ProcessToggleDebugUI();
    void ProcessPlayEmote(CefRefPtr<CefListValue> aEventArgs);
    void SetUIVisible(bool aVisible) noexcept;

    TransportService& m_transport;
};

extern std::atomic<bool> g_emoteWheelActive;
extern std::string g_emoteEventName;
extern std::chrono::steady_clock::time_point g_emoteLastPlayed;
extern NiPoint3 g_emoteStartPos;
extern NiPoint3 g_emoteStartRot;
extern std::atomic<bool> g_emoteStartValid;
