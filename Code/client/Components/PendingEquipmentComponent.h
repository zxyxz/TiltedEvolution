#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <TiltedCore/Stl.hpp>
#include <Messages/NotifyEquipmentChanges.h>

struct PendingEquipmentComponent
{
    TiltedPhoques::Vector<NotifyEquipmentChanges> PendingChanges;
};
