#include <TiltedOnlinePCH.h>

#include <Services/NameTagService.h>

#include <Services/ImguiService.h>
#include <Services/PartyService.h>
#include <Services/OverlayService.h>

#include <Systems/RenderSystemD3D11.h>

#include <Components.h>
#include <World.h>

#include <PlayerCharacter.h>
#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/Skyrim/Interface/Menus/HUDMenuUtils.h>
#include <Games/Skyrim/Interface/UI.h>

#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cfloat>
#include <limits>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <dxgi.h>
#include <wincodec.h>
#include <mutex>
#include <spdlog/spdlog.h>

#ifdef _MSC_VER
#pragma comment(lib, "windowscodecs.lib")
#endif

namespace
{
float Clamp01(float aValue) noexcept
{
    return std::clamp(aValue, 0.f, 1.f);
}

float SmoothStep(float aEdge0, float aEdge1, float aValue) noexcept
{
    if (aEdge0 == aEdge1)
        return aValue >= aEdge1 ? 1.f : 0.f;

    const float t = Clamp01((aValue - aEdge0) / (aEdge1 - aEdge0));
    return t * t * (3.f - 2.f * t);
}

constexpr std::array<int8_t, 256> BuildBase64Table() noexcept
{
    std::array<int8_t, 256> table{};
    table.fill(-1);

    for (int i = 0; i < 26; ++i)
    {
        table['A' + i] = static_cast<int8_t>(i);
        table['a' + i] = static_cast<int8_t>(i + 26);
    }

    for (int i = 0; i < 10; ++i)
        table['0' + i] = static_cast<int8_t>(52 + i);

    table[static_cast<size_t>('+')] = 62;
    table[static_cast<size_t>('/')] = 63;

    return table;
}

constexpr std::array<int8_t, 256> kBase64Table = BuildBase64Table();

bool DecodeBase64(std::string_view aInput, std::vector<uint8_t>& aOutput) noexcept
{
    int value = 0;
    int bits = -8;
    aOutput.clear();
    aOutput.reserve((aInput.size() * 3) / 4);

    for (unsigned char c : aInput)
    {
        if (c == '=')
            break;

        if (std::isspace(static_cast<int>(c)))
            continue;

        const int8_t decoded = kBase64Table[c];
        if (decoded < 0)
            continue;

        value = (value << 6) + decoded;
        bits += 6;

        if (bits >= 0)
        {
            aOutput.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }

    return !aOutput.empty();
}

IWICImagingFactory* GetWicFactory() noexcept
{
    static Microsoft::WRL::ComPtr<IWICImagingFactory> s_factory;
    static std::once_flag s_factoryOnce;
    static HRESULT s_factoryResult = E_FAIL;

    std::call_once(s_factoryOnce, []() {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_factory));
        if (hr == CO_E_NOTINITIALIZED)
        {
            hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE)
                hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_factory));
        }
        s_factoryResult = hr;
    });

    return SUCCEEDED(s_factoryResult) ? s_factory.Get() : nullptr;
}
} // namespace

NameTagService::NameTagService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    auto& imgui = m_world.ctx().at<ImguiService>();
    m_drawConnection = imgui.OnDraw.connect<&NameTagService::OnDraw>(this);
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&NameTagService::OnUpdate>(this);

    if (m_world.ctx().contains<RenderSystemD3D11>())
        m_renderSystem = &m_world.ctx().at<RenderSystemD3D11>();
}

