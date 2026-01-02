

#include "ESLoader.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <unistd.h>
#endif

#include <Records/CLMT.h>
#include <Records/NPC.h>
#include <Records/REFR.h>

namespace
{
struct Mo2Context
{
    fs::path dataPath;
    fs::path loadOrderPath;
    fs::path pluginsPath;
    String profile;
};

std::once_flag s_loadOrderOnceFlag;
ESLoader::PluginCollection s_cachedLoadOrder;
TiltedPhoques::Map<String, uint32_t> s_cachedMasterFiles;
bool s_loadOrderSuccess = false;

fs::path ResolveExecutableDirectory() noexcept
{
#if defined(_WIN32)
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length != 0 && length < buffer.size())
        return fs::path(buffer.data()).parent_path();
#else
    std::error_code ec;
    auto exePath = fs::canonical("/proc/self/exe", ec);
    if (!ec)
        return exePath.parent_path();
#endif

    return {};
}

String Trim(String value)
{
    auto notSpace = [](int ch) { return !std::isspace(static_cast<unsigned char>(ch)); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

String ParseMo2ByteArray(String value)
{
    constexpr std::string_view kPrefix = "@ByteArray(";

    if (value.rfind(kPrefix.data(), 0) == 0 && !value.empty())
    {
        const auto endPos = value.rfind(')');
        if (endPos != String::npos && endPos > kPrefix.size())
        {
            value = value.substr(kPrefix.size(), endPos - kPrefix.size());
        }
    }

    if (!value.empty() && value.front() == '"' && value.back() == '"' && value.size() > 1)
    {
        value = value.substr(1, value.size() - 2);
    }

    String result;
    result.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i)
    {
        const char ch = value[i];
        if (ch == '\\' && i + 1 < value.size())
        {
            result.push_back(value[i + 1]);
            ++i;
        }
        else
        {
            result.push_back(ch);
        }
    }

    return result;
}

fs::path NormalizeMo2Path(String value)
{
    if (value.empty())
        return {};

    for (auto& ch : value)
    {
        if (ch == '\\')
            ch = '/';
    }

#if defined(__linux__)
    if (value.size() > 1 && value[1] == ':' && (value[0] == 'Z' || value[0] == 'z'))
    {
        value.erase(0, 2);
        if (value.empty() || (value.front() != '/' && value.front() != '\\'))
            value.insert(value.begin(), '/');
    }
#endif

    return fs::path(value);
}

enum class PluginKind
{
    kUnknown,
    kStandard,
    kLite,
};

bool HasEslFlag(const fs::path& aPluginPath) noexcept
{
    if (aPluginPath.empty())
        return false;

    std::ifstream file(aPluginPath, std::ios::binary);
    if (!file.is_open())
        return false;

    Record header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file)
        return false;

    if (header.GetType() != FormEnum::TES4)
        return false;

    return (header.GetFlags() & Record::FLAGS::kLightFile) != 0;
}

PluginKind ResolvePluginKind(const fs::path& aDataRoot, const String& aFilename) noexcept
{
    if (aFilename.empty())
        return PluginKind::kUnknown;

    const char extensionType = static_cast<char>(std::tolower(static_cast<unsigned char>(aFilename.back())));
    if (extensionType == 'l')
        return PluginKind::kLite;

    if (extensionType != 'm' && extensionType != 'p')
        return PluginKind::kUnknown;

    // ESL-flagged .esp/.esm plugins still use light IDs.
    if (HasEslFlag(aDataRoot / aFilename))
        return PluginKind::kLite;

    return PluginKind::kStandard;
}

uint32_t FormIdPrefixFromPlugin(const ESLoader::PluginData& aPlugin) noexcept
{
    if (aPlugin.m_isLite)
        return 0xFE000000u | (static_cast<uint32_t>(aPlugin.m_liteId) << 12);
    return static_cast<uint32_t>(aPlugin.m_standardId) << 24;
}

