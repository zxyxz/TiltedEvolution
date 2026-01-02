#pragma once

#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include <entt/entt.hpp>
#include <imgui.h>

#include <Events/UpdateEvent.h>

struct World;
struct PlayerCharacter;
struct Actor;
struct NiPoint3;
struct RenderSystemD3D11;

/**
 * @brief Draws 3D-aware name tags above remote players.
 *
 * The service is intentionally data-driven via Style so that future UI/UX
 * customisation can plug into it without having to touch the rendering logic.
 */
struct NameTagService
{
    enum class Mode : uint8_t
    {
        Detailed = 0,
        Basic = 1,
        Hidden = 2,
        Normal = 3
    };

    struct Style
    {
        ImVec4 BackgroundColor{0.f, 0.f, 0.f, 0.65f};
        ImVec4 BorderColor{1.f, 1.f, 1.f, 0.35f};
        ImVec4 TextColor{0.95f, 0.95f, 0.95f, 1.f};
        ImVec4 LevelTextColor{0.85f, 0.92f, 1.f, 0.95f};
        ImVec4 AccentColor{0.42f, 0.65f, 1.f, 0.9f};
        ImVec4 AvatarRingColor{1.f, 1.f, 1.f, 0.45f};
        ImVec4 PlaceholderAvatarColor{0.35f, 0.45f, 0.65f, 0.7f};
        float VerticalOffset = 24.f;
        float ForwardOffset = 10.f;
        float MinScale = 0.55f;
        float MaxScale = 1.1f;
        float ScaleReferenceDistance = 450.f;
        float ScaleFalloff = 1.2f;
        float DepthPerspectiveBoost = 0.25f;
        float DepthNear = 0.15f;
        float DepthFar = 0.9f;
        float MaxDistance = 3500.f;
        float FadeDistance = 2500.f;
        float PaddingX = 14.f;
        float PaddingY = 6.f;
        float CornerRounding = 6.f;
        float LevelFontScale = 0.75f;
        float NameLevelSpacing = 4.f;
        float AvatarSize = 38.f;
        float CompactAvatarScale = 0.84f;
        float AvatarSpacing = 10.f;
        float AccentThickness = 2.5f;
        float VisibilityEpsilon = 1e-3f;
        float NearBypassDistance = 600.f;
        float AvatarRetryDelayMs = 3500.f;
        uint32_t AvatarMaxBytes = 256u * 1024u;
    };

    NameTagService(World& aWorld, [[maybe_unused]] entt::dispatcher& aDispatcher) noexcept;
    ~NameTagService() noexcept = default;

    TP_NOCOPYMOVE(NameTagService);

    [[nodiscard]] Style& GetStyle() noexcept { return m_style; }
    void SetStyle(const Style& aStyle) noexcept { m_style = aStyle; }
    [[nodiscard]] Mode GetMode() const noexcept { return m_mode; }
    void SetMode(Mode aMode) noexcept;

private:
    struct VisibilityInfo
    {
        bool Visible = true;
        uint64_t Tick = 0;
    };

    struct AvatarTexture
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Texture;
        ImVec2 Size{0.f, 0.f};
        std::string Signature;
        uint64_t LastAttemptTick = 0;
        uint64_t LastSeenTick = 0;
        bool Failed = false;
    };

    void OnDraw() noexcept;
    void OnUpdate(const UpdateEvent& acEvent) noexcept;

    [[nodiscard]] bool ShouldRenderTag(uint32_t aPlayerId) const noexcept;
    [[nodiscard]] bool ComputeLineOfSight(PlayerCharacter* apLocalPlayer, Actor* apRemoteActor) const noexcept;
    [[nodiscard]] bool ProjectWorldPoint(const NiPoint3& aWorldPoint, ImVec2& aScreenPos, float& aDepth, const ImVec2& aViewport) const noexcept;
    [[nodiscard]] NiPoint3 BuildAnchorPoint(Actor* apActor) const noexcept;
    [[nodiscard]] static ImU32 ColorWithAlpha(const ImVec4& aColor, float aAlpha) noexcept;
    void GarbageCollectAvatarCache(uint64_t aNowTick) noexcept;
    [[nodiscard]] const AvatarTexture* ResolveAvatarTexture(uint32_t aPlayerId, std::string_view aAvatarData, uint64_t aNowTick);
    [[nodiscard]] bool DecodeAvatarData(std::string_view aAvatarData, std::vector<uint8_t>& aDecodedData) const;
    [[nodiscard]] bool CreateAvatarTexture(const std::vector<uint8_t>& aDecodedData, AvatarTexture& aEntry) noexcept;
    [[nodiscard]] ID3D11Device* AcquireD3DDevice() noexcept;

    World& m_world;
    Style m_style{};
    Mode m_mode = Mode::Normal;
    RenderSystemD3D11* m_renderSystem = nullptr;

    std::unordered_map<uint32_t, VisibilityInfo> m_visibility;
    std::unordered_map<uint32_t, AvatarTexture> m_avatarCache;
    Microsoft::WRL::ComPtr<ID3D11Device> m_cachedDevice;

    entt::scoped_connection m_drawConnection;
    entt::scoped_connection m_updateConnection;
};
