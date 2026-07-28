#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace apisig
{
std::vector<std::string> ReadSymbolLines(const std::filesystem::path& filePath);
std::vector<std::string> ReadSemanticModelFromAstReport(const std::filesystem::path& filePath);
std::vector<std::pair<std::string, std::string>> ReadMetadata(const std::filesystem::path& filePath);
} // namespace apisig
