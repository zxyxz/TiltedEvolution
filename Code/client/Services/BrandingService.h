#pragma once

#include <entt/entt.hpp>

struct World;

// Draws a simple message on the Skyrim main menu indicating the mod is loaded.
struct BrandingService
{
    BrandingService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~BrandingService() = default;

    TP_NOCOPYMOVE(BrandingService);

private:
    void OnDraw() noexcept;

    World& m_world;

    // connections
    entt::scoped_connection m_drawImGuiConnection;
};