void NameTagService::OnDraw() noexcept
{
    auto* pLocalPlayer = PlayerCharacter::Get();
    if (!pLocalPlayer)
        return;

    if (auto* pUI = UI::Get())
    {
        auto menuOpen = [pUI](const char* acName) {
            return acName && pUI->GetMenuOpen(BSFixedString(acName));
        };

        if (menuOpen("Dialogue Menu") || menuOpen("DialogueMenu") || menuOpen("InventoryMenu") || menuOpen("StatsMenu") ||
            menuOpen("SkillsMenu") || menuOpen("MagicMenu") || menuOpen("MapMenu") || menuOpen("TweenMenu") ||
            menuOpen("FavoritesMenu"))
        {
            return;
        }
    }

    const uint64_t nowTick = m_world.GetTick();
    GarbageCollectAvatarCache(nowTick);

    auto view = m_world.view<PlayerComponent, RemoteComponent, FormIdComponent>();
    if (view.begin() == view.end())
        return;

    const auto& playerDirectory = m_world.GetPartyService().GetPlayers();

    auto& imguiSvc = m_world.ctx().at<ImguiService>();
    ImFont* pFont = imguiSvc.GetSkyrimFont();

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
        return;

    if (pFont)
        ImGui::PushFont(pFont);

    const ImVec2 viewport = ImGui::GetIO().DisplaySize;
    const glm::vec3 localPos = {pLocalPlayer->position.x, pLocalPlayer->position.y, pLocalPlayer->position.z};
    ImFont* font = pFont ? pFont : ImGui::GetFont();
    if (!font)
    {
        if (pFont)
            ImGui::PopFont();
        return;
    }

    const float baseFontSize = font->FontSize;

    if (m_mode == Mode::Hidden)
    {
        if (pFont)
            ImGui::PopFont();
        return;
    }

    for (auto entity : view)
    {
        const auto& playerComponent = view.get<PlayerComponent>(entity);
        const auto& formComponent = view.get<FormIdComponent>(entity);

        Actor* pActor = Cast<Actor>(TESForm::GetById(formComponent.Id));
        if (!pActor)
            continue;

        const ActorExtension* pExtension = pActor->GetExtension();
        if (!pExtension || pExtension->IsLocalPlayer())
            continue;

        if (!ShouldRenderTag(playerComponent.Id))
            continue;

        auto nameIt = playerDirectory.find(playerComponent.Id);
        std::string fallbackName;
        std::string_view displayName;
        if (nameIt != playerDirectory.end() && !nameIt->second.Name.empty())
            displayName = nameIt->second.Name.c_str();
        else
        {
            fallbackName = fmt::format("#{}", playerComponent.Id);
            displayName = fallbackName;
        }

        const bool useCompactLayout = m_mode == Mode::Detailed;
        const bool showAvatar = (m_mode == Mode::Detailed || m_mode == Mode::Normal);
        const bool showLevel = (m_mode == Mode::Detailed || m_mode == Mode::Normal);

        std::string_view avatarData;
        if (showAvatar && nameIt != playerDirectory.end() && !nameIt->second.Avatar.empty())
            avatarData = std::string_view(nameIt->second.Avatar.c_str(), nameIt->second.Avatar.size());

        const NiPoint3 anchor = BuildAnchorPoint(pActor);

        ImVec2 screenPos;
        float depth = 0.f;
        if (!ProjectWorldPoint(anchor, screenPos, depth, viewport))
            continue;

        const glm::vec3 remotePos = {anchor.x, anchor.y, anchor.z};
        const float distance = glm::distance(localPos, remotePos);
        if (distance > m_style.MaxDistance)
            continue;

        const float reference = std::max(m_style.ScaleReferenceDistance, 1.f);
        const float normalizedDistance = reference / (reference + std::max(distance, 0.1f));
        const float falloffPower = std::max(m_style.ScaleFalloff, 0.05f);
        float scale = m_style.MinScale + (m_style.MaxScale - m_style.MinScale) * std::pow(normalizedDistance, falloffPower);
        scale = std::clamp(scale, m_style.MinScale, m_style.MaxScale);

        if (m_style.DepthFar > m_style.DepthNear)
        {
            const float depthFactor = 1.f - Clamp01((depth - m_style.DepthNear) / (m_style.DepthFar - m_style.DepthNear));
            scale = std::clamp(scale * (1.f + depthFactor * m_style.DepthPerspectiveBoost), m_style.MinScale, m_style.MaxScale);
        }

        float alpha = 1.f;
        if (distance > m_style.FadeDistance)
        {
            const float fadeSpan = std::max(m_style.MaxDistance - m_style.FadeDistance, 1.f);
            const float fadeT = Clamp01((distance - m_style.FadeDistance) / fadeSpan);
            alpha = 1.f - SmoothStep(0.f, 1.f, fadeT);
        }

        alpha *= Clamp01(1.f - std::max(depth - 0.95f, 0.f) * 4.f);
        if (alpha <= 0.01f)
            continue;

        const uint16_t level = pActor->GetLevel();
        const std::string levelText = level > 0 ? fmt::format("Lv. {}", level) : std::string("Lv. --");

        const float nameFontSize = baseFontSize * scale;
        const float levelFontSize = nameFontSize * m_style.LevelFontScale;
        const ImVec2 nameTextSize = font->CalcTextSizeA(nameFontSize, FLT_MAX, 0.f, displayName.data(), displayName.data() + displayName.size());
        const ImVec2 levelTextSize = font->CalcTextSizeA(levelFontSize, FLT_MAX, 0.f, levelText.c_str(), levelText.c_str() + levelText.size());

        const float paddingX = m_style.PaddingX * scale;
        const float paddingY = m_style.PaddingY * scale;
        const float baseAvatarSize = m_style.AvatarSize * scale;
        const float avatarSize = showAvatar ? baseAvatarSize * (useCompactLayout ? m_style.CompactAvatarScale : 1.f) : 0.f;
        const float avatarSpacing = m_style.AvatarSpacing * scale;
        const float nameLevelSpacing = m_style.NameLevelSpacing * scale;
        const float accentHeight = m_style.AccentThickness * scale;

        float textColumnWidth = nameTextSize.x;
        float textColumnHeight = nameTextSize.y;
        if (showLevel)
        {
            if (useCompactLayout)
            {
                textColumnWidth += nameLevelSpacing + levelTextSize.x;
                textColumnHeight = std::max(nameTextSize.y, levelTextSize.y);
            }
            else
            {
                textColumnWidth = std::max(nameTextSize.x, levelTextSize.x);
                textColumnHeight = nameTextSize.y + nameLevelSpacing + levelTextSize.y;
            }
        }

        const float avatarContribution = showAvatar ? (avatarSize + avatarSpacing) : 0.f;
        const float contentHeight = showAvatar ? std::max(textColumnHeight, avatarSize) : textColumnHeight;
        const float contentWidth = avatarContribution + textColumnWidth;
        const float totalWidth = contentWidth + paddingX * 2.f;
        const float totalHeight = contentHeight + paddingY * 2.f + accentHeight;

        ImVec2 topLeft(screenPos.x - totalWidth * 0.5f, screenPos.y - totalHeight * 0.5f);
        ImVec2 bottomRight(screenPos.x + totalWidth * 0.5f, screenPos.y + totalHeight * 0.5f);

        // Light 3D cue: slight horizontal offset based on how much the actor looks at the camera.
        const glm::vec3 actorForward = {std::sin(pActor->rotation.z), std::cos(pActor->rotation.z), 0.f};
        glm::vec3 toCameraVec = localPos - remotePos;
        if (glm::length2(toCameraVec) <= std::numeric_limits<float>::epsilon())
            toCameraVec = {0.f, 0.f, 1.f};
        const glm::vec3 toCamera = glm::normalize(toCameraVec);
        const glm::vec3 forwardNorm = glm::normalize(actorForward);
        const float facing = Clamp01((glm::dot(forwardNorm, toCamera) + 1.f) * 0.5f);
        const float skew = (1.f - facing) * (paddingX * 0.35f);
        topLeft.x -= skew;
        bottomRight.x -= skew;
        screenPos.x -= skew;

        const ImU32 bgColor = ColorWithAlpha(m_style.BackgroundColor, alpha);
        drawList->AddRectFilled(topLeft, bottomRight, bgColor, m_style.CornerRounding);

        const ImU32 borderColor = ColorWithAlpha(m_style.BorderColor, alpha);
        drawList->AddRect(topLeft, bottomRight, borderColor, m_style.CornerRounding);

        ImVec2 avatarMin{};
        ImVec2 avatarMax{};
        ImVec2 avatarCenter{};
        if (showAvatar)
        {
            avatarMin = ImVec2(topLeft.x + paddingX, topLeft.y + paddingY + (contentHeight - avatarSize) * 0.5f);
            avatarMax = ImVec2(avatarMin.x + avatarSize, avatarMin.y + avatarSize);
            avatarCenter = ImVec2((avatarMin.x + avatarMax.x) * 0.5f, (avatarMin.y + avatarMax.y) * 0.5f);
        }

        const ImVec2 textTopLeft =
            ImVec2(topLeft.x + paddingX + avatarContribution, topLeft.y + paddingY + (contentHeight - textColumnHeight) * 0.5f);
        ImVec2 namePos = ImVec2(textTopLeft.x, textTopLeft.y);
        if (useCompactLayout)
            namePos.y += (textColumnHeight - nameTextSize.y) * 0.5f;

        ImVec2 levelPos = namePos;
        if (showLevel)
        {
            if (useCompactLayout)
            {
                levelPos.x = textTopLeft.x + nameTextSize.x + nameLevelSpacing;
                levelPos.y = textTopLeft.y + (textColumnHeight - levelTextSize.y) * 0.5f;
            }
            else
            {
                levelPos.x = textTopLeft.x;
                levelPos.y = textTopLeft.y + nameTextSize.y + nameLevelSpacing;
            }
        }

        const ImVec2 accentMin(topLeft.x + paddingX, bottomRight.y - paddingY - accentHeight);
        const ImVec2 accentMax(bottomRight.x - paddingX, accentMin.y + accentHeight);
        const ImU32 accentColor = ColorWithAlpha(m_style.AccentColor, alpha);
        drawList->AddRectFilled(accentMin, accentMax, accentColor, m_style.CornerRounding);

        if (showAvatar)
        {
            const AvatarTexture* avatarTexture = ResolveAvatarTexture(playerComponent.Id, avatarData, nowTick);
            ID3D11ShaderResourceView* avatarView = avatarTexture ? avatarTexture->Texture.Get() : nullptr;
            const ImU32 ringColor = ColorWithAlpha(m_style.AvatarRingColor, alpha);

            const float avatarRounding = avatarSize * 0.5f;
            if (avatarView)
            {
                drawList->AddImageRounded(avatarView, avatarMin, avatarMax, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32_WHITE, avatarRounding,
                                          ImDrawFlags_RoundCornersAll);
            }
            else
            {
                const ImU32 placeholderColor = ColorWithAlpha(m_style.PlaceholderAvatarColor, alpha);
                drawList->AddCircleFilled(avatarCenter, avatarSize * 0.5f, placeholderColor, 48);

                if (!displayName.empty())
                {
                    const unsigned char rawChar = static_cast<unsigned char>(displayName.front());
                    const char initial = static_cast<char>(std::toupper(rawChar));
                    char buffer[2] = {initial, '\0'};
                    const ImVec2 initialSize = font->CalcTextSizeA(levelFontSize, FLT_MAX, 0.f, buffer, buffer + 1);
                    const ImVec2 initialPos = ImVec2(avatarCenter.x - initialSize.x * 0.5f, avatarCenter.y - initialSize.y * 0.5f);
                    drawList->AddText(font, levelFontSize, initialPos, ColorWithAlpha(m_style.TextColor, alpha), buffer);
                }
            }

            drawList->AddCircle(avatarCenter, (avatarSize * 0.5f) + 1.f, ringColor, 48, 2.f);
        }

        const ImU32 textColor = ColorWithAlpha(m_style.TextColor, alpha);
        drawList->AddText(font, nameFontSize, namePos, textColor, displayName.data(), displayName.data() + displayName.size());

        if (showLevel)
        {
            const ImU32 levelColor = ColorWithAlpha(m_style.LevelTextColor, alpha);
            drawList->AddText(font, levelFontSize, levelPos, levelColor, levelText.c_str(), levelText.c_str() + levelText.size());
        }
    }

    if (pFont)
        ImGui::PopFont();
}

