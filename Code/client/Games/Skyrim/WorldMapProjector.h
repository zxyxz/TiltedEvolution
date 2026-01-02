#pragma once

#include <glm/glm.hpp>

struct TESWorldSpace;

// WorldMapProjector
//
// Provides conversion of a 3D point from one TESWorldSpace to another (typically to Tamriel)
// for use with the world map projection. Uses ESM WRLD ONAM parent links; no runtime addresses.
struct WorldMapProjector
{
    // Kick off asynchronous preload of WRLD ONAM data.
    static void WarmupAsync() noexcept;

    // Convert a position from source worldspace to destination worldspace.
    // Returns true on success and writes out position in destination worldspace coordinates.
    static bool Convert(TESWorldSpace* apFromWs,
                        const glm::vec3& aFromPos,
                        TESWorldSpace* apToWs,
                        glm::vec3& aOutToPos) noexcept;

    // Returns the root ancestor worldspace to use for world map display (e.g., Tamriel).
    // If no parent chain is known, returns the input worldspace.
    static TESWorldSpace* GetDisplayWorld(TESWorldSpace* apWs) noexcept;

private:
    // Tries to use runtime addresses (currently disabled).
    static bool TryExactViaAddressLib(TESWorldSpace* apFromWs,
                                      const glm::vec3& aFromPos,
                                      TESWorldSpace* apToWs,
                                      glm::vec3& aOutToPos) noexcept;
};
