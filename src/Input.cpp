#include "apisig/Input.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <stdexcept>

namespace
{
std::string Trim(const std::string& value)
{
    const auto start = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (start >= end)
    {
        return {};
    }
    return std::string(start, end);
}

std::vector<std::string> ReadLines(const std::filesystem::path& filePath)
{
    std::ifstream input(filePath);
    if (!input)
    {
        throw std::runtime_error("Could not open file: " + filePath.string());
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line))
    {
        lines.push_back(line);
    }
    return lines;
}
} // namespace

namespace apisig
{
std::vector<std::string> ReadSymbolLines(const std::filesystem::path& filePath)
{
    std::vector<std::string> symbols;
    for (const std::string& rawLine : ReadLines(filePath))
    {
        const std::string line = Trim(rawLine);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        symbols.push_back(line);
    }
    return symbols;
}

std::vector<std::pair<std::string, std::string>> ReadMetadata(const std::filesystem::path& filePath)
{
    std::map<std::string, std::string> values;

    for (const std::string& rawLine : ReadLines(filePath))
    {
        const std::string line = Trim(rawLine);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos)
        {
            continue;
        }

        const std::string key = Trim(line.substr(0, separator));
        const std::string value = Trim(line.substr(separator + 1));
        if (!key.empty())
        {
            values[key] = value;
        }
    }

    return {values.begin(), values.end()};
}
} // namespace apisig