std::optional<Mo2Context> ParseMo2Instance(const fs::path& aRootPath, const fs::path& aIniPath, const char* aForcedProfile)
{
    std::ifstream iniStream(aIniPath.c_str());
    if (!iniStream.is_open())
        return std::nullopt;

    String currentSection;
    String selectedProfile;
    String gamePath;
    String line;

    while (std::getline(iniStream, line))
    {
        if (line.empty())
            continue;

        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }

        if (currentSection != "General")
            continue;

        const auto delimiter = line.find('=');
        if (delimiter == String::npos)
            continue;

        String key = Trim(line.substr(0, delimiter));
        String value = Trim(line.substr(delimiter + 1));

        if (key == "selected_profile")
            selectedProfile = ParseMo2ByteArray(value);
        else if (key == "gamePath")
            gamePath = ParseMo2ByteArray(value);

        if (!selectedProfile.empty() && !gamePath.empty())
            break;
    }

    const String configuredProfile = selectedProfile;
    const String forcedProfile = (aForcedProfile && aForcedProfile[0] != '\0') ? String(aForcedProfile) : String{};

    if (!forcedProfile.empty())
    {
        selectedProfile = forcedProfile;
    }

    if (selectedProfile.empty() || gamePath.empty())
        return std::nullopt;

    const fs::path dataRoot = NormalizeMo2Path(gamePath) / "Data";
    fs::path profilePath = aRootPath / "profiles" / selectedProfile;
    std::error_code profileEc;
    if (!fs::exists(profilePath, profileEc) || profileEc)
    {
        if (!configuredProfile.empty() && configuredProfile != selectedProfile)
        {
            if (!forcedProfile.empty())
            {
                spdlog::warn("ESLoader: MO2 profile override '{}' not found, falling back to '{}'", forcedProfile.c_str(), configuredProfile.c_str());
            }

            profilePath = aRootPath / "profiles" / configuredProfile;
            selectedProfile = configuredProfile;
            profileEc.clear();
        }
    }

    if (!fs::exists(profilePath, profileEc) || profileEc)
    {
        spdlog::warn("ESLoader: MO2 profile '{}' not found under {}", selectedProfile.c_str(), (aRootPath / "profiles").string());
        return std::nullopt;
    }

    Mo2Context context{};
    context.dataPath = dataRoot;
    context.loadOrderPath = profilePath / "loadorder.txt";
    context.pluginsPath = profilePath / "plugins.txt";
    context.profile = selectedProfile;

    return context;
}

std::optional<Mo2Context> SearchMo2Root(fs::path start, const char* aForcedProfile, std::vector<std::string>* aTrace)
{
    if (start.empty())
        return std::nullopt;

    std::error_code ec;
    auto canonical = fs::weakly_canonical(start, ec);
    if (!ec)
        start = canonical;
    else
    {
        canonical = fs::absolute(start, ec);
        if (!ec)
            start = canonical;
    }

    fs::path search = start;
    fs::path previous;

    while (true)
    {
        if (aTrace)
            aTrace->push_back(search.string());

        const fs::path iniPath = search / "ModOrganizer.ini";
        std::error_code existsEc;
        if (fs::exists(iniPath, existsEc) && !existsEc)
        {
            if (auto context = ParseMo2Instance(search, iniPath, aForcedProfile))
                return context;
        }

        if (!search.has_parent_path() || search == previous)
            break;

        previous = search;
        search = search.parent_path();
    }

    return std::nullopt;
}

std::optional<Mo2Context> DetectMo2Instance()
{
    std::vector<fs::path> candidates;
    std::vector<std::string> trace;

    const char* envInstance = std::getenv("MO2_INSTANCE_DIR");
    const char* envProfile = std::getenv("MO2_PROFILE");

    if (envInstance && envInstance[0] != '\0')
        candidates.emplace_back(NormalizeMo2Path(envInstance));

    if (auto exeDir = ResolveExecutableDirectory(); !exeDir.empty())
        candidates.push_back(exeDir);

    std::error_code ec;
    auto cwd = fs::current_path(ec);
    if (!ec)
        candidates.push_back(cwd);

#if defined(_WIN32)
    if (const char* localAppData = std::getenv("LOCALAPPDATA"); localAppData && localAppData[0] != '\0')
    {
        candidates.emplace_back(fs::path(localAppData) / "ModOrganizer");
        candidates.emplace_back(fs::path(localAppData) / "ModOrganizer" / "Skyrim Special Edition");
    }
#endif

    for (const auto& root : candidates)
    {
        if (auto context = SearchMo2Root(root, envProfile, &trace))
            return context;
    }

    if (!trace.empty())
    {
        std::string message = "ESLoader: no Mod Organizer installation detected. searched paths: ";
        for (size_t i = 0; i < trace.size(); ++i)
        {
            message += trace[i];
            if (i + 1 < trace.size())
                message += ", ";
        }
        spdlog::info(message);
    }

    return std::nullopt;
}

