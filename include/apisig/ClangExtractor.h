#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace apisig
{
std::vector<std::string> ExtractPublicSymbolsFromCompilationDatabase(
    const std::filesystem::path& compilationDatabase,
    const std::filesystem::path& sourceRoot,
    bool strictTooling = false,
    const std::vector<std::string>& extraToolingStripArgPrefixes = {},
    const std::vector<std::string>& extraToolingSuppressions = {});
} // namespace apisig
