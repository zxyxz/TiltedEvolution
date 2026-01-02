#include <TiltedOnlinePCH.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PartyJoinedEvent.h>
#include <Events/PartyLeftEvent.h>

#include <Services/PartyMarkerOverlayService.h>

#include <Services/ImguiService.h>
#include <Services/PartyService.h>

#include <Components.h>
#include <World.h>

#include <PlayerCharacter.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>


static float NormalizeAngle(float a) noexcept
{
    const float pi = static_cast<float>(TiltedPhoques::Pi);
    const float twoPi = 2.f * pi;
    while (a > pi)
        a -= twoPi;
    while (a < -pi)
        a += twoPi;
    return a;
}

PartyMarkerOverlayService::PartyMarkerOverlayService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    // Update cache of last-known positions each frame
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&PartyMarkerOverlayService::OnUpdate>(this);

    // Draw markers using ImGui each frame
    auto& imgui = m_world.ctx().at<ImguiService>();
    m_drawImGuiConnection = imgui.OnDraw.connect<&PartyMarkerOverlayService::OnDraw>(this);

    // Clear on disconnect/party changes
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&PartyMarkerOverlayService::OnDisconnected>(this);
    m_partyJoinedConnection = aDispatcher.sink<PartyJoinedEvent>().connect<&PartyMarkerOverlayService::OnPartyJoined>(this);
    m_partyLeftConnection = aDispatcher.sink<PartyLeftEvent>().connect<&PartyMarkerOverlayService::OnPartyLeft>(this);
}

void PartyMarkerOverlayService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_last.clear();
}

void PartyMarkerOverlayService::OnPartyJoined(const PartyJoinedEvent&) noexcept
{
    // ensure we only keep entries for current party members
    PruneNonPartyEntries();
}

void PartyMarkerOverlayService::OnPartyLeft(const PartyLeftEvent&) noexcept
{
    m_last.clear();
}

void PartyMarkerOverlayService::PruneNonPartyEntries() noexcept
{
    const auto& members = m_world.GetPartyService().GetPartyMembers();
    TiltedPhoques::Vector<uint32_t> toErase;
    toErase.reserve(m_last.size());

    for (const auto& [pid, _] : m_last)
    {
        if (std::find(members.begin(), members.end(), pid) == members.end())
            toErase.push_back(pid);
    }

    for (auto id : toErase)
        m_last.erase(id);
}

bool PartyMarkerOverlayService::HasLiveEntityForPlayer(uint32_t aPlayerId) const noexcept
{
    auto view = m_world.view<PlayerComponent>();
    for (auto e : view)
    {
        if (view.get<PlayerComponent>(e).Id == aPlayerId)
            return true; // entity with PlayerComponent exists -> considered loaded
    }
    return false;
}

void PartyMarkerOverlayService::OnUpdate(const UpdateEvent&) noexcept
{
    if (!m_world.GetPartyService().IsInParty())
        return;

    const uint64_t tick = m_world.GetTick();

    // Cache last known positions from live entities
    auto view = m_world.view<PlayerComponent, InterpolationComponent>();
    for (auto e : view)
    {
        const auto& pc = view.get<PlayerComponent>(e);
        const auto& interp = view.get<InterpolationComponent>(e);
        m_last[pc.Id] = LastInfo{interp.Position, tick};
    }

    // Drop very old entries (> 5 minutes) to avoid stale clutter
    const uint64_t maxAge = 5ull * 60ull * 1000ull; // in ms if ticks are ms; otherwise harmless large window
    for (auto it = m_last.begin(); it != m_last.end(); )
    {
        if (tick - it->second.Tick > maxAge)
            it = m_last.erase(it);
        else
            ++it;
    }
}

void PartyMarkerOverlayService::OnDraw() noexcept
{
    // Disabled per user request: remove debug/marker UI when players are in different cells
    return;
}