bool LoadPluginsTxtFromPath(const fs::path& aPluginsPath, const fs::path& aDataRoot, Vector<ESLoader::PluginData>& outOrder)
{
    std::ifstream pluginsFile(aPluginsPath.c_str());
    if (!pluginsFile.is_open())
        return false;

    uint8_t standardId = 0x0;
    uint16_t liteId = 0x0;

    while (!pluginsFile.eof())
    {
        String line;
        std::getline(pluginsFile, line);

        if (line.empty() || line[0] == '#')
            continue;

        if (!line.empty() && line[0] == '*')
            line.erase(line.begin());

        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        if (line.empty())
            continue;

        ESLoader::PluginData plugin{};
        plugin.m_filename = line;
        const auto kind = ResolvePluginKind(aDataRoot, line);
        switch (kind)
        {
        case PluginKind::kStandard:
            plugin.m_standardId = standardId++;
            plugin.m_isLite = false;
            outOrder.push_back(plugin);
            break;
        case PluginKind::kLite:
            plugin.m_liteId = liteId++;
            plugin.m_isLite = true;
            outOrder.push_back(plugin);
            break;
        default:
            break;
        }
    }

    return !outOrder.empty();
}

bool LoadPluginsTxtFromLocalAppData(const fs::path& aDataRoot, Vector<ESLoader::PluginData>& outOrder)
{
#if defined(_WIN32)
    if (char* localAppData = std::getenv("LOCALAPPDATA"))
    {
        const fs::path pluginsPath = fs::path(localAppData) / "Skyrim Special Edition" / "plugins.txt";
        return LoadPluginsTxtFromPath(pluginsPath, aDataRoot, outOrder);
    }
#endif

    return false;
}
} // namespace

namespace ESLoader
{
String ReadZString(Buffer::Reader& aReader) noexcept
{
    String zstring = String(reinterpret_cast<const char*>(aReader.GetDataAtPosition()));
    aReader.Advance(zstring.size() + 1);
    return zstring;
}

String ReadWString(Buffer::Reader& aReader) noexcept
{
    uint16_t stringLength = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&stringLength), 2);
    String wstring = String(reinterpret_cast<const char*>(aReader.GetDataAtPosition()), stringLength);
    aReader.Advance(stringLength);
    return wstring;
}

ESLoader::ESLoader()
{
    if (auto context = DetectMo2Instance())
    {
        m_directory = context->dataPath;
        m_loadOrderFile = context->loadOrderPath;
        m_pluginsFile = context->pluginsPath;
        spdlog::info("Detected Mod Organizer 2 profile '{}' (Data: {}, loadorder: {}).", context->profile, m_directory.string(), m_loadOrderFile.string());
    }
    else
    {
        m_directory = fs::current_path() / "Data"; //< Keep upper case to match Skyrim's file system
        m_loadOrderFile = m_directory / "loadorder.txt";
    }
}

UniquePtr<RecordCollection> ESLoader::BuildRecordCollection() noexcept
{
    if (!fs::is_directory(m_directory))
    {
        spdlog::warn("Data directory not found at {} (MO2 VFS may still provide files)", m_directory.string());
        // Continue; we will attempt to open files directly via VFS paths.
    }

    if (!LoadLoadOrder())
    {
        spdlog::warn("No load order found; will fallback to base ESM set if available.");
    }

    // Load all plugins from discovered list and index records
    auto recordCollection = LoadFiles();
    if (!recordCollection)
        return nullptr;

    // Optional: build cross-record references if needed elsewhere
    // recordCollection->BuildReferences();

    return std::move(recordCollection);
}

bool ESLoader::LoadLoadOrder()
{
    std::call_once(s_loadOrderOnceFlag, [this]() {
        s_loadOrderSuccess = LoadLoadOrderFromDisk();
        if (s_loadOrderSuccess)
        {
            s_cachedLoadOrder = m_loadOrder;
            s_cachedMasterFiles = m_masterFiles;
        }
    });

    if (!s_loadOrderSuccess)
        return false;

    m_loadOrder = s_cachedLoadOrder;
    m_masterFiles = s_cachedMasterFiles;
    return true;
}

