#pragma once

#include <filesystem>
#include <string>

namespace SaveGameUtils
{
std::string GetCurrentSaveName() noexcept;
std::filesystem::path GetCurrentSavePath() noexcept;
}
