#include <TiltedOnlinePCH.h>

#include <Games/SaveGameUtils.h>

#include <Games/Skyrim/Misc/BSFixedString.h>
#include <Games/Skyrim/Misc/BSSaveDataSystemUtility.h>

#include <filesystem>

namespace
{
struct BGSSaveLoadManagerLite
{
    uint8_t pad138[0x138];
    char lastFileFullName[0x104];
    uint32_t pad23C;
    BSFixedString lastFileName;
};

static_assert(offsetof(BGSSaveLoadManagerLite, lastFileFullName) == 0x138);
static_assert(offsetof(BGSSaveLoadManagerLite, lastFileName) == 0x240);

BGSSaveLoadManagerLite* GetSaveLoadManager() noexcept
{
    POINTER_SKYRIMSE(BGSSaveLoadManagerLite*, s_singleton, 403340);
    auto** ppManager = s_singleton.Get();
    if (!ppManager)
        return nullptr;
    return *ppManager;
}

BSWin32SaveDataSystemUtility* GetSaveDataSystemUtility() noexcept
{
    return BSWin32SaveDataSystemUtility::GetSingleton();
}
} // namespace

namespace SaveGameUtils
{
std::string GetCurrentSaveName() noexcept
{
    auto* pManager = GetSaveLoadManager();
    if (!pManager)
        return {};

    if (pManager->lastFileName.data && pManager->lastFileName.data[0] != '\0')
        return pManager->lastFileName.data;

    if (pManager->lastFileFullName[0] == '\0')
        return {};

    std::filesystem::path path(pManager->lastFileFullName);
    return path.stem().string();
}

std::filesystem::path GetCurrentSavePath() noexcept
{
    auto* pManager = GetSaveLoadManager();
    if (!pManager)
        return {};

    if (pManager->lastFileFullName[0] != '\0')
    {
        std::filesystem::path fullPath(pManager->lastFileFullName);
        if (fullPath.has_parent_path() || fullPath.has_root_path())
            return fullPath;
    }

    const char* fileName = nullptr;
    if (pManager->lastFileName.data && pManager->lastFileName.data[0] != '\0')
        fileName = pManager->lastFileName.data;
    else if (pManager->lastFileFullName[0] != '\0')
        fileName = pManager->lastFileFullName;

    if (!fileName || fileName[0] == '\0')
        return {};

    auto* pSaveUtil = GetSaveDataSystemUtility();
    if (!pSaveUtil)
        return {};

    char resolvedPath[0x104]{};
    if (pSaveUtil->PrepareFileSavePath(fileName, resolvedPath, false, false) != 0)
        return {};

    if (resolvedPath[0] == '\0')
        return {};

    return std::filesystem::path(resolvedPath);
}
} // namespace SaveGameUtils
