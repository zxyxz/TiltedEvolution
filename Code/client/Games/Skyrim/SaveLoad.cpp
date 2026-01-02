#include <TiltedOnlinePCH.h>

#include <SaveLoad.h>
#include <World.h>
#include <Services/Generic/CoSaveService.h>

void BGSSaveLoadManager::Save(SaveData* apData)
{
    apData->flags |= 4;

    const char* cSaveName = "";
    if (apData->saveName)
        cSaveName = apData->saveName;
}

TP_THIS_FUNCTION(TBGSSaveLoadManager_SaveImpl, bool, BGSSaveLoadManager, int32_t, uint32_t, const char*);
static TBGSSaveLoadManager_SaveImpl* RealSaveImpl = nullptr;

bool TP_MAKE_THISCALL(HookSaveImpl, BGSSaveLoadManager, int32_t aDeviceId, uint32_t aOutputStats, const char* apFileName)
{
    spdlog::info("SaveLoad: Save_Impl called (file='{}')", apFileName ? apFileName : "");
    const bool result = TiltedPhoques::ThisCall(RealSaveImpl, apThis, aDeviceId, aOutputStats, apFileName);

    if (result)
    {
        auto& world = World::Get();
        if (world.ctx().contains<CoSaveService>())
            world.ctx().at<CoSaveService>().OnSaveGame(apFileName);
    }

    return result;
}

static TiltedPhoques::Initializer s_saveLoadManagerHooks(
    []()
    {
        POINTER_SKYRIMSE(TBGSSaveLoadManager_SaveImpl, s_save, 35727);
        RealSaveImpl = s_save.Get();
        if (RealSaveImpl)
        {
            spdlog::info("SaveLoad: Save_Impl hook installed");
            TP_HOOK(&RealSaveImpl, HookSaveImpl);
        }
        else
        {
            spdlog::warn("SaveLoad: Save_Impl hook missing");
        }
    });