void NameTagService::OnUpdate(const UpdateEvent&) noexcept
{
    auto* pLocalPlayer = PlayerCharacter::Get();
    if (!pLocalPlayer)
    {
        m_visibility.clear();
        return;
    }

    const uint64_t tick = m_world.GetTick();
    size_t total = 0;
    size_t resolved = 0;
    size_t visibleCount = 0;

    auto view = m_world.view<PlayerComponent, RemoteComponent, FormIdComponent>();
    for (auto entity : view)
    {
        ++total;
        const auto& playerComponent = view.get<PlayerComponent>(entity);
        const auto& formComponent = view.get<FormIdComponent>(entity);
        Actor* pActor = Cast<Actor>(TESForm::GetById(formComponent.Id));
        if (!pActor)
            continue;
        ++resolved;

        const float dx = pLocalPlayer->position.x - pActor->position.x;
        const float dy = pLocalPlayer->position.y - pActor->position.y;
        const float dz = pLocalPlayer->position.z - pActor->position.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        bool isVisible = ComputeLineOfSight(pLocalPlayer, pActor);
        if (!isVisible && distance <= m_style.NearBypassDistance)
            isVisible = true;

        VisibilityInfo& info = m_visibility[playerComponent.Id];
        info.Visible = isVisible;
        info.Tick = tick;
        if (info.Visible)
            ++visibleCount;
    }

    constexpr uint64_t kMaxAgeMs = 2000;
    for (auto it = m_visibility.begin(); it != m_visibility.end();)
    {
        if (tick - it->second.Tick > kMaxAgeMs)
            it = m_visibility.erase(it);
        else
            ++it;
    }

    static uint64_t s_lastLogTick = 0;
    if (tick - s_lastLogTick > 1000)
    {
        spdlog::debug("[NameTagService] update candidates={} resolved={} visible={}", total, resolved, visibleCount);
        s_lastLogTick = tick;
    }
}

