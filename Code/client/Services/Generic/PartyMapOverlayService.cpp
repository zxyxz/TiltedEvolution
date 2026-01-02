#include <TiltedOnlinePCH.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>
#include <Events/SetWaypointEvent.h>

#include <Messages/NotifyPlayerCellChanged.h>
#include <Messages/PartyPositionUpdateRequest.h>
#include <Messages/PartyPositionsRequest.h>

#include <Services/PartyMapOverlayService.h>
#include <Services/PartyService.h>
#include <Services/OverlayService.h>
#include <Services/Generic/CoSaveService.h>

#include <Components.h>
#include <World.h>

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <limits>
#include <glm/gtx/norm.hpp>

#include <Games/Skyrim/Interface/UI.h>
#include <Games/Skyrim/Camera/PlayerCamera.h>
#include <Games/Skyrim/Renderer.h>
#include <Games/Skyrim/PlayerCharacter.h>
#include <Forms/TESWorldSpace.h>
#include <Forms/TESObjectCELL.h>
#include <Games/Skyrim/Forms/TESForm.h>
#include <Games/Skyrim/TESObjectREFR.h>
#include <Games/Skyrim/Interface/Menus/HUDMenuUtils.h>
#include <Games/Skyrim/WorldMapProjector.h>

namespace
{
std::string JsonEscapeString(const TiltedPhoques::String& input)
{
    std::string escaped;
    escaped.reserve(input.size());
    for (char ch : input)
    {
        switch (ch)
        {
        case '\\': escaped += "\\\\"; break;
        case '\"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

uint64_t GetEpochSeconds() noexcept
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

bool HasMenuOpen(UI* apUI, const char* acName)
{
    if (!apUI || !acName)
        return false;

    const BSFixedString desired(acName);
    if (apUI->GetMenuOpen(desired))
        return true;

    for (auto* pMenu : apUI->menuStack)
    {
        if (!pMenu)
            continue;
        if (auto* pName = apUI->LookupMenuNameByInstance(pMenu))
        {
            if (*pName == desired)
                return true;
        }
    }

    return false;
}

}

PartyMapOverlayService::PartyMapOverlayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    WorldMapProjector::WarmupAsync();

    // Update position cache each frame
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&PartyMapOverlayService::OnUpdate>(this);


    // Network-driven worldspace updates and lifecycle events
    m_cellChangedConnection = aDispatcher.sink<NotifyPlayerCellChanged>().connect<&PartyMapOverlayService::OnPlayerCellChanged>(this);
    m_positionsConnection = aDispatcher.sink<NotifyPartyPositions>().connect<&PartyMapOverlayService::OnPartyPositions>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&PartyMapOverlayService::OnDisconnected>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&PartyMapOverlayService::OnPartyJoined>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&PartyMapOverlayService::OnPartyLeft>(this);
}

void PartyMapOverlayService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    std::scoped_lock lock(m_cacheMutex);
    m_last.clear();
    m_worlds.clear();
    m_lastPerWorld.clear();
    m_lastScreen.clear();
    m_lastValidDisplayWorldId = 0;
}

void PartyMapOverlayService::OnPartyJoined(const PartyJoinedEvent&) noexcept
{
    std::scoped_lock lock(m_cacheMutex);
    PruneNonPartyEntries();
    PartyPositionsRequest req{};
    m_world.GetTransport().Send(req);
}

void PartyMapOverlayService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    std::scoped_lock lock(m_cacheMutex);
    m_last.clear();
    m_worlds.clear();
    m_lastPerWorld.clear();
    m_lastScreen.clear();
    m_lastValidDisplayWorldId = 0;
}

