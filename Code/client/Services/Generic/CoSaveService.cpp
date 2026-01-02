#include <Services/Generic/CoSaveService.h>

#include <Games/SaveGameUtils.h>
#include <Services/TransportService.h>
#include <World.h>

#include <spdlog/spdlog.h>

namespace
{
std::filesystem::path ResolveCoSavePath(const std::filesystem::path& aSavePath) noexcept
{
    if (aSavePath.empty())
        return {};

    std::filesystem::path coSavePath = aSavePath;
    coSavePath.replace_extension("tilted");
    return coSavePath;
}
} // namespace

CoSaveService::CoSaveService(World& aWorld, entt::dispatcher&, TransportService& aTransport) noexcept
    : m_world(aWorld)
    , m_transport(aTransport)
{
    if (auto* pDispatcher = EventDispatcherManager::Get())
        pDispatcher->loadGameEvent.RegisterSink(this);
}

CoSaveService::~CoSaveService()
{
    if (auto* pDispatcher = EventDispatcherManager::Get())
        pDispatcher->loadGameEvent.UnRegisterSink(this);
}

void CoSaveService::PrepareForUser(const std::string& aUsername) noexcept
{
    const std::string username = aUsername.empty() ? "default" : aUsername;
    if (username != m_cachedUsername)
    {
        m_cachedUsername = username;
        m_dropStorage.SetActiveUser(username);
    }
}

void CoSaveService::UpdateLocalPlayerLocation(const CoSaveStorage::LocalPlayerLocation& aLocation) noexcept
{
    m_dropStorage.SetLocalPlayerLocation(aLocation);
}

std::optional<CoSaveStorage::LocalPlayerLocation> CoSaveService::GetLocalPlayerLocation() const noexcept
{
    return m_dropStorage.GetLocalPlayerLocation();
}

BSTEventResult CoSaveService::OnEvent(const TESLoadGameEvent*, const EventDispatcher<TESLoadGameEvent>*)
{
    spdlog::info("CoSaveService: LoadGame event received");
    LoadFromCurrentSave();
    return BSTEventResult::kOk;
}

void CoSaveService::LoadFromCurrentSave() noexcept
{
    m_dropStorage.OnLoadGameReset();
    PrepareForUser(m_transport.GetLoginUsername());

    const auto savePath = SaveGameUtils::GetCurrentSavePath();
    if (savePath.empty())
        return;

    const auto coSavePath = ResolveCoSavePath(savePath);
    if (coSavePath.empty())
        return;

    m_dropStorage.LoadFromPath(coSavePath);
}

void CoSaveService::OnSaveGame(const char* apFileName) noexcept
{
    spdlog::info("CoSaveService: OnSaveGame called (file='{}')", apFileName ? apFileName : "");
    PrepareForUser(m_transport.GetLoginUsername());

    if (!apFileName || apFileName[0] == '\0')
    {
        const auto fallbackPath = SaveGameUtils::GetCurrentSavePath();
        if (!fallbackPath.empty())
            SaveToPath(fallbackPath);
        return;
    }

    const std::filesystem::path savePath(apFileName);
    if (savePath.has_parent_path() || savePath.has_root_path())
    {
        SaveToPath(savePath);
        return;
    }

    const auto fallbackPath = SaveGameUtils::GetCurrentSavePath();
    if (!fallbackPath.empty())
        SaveToPath(fallbackPath);
}

void CoSaveService::SaveToPath(const std::filesystem::path& aSavePath) noexcept
{
    const auto coSavePath = ResolveCoSavePath(aSavePath);
    if (coSavePath.empty())
        return;

    m_dropStorage.SaveToPath(coSavePath);
}