bool NameTagService::ShouldRenderTag(uint32_t aPlayerId) const noexcept
{
    const uint64_t tick = m_world.GetTick();
    auto it = m_visibility.find(aPlayerId);
    if (it == m_visibility.end())
        return true; // not evaluated yet, optimistically show tag

    constexpr uint64_t kStaleThresholdMs = 1500;
    if (tick - it->second.Tick > kStaleThresholdMs)
        return true;

    return it->second.Visible;
}

bool NameTagService::ComputeLineOfSight(PlayerCharacter* apLocalPlayer, Actor* apRemoteActor) const noexcept
{
    if (!apLocalPlayer || !apRemoteActor)
        return false;

    if (apRemoteActor->IsDead())
        return false;

    if (apLocalPlayer->HasLineOfSight(apRemoteActor))
        return true;

    if (apRemoteActor->HasLineOfSight(apLocalPlayer))
        return true;

    return false;
}

bool NameTagService::ProjectWorldPoint(const NiPoint3& aWorldPoint, ImVec2& aScreenPos, float& aDepth, const ImVec2& aViewport) const noexcept
{
    NiPoint3 projected{};
    if (!HUDMenuUtils::WorldPtToScreenPt3(aWorldPoint, projected))
        return false;

    if (projected.z <= m_style.VisibilityEpsilon || projected.z >= 1.f)
        return false;

    aScreenPos.x = projected.x * aViewport.x;
    aScreenPos.y = (1.f - projected.y) * aViewport.y;
    aDepth = projected.z;

    return true;
}