void PartyMapOverlayService::OnPlayerCellChanged(const NotifyPlayerCellChanged& aMsg) noexcept
{
    std::unique_lock<std::mutex> lock(m_cacheMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    // Track worldspace per player when available
    auto& modSystem = m_world.GetModSystem();

    WorldspaceInfo info{};
    info.IsInterior = !aMsg.WorldSpaceId;
    if (aMsg.WorldSpaceId)
    {
        info.WorldSpaceFormId = modSystem.GetGameId(aMsg.WorldSpaceId);
        info.HasWorld = info.WorldSpaceFormId != 0;
    }
    else
    {
        // Interior cells have no worldspace; skip for now (map UI is worldspace-only)
        info.WorldSpaceFormId = 0;
        info.HasWorld = false;
    }

    m_worlds[aMsg.PlayerId] = info;
}

void PartyMapOverlayService::OnPartyPositions(const NotifyPartyPositions& aMsg) noexcept
{
    std::unique_lock<std::mutex> lock(m_cacheMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    // Update caches from server-sent party positions (covers far-away/unloaded players)
    const uint64_t tick = m_world.GetTick();
    auto& modSystem = m_world.GetModSystem();

    for (const auto& e : aMsg.Entries)
    {
        glm::vec3 pos{e.Position.x, e.Position.y, e.Position.z};
        StoreLastInfo(e.PlayerId, pos, tick);

        WorldspaceInfo info{};
        info.IsInterior = e.IsInterior;
        if (e.WorldSpaceId)
        {
            info.WorldSpaceFormId = modSystem.GetGameId(e.WorldSpaceId);
            info.HasWorld = info.WorldSpaceFormId != 0;
        }
        else
        {
            info.WorldSpaceFormId = 0;
            info.HasWorld = false; // interior or unknown
        }
        m_worlds[e.PlayerId] = info;

        // Track last known position per worldspace; projection is deferred to the render thread.
        if (info.HasWorld)
            m_lastPerWorld[e.PlayerId][info.WorldSpaceFormId] = pos;
    }
}


void PartyMapOverlayService::PruneNonPartyEntries() noexcept
{
    const auto& members = m_world.GetPartyService().GetPartyMembers();
    TiltedPhoques::Vector<uint32_t> toErase;
    toErase.reserve(std::max<size_t>(m_last.size(), m_worlds.size()));

    for (const auto& [pid, _] : m_last)
    {
        if (std::find(members.begin(), members.end(), pid) == members.end())
            toErase.push_back(pid);
    }
    for (auto id : toErase)
        m_last.erase(id);

    toErase.clear();
    for (const auto& [pid, _] : m_worlds)
    {
        if (std::find(members.begin(), members.end(), pid) == members.end())
            toErase.push_back(pid);
    }
    for (auto id : toErase)
        m_worlds.erase(id);

    // Prune per-world history for non-members
    toErase.clear();
    for (const auto& [pid, _] : m_lastPerWorld)
    {
        if (std::find(members.begin(), members.end(), pid) == members.end())
            toErase.push_back(pid);
    }
    for (auto id : toErase)
        m_lastPerWorld.erase(id);

    // Prune last-screen cache for non-members
    toErase.clear();
    for (const auto& [pid, _] : m_lastScreen)
    {
        if (std::find(members.begin(), members.end(), pid) == members.end())
            toErase.push_back(pid);
    }
    for (auto id : toErase)
        m_lastScreen.erase(id);
}

void PartyMapOverlayService::OnUpdate(const UpdateEvent&) noexcept
{
    std::unique_lock<std::mutex> lock(m_cacheMutex, std::try_to_lock);
    if (!lock.owns_lock())
        return;
    const uint64_t tick = m_world.GetTick();

    // Cache last known positions for loaded remote players
    auto view = m_world.view<PlayerComponent, InterpolationComponent>();
    for (auto e : view)
    {
        const auto& pc = view.get<PlayerComponent>(e);
        const auto& interp = view.get<InterpolationComponent>(e);
        StoreLastInfo(pc.Id, interp.Position, tick);
    }

    // Periodically send our own position to the server for party tracking
    {
        static auto s_lastPosSend = std::chrono::steady_clock::time_point{};
        constexpr auto cPosInterval = std::chrono::milliseconds(100);
        const auto now = std::chrono::steady_clock::now();
        if (now - s_lastPosSend >= cPosInterval)

        {
            s_lastPosSend = now;
            if (auto* pPc = PlayerCharacter::Get())
            {
                PartyPositionUpdateRequest req{};
                req.Position.x = pPc->position.x;
                req.Position.y = pPc->position.y;
                req.Position.z = pPc->position.z;

                auto& modSystem = m_world.GetModSystem();
                if (auto* pWs = pPc->GetWorldSpace())
                    modSystem.GetServerModId(pWs->formID, req.WorldSpaceId);
                else
                    req.WorldSpaceId = {};

                if (pPc->parentCell)
                    modSystem.GetServerModId(pPc->parentCell->formID, req.CellId);
                else
                    req.CellId = {};

                m_world.GetTransport().Send(req);

                CoSaveStorage::LocalPlayerLocation location{};
                const auto existing = m_world.ctx().at<CoSaveService>().GetLocalPlayerLocation();
                location.HasLocation = true;
                location.Position = req.Position;
                location.WorldSpaceId = req.WorldSpaceId;
                location.CellId = req.CellId;
                location.LastSeenEpoch = GetEpochSeconds();
                location.HasExterior = req.WorldSpaceId;
                if (location.HasExterior)
                {
                    location.ExteriorPosition = req.Position;
                    location.ExteriorWorldSpaceId = req.WorldSpaceId;
                    location.ExteriorCellId = req.CellId;
                    location.ExteriorLastSeenEpoch = location.LastSeenEpoch;
                }
                else if (existing && existing->HasExterior)
                {
                    location.HasExterior = true;
                    location.ExteriorPosition = existing->ExteriorPosition;
                    location.ExteriorWorldSpaceId = existing->ExteriorWorldSpaceId;
                    location.ExteriorCellId = existing->ExteriorCellId;
                    location.ExteriorLastSeenEpoch = existing->ExteriorLastSeenEpoch;
                }
                m_world.ctx().at<CoSaveService>().UpdateLocalPlayerLocation(location);
            }
        }
    }

    // Prune very stale entries (>10 minutes)
    const uint64_t maxAge = 10ull * 60ull * 1000ull;
    for (auto it = m_last.begin(); it != m_last.end();)
    {
        if (tick - it->second.Tick > maxAge)
            it = m_last.erase(it);
        else
            ++it;
    }

    // Build and send pins to CEF overlay at a throttled rate
    static auto s_lastSend = std::chrono::steady_clock::time_point{};
    constexpr auto cDelayBetweenUpdates = std::chrono::milliseconds(16); // ~60 FPS for tighter tracking
    const auto now = std::chrono::steady_clock::now();
    if (now - s_lastSend < cDelayBetweenUpdates)
        return;
    s_lastSend = now;

    auto& partyService = m_world.GetPartyService();
    UI* pUI = UI::Get();
    const bool mapOpen = pUI && pUI->GetMenuOpen(BSFixedString("MapMenu"));
    if (!mapOpen)
    {
        m_world.GetOverlayService().SetPartyPinsJson("[]");
        return;
    }

    const bool fastTravelPromptOpen = HasMenuOpen(pUI, "MessageBoxMenu");
    const bool loadingScreenOpen = HasMenuOpen(pUI, "LoadingMenu") || HasMenuOpen(pUI, "Loading Menu") ||
                                   HasMenuOpen(pUI, "FaderMenu") || HasMenuOpen(pUI, "Fader Menu") ||
                                   HasMenuOpen(pUI, "LoadWaitSpinner") || HasMenuOpen(pUI, "Load Wait Spinner") ||
                                   HasMenuOpen(pUI, "Sleep/Wait Menu");
    if (fastTravelPromptOpen || loadingScreenOpen)
    {
        m_world.GetOverlayService().SetPartyPinsJson("[]");
        return;
    }

    // Determine current worldspace of the local player and the map's display world (root ancestor)
    TESWorldSpace* pMyWs = nullptr;
    if (auto* pPc = PlayerCharacter::Get())
        pMyWs = pPc->GetWorldSpace();
    TESWorldSpace* pDispWs = WorldMapProjector::GetDisplayWorld(pMyWs);

    if (!pDispWs && m_lastValidDisplayWorldId != 0)
    {
        if (auto* pForm = TESForm::GetById(m_lastValidDisplayWorldId))
            pDispWs = Cast<TESWorldSpace>(pForm);
    }

    if (pDispWs)
        m_lastValidDisplayWorldId = pDispWs->formID;

    const uint32_t dispWsId = pDispWs ? pDispWs->formID : 0u;

    // Screen size
    auto* pRenderer = BGSRenderer::Get();
    const float width = pRenderer ? static_cast<float>(pRenderer->windowWidth) : 0.f;
    const float height = pRenderer ? static_cast<float>(pRenderer->windowHeight) : 0.f;

    auto* pCam = PlayerCamera::Get();
    if (!pCam || width <= 0.f || height <= 0.f)
    {
        // If we can't project, don't draw anything
        m_world.GetOverlayService().SetPartyPinsJson("[]");
        return;
    }

    // If not in a party or only ourselves, don't draw anything
    const auto& members = partyService.GetPartyMembers();
    if (!partyService.IsInParty() || members.size() <= 1)
    {
        m_world.GetOverlayService().SetPartyPinsJson("[]");
        return;
    }

    const uint32_t localId = m_world.GetTransport().GetLocalPlayerId();

    std::ostringstream os;
    os.setf(std::ios::fixed); os << std::setprecision(1);
    os << "[";
    bool first = true;
    const auto sampleNow = std::chrono::steady_clock::now();

    const auto& playerInfoMap = partyService.GetPlayers();

    for (uint32_t pid : members)
    {
        if (pid == localId)
            continue; // don't display ourselves on the map

        const auto itPos = m_last.find(pid);
        const auto itWs = m_worlds.find(pid);
        if (itPos == m_last.end())
            continue; // no position at all yet

        const bool hasWorld = (itWs != m_worlds.end()) && itWs->second.HasWorld && itWs->second.WorldSpaceFormId != 0;
        const bool isInterior = (itWs != m_worlds.end()) && itWs->second.IsInterior;
        float sx = 0.f, sy = 0.f;
        bool drew = false;
        const auto& lastInfo = itPos->second;

        glm::vec3 worldPos = lastInfo.Pos;
        float dtSeconds = 0.0f;
        if (tick > lastInfo.Tick)
            dtSeconds = static_cast<float>(tick - lastInfo.Tick) / 1000.0f;

        if (lastInfo.SampleTime.time_since_epoch().count() != 0)
        {
            const float realDt = std::chrono::duration_cast<std::chrono::duration<float>>(sampleNow - lastInfo.SampleTime).count();
            dtSeconds = std::max(dtSeconds, realDt);
        }

        constexpr float cMaxPredictionSeconds = 1.0f;
        dtSeconds = std::clamp(dtSeconds, 0.0f, cMaxPredictionSeconds);
        const bool usingPrediction = dtSeconds > 0.0f && glm::length2(lastInfo.Velocity) > std::numeric_limits<float>::epsilon();
        if (usingPrediction)
            worldPos += lastInfo.Velocity * dtSeconds;

        // 1) If member already in display worldspace, project directly
        if (hasWorld && itWs->second.WorldSpaceFormId == dispWsId)
        {
            NiPoint3 wpos(worldPos);
            NiPoint3 spos{};
            if (HUDMenuUtils::WorldPtToScreenPt3(wpos, spos))
            {
                sx = std::clamp(spos.x, 0.0f, 1.0f) * width;
                sy = (1.f - std::clamp(spos.y, 0.0f, 1.0f)) * height;
                drew = true;
                if (dispWsId != 0)
                    m_lastPerWorld[pid][dispWsId] = worldPos;
            }
        }

        // 2) Otherwise, convert into display world via WRLD ONAM (child->ancestor only)
        if (!drew && hasWorld && dispWsId != 0 && itWs->second.WorldSpaceFormId != dispWsId && pDispWs)
        {
            if (auto* pFromWs = static_cast<TESWorldSpace*>(TESForm::GetById(itWs->second.WorldSpaceFormId)))
            {
                glm::vec3 dstPos{};
                if (WorldMapProjector::Convert(pFromWs, worldPos, pDispWs, dstPos))
                {
                    NiPoint3 wpos(dstPos);
                    NiPoint3 spos{};
                    if (HUDMenuUtils::WorldPtToScreenPt3(wpos, spos))
                    {
                        sx = std::clamp(spos.x, 0.0f, 1.0f) * width;
                        sy = (1.f - std::clamp(spos.y, 0.0f, 1.0f)) * height;
                        drew = true;
                        if (dispWsId != 0)
                            m_lastPerWorld[pid][dispWsId] = dstPos;
                    }
                }
            }
        }

        // 3) Approximation using per-player anchors (convert into display world)
        if (!drew && hasWorld && dispWsId != 0 && itWs->second.WorldSpaceFormId != dispWsId)
        {
            glm::vec3 dstPos{};
            if (ComputeCrossWorldApprox(pid, itWs->second.WorldSpaceFormId, worldPos, dispWsId, dstPos))
            {
                NiPoint3 wpos(dstPos);
                NiPoint3 spos{};
                if (HUDMenuUtils::WorldPtToScreenPt3(wpos, spos))
                {
                    sx = std::clamp(spos.x, 0.0f, 1.0f) * width;
                    sy = (1.f - std::clamp(spos.y, 0.0f, 1.0f)) * height;
                    drew = true;
                    if (dispWsId != 0)
                        m_lastPerWorld[pid][dispWsId] = dstPos;
                }
            }
        }

        // 4) Fallback to last known position already in display world
        if (!drew)
        {
            auto itHistPid = m_lastPerWorld.find(pid);
            if (itHistPid != m_lastPerWorld.end())
            {
                auto itDispPos = itHistPid->second.find(dispWsId);
                if (itDispPos != itHistPid->second.end())
                {
                    const auto& p = itDispPos->second;
                    NiPoint3 wpos(p);
                    NiPoint3 spos{};
                    if (HUDMenuUtils::WorldPtToScreenPt3(wpos, spos))
                    {
                        sx = std::clamp(spos.x, 0.0f, 1.0f) * width;
                        sy = (1.f - std::clamp(spos.y, 0.0f, 1.0f)) * height;
                        drew = true;
                    }
                }
            }
        }

        // 5) Retain last projected screen position. If a player is currently in an
        //    interior (no valid worldspace) keep showing their last known map spot
        //    indefinitely so they don't vanish from the world map.
        if (!drew)
        {
            constexpr uint64_t cKeepMs = 3000; // 3 seconds
            auto itLastScr = m_lastScreen.find(pid);
            const bool allowStaleFallback = !hasWorld || isInterior;
            if (itLastScr != m_lastScreen.end())
            {
                const uint64_t age = tick >= itLastScr->second.Tick ? (tick - itLastScr->second.Tick) : 0;
                if (allowStaleFallback || age <= cKeepMs)
                {
                    sx = itLastScr->second.sx;
                    sy = itLastScr->second.sy;
                    drew = true;
                }
            }
        }

        if (!drew)
            continue; // nothing to draw for this member on this map

        // Smooth towards new projected position using an exponential moving average
        auto itLS = m_lastScreen.find(pid);
        if (itLS != m_lastScreen.end())
        {
            const uint64_t dt = tick >= itLS->second.Tick ? (tick - itLS->second.Tick) : 0;
            float alpha = 1.0f;
            if (dt > 0)
            {
                constexpr float cSmoothingMs = 45.0f;
                alpha = 1.0f - std::exp(-static_cast<float>(dt) / cSmoothingMs);
                alpha = std::clamp(alpha, 0.7f, 1.0f);
            }

            const float dx = sx - itLS->second.sx;
            const float dy = sy - itLS->second.sy;
            constexpr float cTeleportPixelsSq = 600.0f * 600.0f;
            if ((dx * dx + dy * dy) > cTeleportPixelsSq)
                alpha = 1.0f;
            else if (usingPrediction && dtSeconds >= 0.2f)
                alpha = 1.0f;

            sx = itLS->second.sx + dx * alpha;
            sy = itLS->second.sy + dy * alpha;
        }

        // Cache last projected (smoothed) position
        m_lastScreen[pid] = LastScreen{sx, sy, tick};

        const auto itInfo = playerInfoMap.find(pid);
        const auto* pInfo = itInfo != playerInfoMap.end() ? &itInfo->second : nullptr;
        const std::string nameEscaped = pInfo ? JsonEscapeString(pInfo->Name) : std::string{};
        const std::string avatarEscaped = pInfo ? JsonEscapeString(pInfo->Avatar) : std::string{};
        const bool isOutOfBounds = !hasWorld || isInterior;

        if (!first) os << ",";
        first = false;
        os << "{\"x\":" << sx << ",\"y\":" << sy << ",\"id\":" << pid
           << ",\"oob\":" << (isOutOfBounds ? "true" : "false")
           << ",\"name\":\"" << nameEscaped << "\",\"avatar\":\"" << avatarEscaped << "\"}";
    }

    // If nothing to draw, send empty list
    if (first)
    {
        m_world.GetOverlayService().SetPartyPinsJson("[]");
        return;
    }

    os << "]";
    m_world.GetOverlayService().SetPartyPinsJson(os.str());
}


bool PartyMapOverlayService::ComputeCrossWorldApprox(uint32_t aPlayerId, uint32_t aSrcWsId, const glm::vec3& aSrcPos,
                                                     uint32_t aDstWsId, glm::vec3& aOutDstPos) const noexcept
{
    auto itHistPid = m_lastPerWorld.find(aPlayerId);
    if (itHistPid == m_lastPerWorld.end())
        return false;

    const auto& perWorld = itHistPid->second;
    auto itSrc = perWorld.find(aSrcWsId);
    auto itDst = perWorld.find(aDstWsId);
    if (itSrc == perWorld.end() || itDst == perWorld.end())
        return false;

    const glm::vec3 srcAnchor = itSrc->second;
    const glm::vec3 dstAnchor = itDst->second;

    // Translation-only approximation: assumes no rotation/scale differences.
    aOutDstPos = dstAnchor + (aSrcPos - srcAnchor);
    return true;
}

void PartyMapOverlayService::SetWaypointFor(uint32_t aPlayerId) noexcept
{
    const auto itPos = m_last.find(aPlayerId);
    if (itPos == m_last.end())
        return;

    const auto itWs = m_worlds.find(aPlayerId);
    if (itWs == m_worlds.end() || !itWs->second.HasWorld || itWs->second.WorldSpaceFormId == 0)
        return; // cannot set map waypoint without a worldspace

    Vector3_NetQuantize pos{};
    pos.x = itPos->second.Pos.x;
    pos.y = itPos->second.Pos.y;
    pos.z = itPos->second.Pos.z;

    m_world.GetDispatcher().trigger(SetWaypointEvent(pos, itWs->second.WorldSpaceFormId));
}

void PartyMapOverlayService::StoreLastInfo(uint32_t aPlayerId, const glm::vec3& aPos, uint64_t aTick) noexcept
{
    constexpr float cMinDtSeconds = 0.001f;
    constexpr float cMaxDtSeconds = 6.0f;
    constexpr float cTeleportResetDistanceSq = 4096.0f * 4096.0f;
    constexpr float cMaxSpeed = 16000.0f; // units per second, avoids runaway extrapolation
    constexpr float cMaxSpeedSq = cMaxSpeed * cMaxSpeed;
    constexpr float cVelocityBlend = 0.25f; // keep some of the previous vector to reduce jitter

    const auto now = std::chrono::steady_clock::now();

    LastInfo info{};
    info.Pos = aPos;
    info.Tick = aTick;
    info.Velocity = {};
    info.SampleTime = now;

    auto itPrev = m_last.find(aPlayerId);
    if (itPrev != m_last.end())
    {
        const uint64_t tickDelta = (aTick > itPrev->second.Tick) ? (aTick - itPrev->second.Tick) : 0ull;
        float dtSeconds = tickDelta > 0 ? static_cast<float>(tickDelta) / 1000.0f : 0.0f;

        if (itPrev->second.SampleTime.time_since_epoch().count() != 0)
        {
            const float realDtSeconds = std::chrono::duration_cast<std::chrono::duration<float>>(now - itPrev->second.SampleTime).count();
            if (realDtSeconds >= cMinDtSeconds)
                dtSeconds = std::max(dtSeconds, realDtSeconds);
        }

        if (dtSeconds < cMinDtSeconds)
        {
            info.Velocity = itPrev->second.Velocity;
        }
        else
        {
            const glm::vec3 delta = aPos - itPrev->second.Pos;
            const float distSq = glm::length2(delta);

            if (dtSeconds <= cMaxDtSeconds && distSq <= cTeleportResetDistanceSq)
            {
                glm::vec3 newVelocity = delta / dtSeconds;
                if (glm::length2(newVelocity) > cMaxSpeedSq)
                {
                    newVelocity = {};
                }
                else if (glm::length2(itPrev->second.Velocity) > 0.0f)
                {
                    newVelocity = itPrev->second.Velocity * cVelocityBlend + newVelocity * (1.0f - cVelocityBlend);
                }

                info.Velocity = newVelocity;
            }
        }
    }

    m_last[aPlayerId] = info;
}
