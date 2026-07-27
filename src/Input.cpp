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

std::string StripCppComments(const std::string& line, bool& inBlockComment)
{
    std::string out;
    out.reserve(line.size());

    std::size_t i = 0;
    while (i < line.size())
    {
        if (inBlockComment)
        {
            const std::size_t endPos = line.find("*/", i);
            if (endPos == std::string::npos)
            {
                return out;
            }

            inBlockComment = false;
            i = endPos + 2;
            continue;
        }

        if ((i + 1) < line.size() && line[i] == '/' && line[i + 1] == '/')
        {
            break;
        }

        if ((i + 1) < line.size() && line[i] == '/' && line[i + 1] == '*')
        {
            inBlockComment = true;
            i += 2;
            continue;
        }

        out.push_back(line[i]);
        ++i;
    }

    return out;
}

std::string StripUtf8BomAtStart(const std::string& line)
{
    if (line.size() >= 3 &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF)
    {
        return line.substr(3);
    }

    return line;
}

bool IsWordToken(const std::string& token)
{
    if (token.empty())
    {
        return false;
    }

    const unsigned char c = static_cast<unsigned char>(token[0]);
    return std::isalnum(c) != 0 || c == '_';
}

bool IsStringOrCharLiteralToken(const std::string& token)
{
    if (token.empty())
    {
        return false;
    }

    const char c = token.front();
    return c == '"' || c == '\'';
}

bool NeedsTokenSeparator(const std::string& left, const std::string& right)
{
    const bool leftWord = IsWordToken(left);
    const bool rightWord = IsWordToken(right);
    const bool leftLiteral = IsStringOrCharLiteralToken(left);
    const bool rightLiteral = IsStringOrCharLiteralToken(right);

    // Keep lexical boundaries stable for identifiers/literals in macro and declaration contexts.
    return (leftWord && rightWord) ||
           (leftWord && rightLiteral) ||
           (leftLiteral && rightWord);
}

std::string NormalizeCppWhitespace(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string token;

    bool inString = false;
    bool inChar = false;
    bool escape = false;

    auto flushToken = [&]() {
        if (!token.empty())
        {
            tokens.push_back(token);
            token.clear();
        }
    };

    for (const char ch : line)
    {
        if (inString || inChar)
        {
            token.push_back(ch);
            if (escape)
            {
                escape = false;
            }
            else if (ch == '\\')
            {
                escape = true;
            }
            else if ((inString && ch == '"') || (inChar && ch == '\''))
            {
                inString = false;
                inChar = false;
                flushToken();
            }
            continue;
        }

        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isspace(uch) != 0)
        {
            flushToken();
            continue;
        }

        if (ch == '"')
        {
            flushToken();
            token.push_back(ch);
            inString = true;
            continue;
        }

        if (ch == '\'')
        {
            flushToken();
            token.push_back(ch);
            inChar = true;
            continue;
        }

        if (std::isalnum(uch) != 0 || ch == '_')
        {
            token.push_back(ch);
            continue;
        }

        flushToken();
        tokens.push_back(std::string(1, ch));
    }

    flushToken();

    std::string normalized;
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        if (i > 0 && NeedsTokenSeparator(tokens[i - 1], tokens[i]))
        {
            normalized.push_back(' ');
        }
        normalized += tokens[i];
    }

    return normalized;
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
    bool inBlockComment = false;
    bool firstLine = true;
    std::string logicalLine;

    auto finalizeLogicalLine = [&](const std::string& lineValue) {
        const std::string line = NormalizeCppWhitespace(Trim(lineValue));
        if (line.empty())
        {
            return;
        }

        // Keep #define and other preprocessor lines; treat '# ' as a human comment line.
        if (line.rfind("# ", 0) == 0)
        {
            return;
        }

        symbols.push_back(line);
    };

    for (const std::string& rawLine : ReadLines(filePath))
    {
        std::string lineForParse = rawLine;
        if (firstLine)
        {
            lineForParse = StripUtf8BomAtStart(lineForParse);
            firstLine = false;
        }

        const std::string withoutComments = StripCppComments(lineForParse, inBlockComment);
        const std::string trimmed = Trim(withoutComments);
        if (trimmed.empty())
        {
            continue;
        }

        if (!logicalLine.empty())
        {
            logicalLine += ' ';
        }
        logicalLine += trimmed;

        if (!logicalLine.empty() && logicalLine.back() == '\\')
        {
            logicalLine.pop_back();
            logicalLine = Trim(logicalLine);
            continue;
        }

        finalizeLogicalLine(logicalLine);
        logicalLine.clear();
    }

    if (!logicalLine.empty())
    {
        finalizeLogicalLine(logicalLine);
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
