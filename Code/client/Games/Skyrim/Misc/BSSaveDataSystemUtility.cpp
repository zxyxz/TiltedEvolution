#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/Misc/BSSaveDataSystemUtility.h>

#include <VersionDb.h>

BSWin32SaveDataSystemUtility* BSWin32SaveDataSystemUtility::GetSingleton() noexcept
{
    using TGetSingleton = BSWin32SaveDataSystemUtility* (*)();
    const VersionDbPtr<void> getSingleton(109278);
    auto* func = reinterpret_cast<TGetSingleton>(getSingleton.GetPtr());
    return func ? func() : nullptr;
}