bool ESLoader::LoadLoadOrderFromDisk()
{
    m_loadOrder.clear();
    m_masterFiles.clear();

    std::ifstream loadOrderFile;
    const fs::path loadOrderPath = !m_loadOrderFile.empty() ? m_loadOrderFile : (m_directory / "loadorder.txt");
    if (!loadOrderPath.empty())
        loadOrderFile.open(loadOrderPath.c_str());
    else
        loadOrderFile.setstate(std::ios::failbit);

    if (loadOrderFile.fail())
    {
        bool loaded = false;
        bool usedProfilePlugins = false;

        if (!m_pluginsFile.empty())
        {
            loaded = LoadPluginsTxtFromPath(m_pluginsFile, m_directory, m_loadOrder);
            usedProfilePlugins = loaded;
        }

        if (!loaded)
            loaded = LoadPluginsTxtFromLocalAppData(m_directory, m_loadOrder);

        if (loaded)
        {
            for (const auto& p : m_loadOrder)
            {
                m_masterFiles[p.m_filename] = FormIdPrefixFromPlugin(p);
            }

            if (usedProfilePlugins)
                spdlog::info("ESLoader: queued {} plugins from {}", m_loadOrder.size(), m_pluginsFile.string());
            else
                spdlog::info("ESLoader: queued {} plugins from plugins.txt in LOCALAPPDATA", m_loadOrder.size());

            return true;
        }
        // Fallback to base ESMs minimal set
        spdlog::warn("Failed to open loadorder.txt at {}; falling back to base ESM set", loadOrderPath.string());
        Vector<String> base = {"Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"};
        uint8_t standardId = 0x0;
        for (auto& name : base)
        {
            PluginData plugin;
            plugin.m_filename = name;
            plugin.m_standardId = standardId;
            plugin.m_isLite = false;
            m_loadOrder.push_back(plugin);
            m_masterFiles[name] = FormIdPrefixFromPlugin(plugin);
            standardId += 1;
        }
        spdlog::info("ESLoader: queued {} base-game plugins (fallback set)", m_loadOrder.size());
        return true;
    }

    uint8_t standardId = 0x0;
    uint16_t liteId = 0x0;

    while (!loadOrderFile.eof())
    {
        String line;
        std::getline(loadOrderFile, line);
        if (line.empty() || line[0] == '#')
            continue;

        // Trim CR (Linux/Windows)
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        if (line.empty())
            continue;

        PluginData plugin;
        plugin.m_filename = line;

        const auto kind = ResolvePluginKind(m_directory, line);

        switch (kind)
        {
        case PluginKind::kStandard:
            plugin.m_standardId = standardId;
            plugin.m_isLite = false;
            m_loadOrder.push_back(plugin);
            m_masterFiles[line] = FormIdPrefixFromPlugin(plugin);
            standardId += 0x01;
            break;
        case PluginKind::kLite:
            plugin.m_liteId = liteId;
            plugin.m_isLite = true;
            m_loadOrder.push_back(plugin);
            m_masterFiles[line] = FormIdPrefixFromPlugin(plugin);
            liteId += 0x0001;
            break;
        default:
            spdlog::error("Extension in loadorder.txt not recognized: {}", line);
        }
    }

    spdlog::info("ESLoader: queued {} plugins from {}", m_loadOrder.size(), loadOrderPath.string());

    return true;
}

UniquePtr<RecordCollection> ESLoader::LoadFiles()
{
    auto recordCollection = MakeUnique<RecordCollection>();

    for (PluginData& plugin : m_loadOrder)
    {
        fs::path pluginPath = GetPath(plugin.m_filename);
        if (pluginPath.empty())
        {
            spdlog::warn("Path to plugin file not found: {}", plugin.m_filename);
            continue;
        }

        TESFile pluginFile(m_masterFiles);
        if (plugin.IsLite())
            pluginFile.Setup(plugin.m_liteId);
        else
            pluginFile.Setup(plugin.m_standardId);

        bool loadResult = pluginFile.LoadFile(pluginPath);

        if (!loadResult)
            continue;

        pluginFile.IndexRecords(*recordCollection);
    }

    return recordCollection;
}

fs::path ESLoader::GetPath(String& aFilename)
{
    // Prefer direct path; MO2 VFS will usually resolve this without enumerating the directory
    fs::path direct = m_directory / aFilename;
    return direct;
}

} // namespace ESLoader
