#include "apisig/Input.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
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

std::string ReadAllText(const std::filesystem::path& filePath)
{
    std::ifstream input(filePath);
    if (!input)
    {
        throw std::runtime_error("Could not open file: " + filePath.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void SkipWhitespace(const std::string& json, std::size_t& position)
{
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])) != 0)
    {
        ++position;
    }
}

std::string ParseJsonString(const std::string& json, std::size_t& position)
{
    if (position >= json.size() || json[position] != '"')
    {
        throw std::runtime_error("Invalid JSON: expected string value");
    }
    ++position;

    std::string value;
    while (position < json.size())
    {
        const char ch = json[position++];
        if (ch == '"')
        {
            return value;
        }

        if (ch != '\\')
        {
            value.push_back(ch);
            continue;
        }

        if (position >= json.size())
        {
            throw std::runtime_error("Invalid JSON: unterminated escape sequence");
        }

        const char escaped = json[position++];
        switch (escaped)
        {
        case '"':
            value.push_back('"');
            break;
        case '\\':
            value.push_back('\\');
            break;
        case '/':
            value.push_back('/');
            break;
        case 'b':
            value.push_back('\b');
            break;
        case 'f':
            value.push_back('\f');
            break;
        case 'n':
            value.push_back('\n');
            break;
        case 'r':
            value.push_back('\r');
            break;
        case 't':
            value.push_back('\t');
            break;
        default:
            throw std::runtime_error("Invalid JSON: unsupported escape sequence in semantic model report");
        }
    }

    throw std::runtime_error("Invalid JSON: unterminated string value");
}

std::size_t FindPropertyValueStart(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPos = json.find(needle);
    if (keyPos == std::string::npos)
    {
        throw std::runtime_error("Invalid AST report: missing required key '" + key + "'");
    }

    const std::size_t colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos)
    {
        throw std::runtime_error("Invalid AST report: malformed key '" + key + "'");
    }

    std::size_t valuePos = colonPos + 1;
    SkipWhitespace(json, valuePos);
    if (valuePos >= json.size())
    {
        throw std::runtime_error("Invalid AST report: missing value for key '" + key + "'");
    }

    return valuePos;
}

int ParsePositiveInt(const std::string& json, std::size_t& position)
{
    SkipWhitespace(json, position);
    if (position >= json.size() || !std::isdigit(static_cast<unsigned char>(json[position])))
    {
        throw std::runtime_error("Invalid AST report: expected integer format_version");
    }

    int value = 0;
    while (position < json.size() && std::isdigit(static_cast<unsigned char>(json[position])) != 0)
    {
        value = (value * 10) + (json[position] - '0');
        ++position;
    }

    return value;
}

std::vector<std::string> ParseStringArray(const std::string& json, std::size_t& position)
{
    SkipWhitespace(json, position);
    if (position >= json.size() || json[position] != '[')
    {
        throw std::runtime_error("Invalid AST report: semantic_model must be an array");
    }
    ++position;

    std::vector<std::string> values;
    SkipWhitespace(json, position);
    if (position < json.size() && json[position] == ']')
    {
        ++position;
        return values;
    }

    while (position < json.size())
    {
        SkipWhitespace(json, position);
        std::string value = ParseJsonString(json, position);
        if (value.empty())
        {
            throw std::runtime_error("Invalid AST report: semantic_model entries must not be empty");
        }
        values.push_back(std::move(value));

        SkipWhitespace(json, position);
        if (position >= json.size())
        {
            break;
        }

        if (json[position] == ',')
        {
            ++position;
            continue;
        }

        if (json[position] == ']')
        {
            ++position;
            return values;
        }

        throw std::runtime_error("Invalid AST report: malformed semantic_model array");
    }

    throw std::runtime_error("Invalid AST report: unterminated semantic_model array");
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

std::vector<std::string> ReadSemanticModelFromAstReport(const std::filesystem::path& filePath)
{
    const std::string json = ReadAllText(filePath);

    std::size_t schemaPos = FindPropertyValueStart(json, "schema");
    const std::string schema = ParseJsonString(json, schemaPos);
    if (schema != "apisig.ast-report.v1")
    {
        throw std::runtime_error(
            "Invalid AST report schema: expected 'apisig.ast-report.v1', got '" + schema + "'");
    }

    std::size_t versionPos = FindPropertyValueStart(json, "format_version");
    const int version = ParsePositiveInt(json, versionPos);
    if (version != 1)
    {
        throw std::runtime_error("Unsupported AST report format_version: " + std::to_string(version));
    }

    std::size_t semanticPos = FindPropertyValueStart(json, "semantic_model");
    std::vector<std::string> semanticModel = ParseStringArray(json, semanticPos);
    if (semanticModel.empty())
    {
        throw std::runtime_error("Invalid AST report: semantic_model must not be empty");
    }

    return semanticModel;
}
} // namespace apisig
