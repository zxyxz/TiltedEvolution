#include <TiltedOnlinePCH.h>

#include <Games/Skyrim/WorldMapProjector.h>
#include <Forms/TESWorldSpace.h>
#include <Games/Skyrim/Forms/TESForm.h>

#include <components/es_loader/ESLoader.h>
#include <components/es_loader/RecordCollection.h>
#include <components/es_loader/Records/WRLD.h>
#include <atomic>
#include <thread>

namespace {
struct MapLink {
    uint32_t parentId{0};
    float xOff{0.f};
    float yOff{0.f};
    float zOff{0.f};
};

static TiltedPhoques::Map<uint32_t, MapLink> g_links; // worldId -> mapping to its parent
static std::atomic<bool> g_linksReady{false};
static std::atomic<bool> g_linksLoading{false};

static void StartAsyncLoad() noexcept
{
    std::thread([]() noexcept {
        TiltedPhoques::Map<uint32_t, MapLink> links;
        bool success = false;

        try
        {
            ESLoader::ESLoader loader;
            auto records = loader.BuildRecordCollection();
            if (records && records->HasAnyRecords())
            {
                for (const auto& [fid, wrld] : records->GetWorlds())
                {
                    MapLink link{};
                    if (wrld.m_parentId)
                        link.parentId = *wrld.m_parentId;
                    if (wrld.m_onam)
                    {
                        link.xOff = wrld.m_onam->m_cellOffsetX4096;
                        link.yOff = wrld.m_onam->m_cellOffsetY4096;
                        link.zOff = wrld.m_onam->m_cellOffsetZ4096;
                    }
                    if (link.parentId != 0)
                        links[fid] = link;
                }
                success = true;
            }
        }
        catch (const std::exception& e)
        {
            spdlog::warn("WorldMapProjector: failed to build world links ({})", e.what());
        }
        catch (...)
        {
            spdlog::warn("WorldMapProjector: failed to build world links (unknown error)");
        }

        if (success)
        {
            g_links = std::move(links);
            g_linksReady.store(true, std::memory_order_release);
        }

        g_linksLoading.store(false, std::memory_order_release);
    }).detach();
}

static void EnsureLoaded() noexcept
{
    if (g_linksReady.load(std::memory_order_acquire))
        return;

    bool expected = false;
    if (g_linksLoading.compare_exchange_strong(expected, true))
        StartAsyncLoad();
}

// Walk up from child world to target parent, accumulating offsets.
static bool ToAncestor(uint32_t fromId, uint32_t targetAncestor, glm::vec3 srcPos, glm::vec3& outPos) noexcept {
    EnsureLoaded();
    if (!g_linksReady.load(std::memory_order_acquire))
        return false;
    if (fromId == 0 || targetAncestor == 0)
        return false;
    if (fromId == targetAncestor)
    {
        outPos = srcPos;
        return true;
    }

    glm::vec3 p = srcPos;
    uint32_t cur = fromId;
    // Guard against cycles / very deep chains
    for (int i = 0; i < 16; ++i)
    {
        auto it = g_links.find(cur);
        if (it == g_links.end())
            return false;
        const auto& link = it->second;
        // Apply offset to go into parent coordinates
        p.x += link.xOff;
        p.y += link.yOff;
        p.z += link.zOff;
        if (link.parentId == targetAncestor)
        {
            outPos = p;
            return true;
        }
        cur = link.parentId;
    }
    return false;
}

static uint32_t RootAncestorId(uint32_t wsId) noexcept
{
    EnsureLoaded();
    if (!g_linksReady.load(std::memory_order_acquire))
        return wsId;
    uint32_t cur = wsId;
    for (int i = 0; i < 16; ++i)
    {
        auto it = g_links.find(cur);
        if (it == g_links.end() || it->second.parentId == 0)
            return cur;
        cur = it->second.parentId;
    }
    return cur;
}
} // namespace

void WorldMapProjector::WarmupAsync() noexcept
{
    EnsureLoaded();
}

bool WorldMapProjector::Convert(TESWorldSpace* apFromWs,
                                const glm::vec3& aFromPos,
                                TESWorldSpace* apToWs,
                                glm::vec3& aOutToPos) noexcept
{
    if (!apFromWs || !apToWs)
        return false;

    const uint32_t fromId = apFromWs->formID;
    const uint32_t toId = apToWs->formID;

    if (fromId == toId)
    {
        aOutToPos = aFromPos;
        return true;
    }

    // Only support mapping up the parent chain (child -> parent -> ...)
    if (ToAncestor(fromId, toId, aFromPos, aOutToPos))
        return true;

    return false;
}

TESWorldSpace* WorldMapProjector::GetDisplayWorld(TESWorldSpace* apWs) noexcept
{
    if (!apWs)
        return nullptr;
    const uint32_t rootId = RootAncestorId(apWs->formID);
    if (rootId == apWs->formID)
        return apWs;
    auto* pForm = TESForm::GetById(rootId);
    return pForm ? static_cast<TESWorldSpace*>(pForm) : apWs;
}
