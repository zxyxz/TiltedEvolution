
#include "ProcessHandler.h"

ProcessHandler::ProcessHandler() noexcept
    : OverlayRenderProcessHandler("skyrimtogether")
{
}

void ProcessHandler::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context)
{
    OverlayRenderProcessHandler::OnContextCreated(browser, frame, context);

    m_pCoreObject->SetValue("on", CefV8Value::CreateFunction("on", m_pEventsHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("off", CefV8Value::CreateFunction("off", m_pEventsHandler), V8_PROPERTY_ATTRIBUTE_NONE);

    m_pCoreObject->SetValue("connect", CefV8Value::CreateFunction("connect", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("disconnect", CefV8Value::CreateFunction("disconnect", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("revealPlayers", CefV8Value::CreateFunction("revealPlayers", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("sendMessage", CefV8Value::CreateFunction("sendMessage", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("setTime", CefV8Value::CreateFunction("setTime", m_pOverlayHandler),V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("deactivate", CefV8Value::CreateFunction("deactivate", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("launchParty", CefV8Value::CreateFunction("launchParty", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("leaveParty", CefV8Value::CreateFunction("leaveParty", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("createPartyInvite", CefV8Value::CreateFunction("createPartyInvite", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("acceptPartyInvite", CefV8Value::CreateFunction("acceptPartyInvite", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("kickPartyMember", CefV8Value::CreateFunction("kickPartyMember", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("changePartyLeader", CefV8Value::CreateFunction("changePartyLeader", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("setProfilePicture", CefV8Value::CreateFunction("setProfilePicture", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("setNameTagMode", CefV8Value::CreateFunction("setNameTagMode", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("teleportToPlayer", CefV8Value::CreateFunction("teleportToPlayer", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("respondTeleportRequest", CefV8Value::CreateFunction("respondTeleportRequest", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("sendTradeInvite", CefV8Value::CreateFunction("sendTradeInvite", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("respondTradeInvite", CefV8Value::CreateFunction("respondTradeInvite", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("cancelTrade", CefV8Value::CreateFunction("cancelTrade", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("setTradeReady", CefV8Value::CreateFunction("setTradeReady", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("updateTradeOffer", CefV8Value::CreateFunction("updateTradeOffer", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("playEmote", CefV8Value::CreateFunction("playEmote", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("openEmoteMenu", CefV8Value::CreateFunction("openEmoteMenu", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("toggleDebugUI", CefV8Value::CreateFunction("toggleDebugUI", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("respawnButtonClicked", CefV8Value::CreateFunction("respawnButtonClicked", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("showBanner", CefV8Value::CreateFunction("showBanner", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
    m_pCoreObject->SetValue("toggleEmoteMenu", CefV8Value::CreateFunction("toggleEmoteMenu", m_pOverlayHandler), V8_PROPERTY_ATTRIBUTE_NONE);
}

void ProcessHandler::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context)
{
    OverlayRenderProcessHandler::OnContextReleased(browser, frame, context);
}