NiPoint3 NameTagService::BuildAnchorPoint(Actor* apActor) const noexcept
{
    NiPoint3 anchor = apActor->position;
    anchor.z += apActor->GetHeight() + m_style.VerticalOffset;

    const float yaw = apActor->rotation.z;
    anchor.x += std::sin(yaw) * m_style.ForwardOffset;
    anchor.y += std::cos(yaw) * m_style.ForwardOffset;

    return anchor;
}

ImU32 NameTagService::ColorWithAlpha(const ImVec4& aColor, float aAlpha) noexcept
{
    ImVec4 adjusted = aColor;
    adjusted.w *= Clamp01(aAlpha);
    return ImGui::ColorConvertFloat4ToU32(adjusted);
}

void NameTagService::GarbageCollectAvatarCache(uint64_t aNowTick) noexcept
{
    constexpr uint64_t kTtlMs = 30'000;

    for (auto it = m_avatarCache.begin(); it != m_avatarCache.end();)
    {
        if (it->second.LastSeenTick != 0 && aNowTick - it->second.LastSeenTick > kTtlMs)
            it = m_avatarCache.erase(it);
        else
            ++it;
    }
}

const NameTagService::AvatarTexture* NameTagService::ResolveAvatarTexture(uint32_t aPlayerId, std::string_view aAvatarData, uint64_t aNowTick)
{
    if (aAvatarData.empty())
    {
        m_avatarCache.erase(aPlayerId);
        return nullptr;
    }

    AvatarTexture& entry = m_avatarCache[aPlayerId];
    entry.LastSeenTick = aNowTick;

    if (entry.Texture && entry.Signature == aAvatarData)
        return &entry;

    if (entry.Signature == aAvatarData && entry.Failed)
    {
        const uint64_t retryDelay = static_cast<uint64_t>(m_style.AvatarRetryDelayMs);
        if (aNowTick - entry.LastAttemptTick < retryDelay)
            return entry.Texture ? &entry : nullptr;
    }

    entry.Signature.assign(aAvatarData.begin(), aAvatarData.end());
    entry.LastAttemptTick = aNowTick;

    std::vector<uint8_t> decoded;
    if (!DecodeAvatarData(aAvatarData, decoded))
    {
        entry.Texture.Reset();
        entry.Size = ImVec2{};
        entry.Failed = true;
        return nullptr;
    }

    if (!CreateAvatarTexture(decoded, entry))
    {
        entry.Texture.Reset();
        entry.Size = ImVec2{};
        entry.Failed = true;
        return nullptr;
    }

    entry.Failed = false;
    return &entry;
}

