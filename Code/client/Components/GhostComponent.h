#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

struct GhostComponent
{
    explicit GhostComponent(bool aIsGhost = false)
        : IsGhost(aIsGhost)
    {
    }

    bool IsGhost;
};
