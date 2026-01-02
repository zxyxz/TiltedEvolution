#pragma once

#include <Services/Generic/DropStorage.h>
#include <Games/Events.h>

#include <entt/entt.hpp>

#include <filesystem>

struct TransportService;
struct World;

class CoSaveService final : public BSTEventSink<TESLoadGameEvent>
{
public:
    CoSaveService(World& aWorld, entt::dispatcher& aDispatcher, TransportService& aTransport) noexcept;
    ~CoSaveService() override;

    TP_NOCOPYMOVE(CoSaveService);

    DropStorage& GetDropStorage() noexcept { return m_dropStorage; }
    const DropStorage& GetDropStorage() const noexcept { return m_dropStorage; }

    void UpdateLocalPlayerLocation(const CoSaveStorage::LocalPlayerLocation& aLocation) noexcept;
    std::optional<CoSaveStorage::LocalPlayerLocation> GetLocalPlayerLocation() const noexcept;

    void OnSaveGame(const char* apFileName) noexcept;
    void PrepareForUser(const std::string& aUsername) noexcept;

    BSTEventResult OnEvent(const TESLoadGameEvent* apEvent, const EventDispatcher<TESLoadGameEvent>* apSender) override;

private:
    void LoadFromCurrentSave() noexcept;
    void SaveToPath(const std::filesystem::path& aSavePath) noexcept;

    World& m_world;
    TransportService& m_transport;
    DropStorage m_dropStorage;
    std::string m_cachedUsername;
};