bool NameTagService::DecodeAvatarData(std::string_view aAvatarData, std::vector<uint8_t>& aDecodedData) const
{
    if (aAvatarData.rfind("data:", 0) != 0)
        return false;

    const size_t maxBytes = static_cast<size_t>(m_style.AvatarMaxBytes);
    if (aAvatarData.size() > maxBytes * 3ull)
        return false;

    const size_t comma = aAvatarData.find(',');
    if (comma == std::string_view::npos)
        return false;

    const std::string_view header = aAvatarData.substr(0, comma);
    if (header.find(";base64") == std::string_view::npos)
        return false;

    const std::string_view payload = aAvatarData.substr(comma + 1);
    if (!DecodeBase64(payload, aDecodedData))
        return false;

    if (aDecodedData.empty() || aDecodedData.size() > maxBytes)
        return false;

    return true;
}

bool NameTagService::CreateAvatarTexture(const std::vector<uint8_t>& aDecodedData, AvatarTexture& aEntry) noexcept
{
    if (aDecodedData.empty())
        return false;

    ID3D11Device* pDevice = AcquireD3DDevice();
    if (!pDevice)
        return false;

    IWICImagingFactory* pFactory = GetWicFactory();
    if (!pFactory)
        return false;

    Microsoft::WRL::ComPtr<IWICStream> stream;
    HRESULT hr = pFactory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = stream->InitializeFromMemory(const_cast<BYTE*>(reinterpret_cast<const BYTE*>(aDecodedData.data())), static_cast<DWORD>(aDecodedData.size()));
    if (FAILED(hr))
        return false;

    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    hr = pFactory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr))
        return false;

    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr))
        return false;

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
        return false;

    constexpr UINT kMaxDimension = 1024;
    if (width > kMaxDimension || height > kMaxDimension)
        return false;

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    hr = pFactory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        return false;

    const UINT stride = width * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr))
        return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = stride;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    hr = pDevice->CreateTexture2D(&desc, &initData, texture.GetAddressOf());
    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            m_cachedDevice.Reset();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    hr = pDevice->CreateShaderResourceView(texture.Get(), &srvDesc, view.GetAddressOf());
    if (FAILED(hr))
        return false;

    aEntry.Texture = std::move(view);
    aEntry.Size = ImVec2(static_cast<float>(width), static_cast<float>(height));
    return true;
}

ID3D11Device* NameTagService::AcquireD3DDevice() noexcept
{
    if (m_cachedDevice)
        return m_cachedDevice.Get();

    if (!m_renderSystem && m_world.ctx().contains<RenderSystemD3D11>())
        m_renderSystem = &m_world.ctx().at<RenderSystemD3D11>();

    if (!m_renderSystem)
        return nullptr;

    IDXGISwapChain* pSwapChain = m_renderSystem->GetSwapChain();
    if (!pSwapChain)
    {
        m_cachedDevice.Reset();
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(device.GetAddressOf()))))
        return nullptr;

    m_cachedDevice = std::move(device);
    return m_cachedDevice.Get();
}

void NameTagService::SetMode(Mode aMode) noexcept
{
    switch (aMode)
    {
    case Mode::Detailed:
    case Mode::Basic:
    case Mode::Hidden:
    case Mode::Normal:
        break;
    default:
        aMode = Mode::Normal;
        break;
    }

    if (m_mode == aMode)
        return;

    spdlog::debug("[NameTagService] switching mode {}", static_cast<int>(aMode));
    m_mode = aMode;
}
