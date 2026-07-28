#include "apisig/ClangExtractor.h"
#include "apisig/Input.h"
#include "apisig/SignatureEngine.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
enum class CommandType
{
    Compute,
    Snapshot,
    Compare,
    Extract
};

struct CliOptions
{
    CommandType command = CommandType::Compute;
    std::optional<std::filesystem::path> symbolsFile;
    std::optional<std::filesystem::path> metadataFile;
    std::optional<std::filesystem::path> compdb;
    std::optional<std::filesystem::path> sourceRoot;
    std::optional<std::filesystem::path> outputFile;
    std::optional<std::filesystem::path> baselineFile;
    bool json = false;
    bool strictTooling = false;
    bool noToolingBanner = false;
    bool help = false;
    bool version = false;
    std::vector<std::string> toolingStripArgPrefixes;
    std::vector<std::string> toolingSuppressions;
};

struct Baseline
{
    std::string apiHash;
    std::string rebuildHash;
};

enum ExitCode
{
    ExitOk = 0,
    ExitError = 1,
    ExitUsage = 2,
    ExitApiChanged = 10,
    ExitRebuildChanged = 11
};

void PrintToolingModeBanner(bool strictTooling);

std::string EscapeJson(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    for (const char ch : value)
    {
        switch (ch)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

std::string SignatureJson(const apisig::SignaturePair& result)
{
    return "{\n"
           "  \"api_hash\": \"" + EscapeJson(result.apiHash) + "\",\n"
           "  \"rebuild_hash\": \"" + EscapeJson(result.rebuildHash) + "\"\n"
           "}\n";
}

void PrintUsage()
{
    std::cout << "apisig <command> [options]\n\n"
              << "Commands:\n"
              << "  compute   Compute signatures from current input\n"
              << "  snapshot  Compute signatures and write a baseline JSON\n"
              << "  compare   Compute signatures and compare to a baseline JSON\n"
              << "  extract   Print normalized/extracted symbols used for hashing\n\n"
              << "Options:\n"
              << "  --symbols <file>     Public symbols file (one symbol per line)\n"
              << "  --metadata <file>    Metadata file (key=value per line)\n"
              << "  --compdb <file>      compile_commands.json for LibTooling mode\n"
              << "  --source-root <dir>  Source root used with --compdb\n"
              << "  --out <file>         Snapshot output JSON path (snapshot only)\n"
              << "  --baseline <file>    Baseline JSON path (compare only)\n"
              << "  --json               Print JSON output\n"
              << "  --strict-tooling     Keep original compile flags and warning policy\n"
              << "  --tooling-strip-arg <prefix>  Strip matching compile args (repeatable)\n"
              << "  --tooling-suppress <warning>  Add explicit tooling suppression (repeatable)\n"
              << "  --no-tooling-banner  Do not print LibTooling mode banner\n"
              << "  --version, -v        Show apisig version\n"
              << "  --help, -h           Show this help message\n";
}

void PrintVersion()
{
    std::cout << "apisig " << APISIG_VERSION << '\n';
}

std::string ReadAllText(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string ExtractJsonString(const std::string& json, const std::string& key)
{
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = json.find(needle);
    if (keyPos == std::string::npos)
    {
        throw std::runtime_error("Missing key in baseline JSON: " + key);
    }

    const auto colonPos = json.find(':', keyPos + needle.size());
    if (colonPos == std::string::npos)
    {
        throw std::runtime_error("Invalid JSON format for key: " + key);
    }

    const auto firstQuote = json.find('"', colonPos + 1);
    if (firstQuote == std::string::npos)
    {
        throw std::runtime_error("Expected quoted value for key: " + key);
    }

    const auto secondQuote = json.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos)
    {
        throw std::runtime_error("Unterminated quoted value for key: " + key);
    }

    return json.substr(firstQuote + 1, secondQuote - (firstQuote + 1));
}

Baseline ReadBaseline(const std::filesystem::path& path)
{
    const std::string content = ReadAllText(path);
    return Baseline{
        ExtractJsonString(content, "api_hash"),
        ExtractJsonString(content, "rebuild_hash")};
}

void WriteSnapshot(const std::filesystem::path& path, const apisig::SignaturePair& result)
{
    std::ofstream output(path);
    if (!output)
    {
        throw std::runtime_error("Could not write snapshot file: " + path.string());
    }

    output << "{\n"
              "  \"version\": 1,\n"
              "  \"api_hash\": \""
           << EscapeJson(result.apiHash)
           << "\",\n"
              "  \"rebuild_hash\": \""
           << EscapeJson(result.rebuildHash)
           << "\"\n"
              "}\n";
}

CliOptions ParseArguments(const std::vector<std::string>& args)
{
    if (args.empty())
    {
        throw std::runtime_error("Missing command. Use compute, snapshot, or compare.");
    }

    if (args[0] == "--help" || args[0] == "-h" || args[0] == "help")
    {
        CliOptions options;
        options.help = true;
        return options;
    }

    if (args[0] == "--version" || args[0] == "-v" || args[0] == "version")
    {
        CliOptions options;
        options.version = true;
        return options;
    }

    CliOptions options;
    if (args[0] == "compute")
    {
        options.command = CommandType::Compute;
    }
    else if (args[0] == "snapshot")
    {
        options.command = CommandType::Snapshot;
    }
    else if (args[0] == "compare")
    {
        options.command = CommandType::Compare;
    }
    else if (args[0] == "extract")
    {
        options.command = CommandType::Extract;
    }
    else
    {
        throw std::runtime_error("Unknown command: " + args[0]);
    }

    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const std::string& arg = args[i];
        const auto requireValue = [&](const std::string& optionName) -> std::string {
            if (i + 1 >= args.size())
            {
                throw std::runtime_error("Missing value for option: " + optionName);
            }
            ++i;
            return args[i];
        };

        if (arg == "--symbols")
        {
            options.symbolsFile = requireValue(arg);
        }
        else if (arg == "--metadata")
        {
            options.metadataFile = requireValue(arg);
        }
        else if (arg == "--compdb")
        {
            options.compdb = requireValue(arg);
        }
        else if (arg == "--source-root")
        {
            options.sourceRoot = requireValue(arg);
        }
        else if (arg == "--out")
        {
            options.outputFile = requireValue(arg);
        }
        else if (arg == "--baseline")
        {
            options.baselineFile = requireValue(arg);
        }
        else if (arg == "--json")
        {
            options.json = true;
        }
        else if (arg == "--strict-tooling")
        {
            options.strictTooling = true;
        }
        else if (arg == "--tooling-strip-arg")
        {
            options.toolingStripArgPrefixes.push_back(requireValue(arg));
        }
        else if (arg == "--tooling-suppress")
        {
            options.toolingSuppressions.push_back(requireValue(arg));
        }
        else if (arg == "--no-tooling-banner")
        {
            options.noToolingBanner = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            options.help = true;
        }
        else if (arg == "--version" || arg == "-v")
        {
            options.version = true;
        }
        else
        {
            throw std::runtime_error("Unknown option: " + arg);
        }
    }

    return options;
}

apisig::ComputeRequest BuildRequest(const CliOptions& options)
{
    apisig::ComputeRequest request;

    if (options.compdb.has_value())
    {
        if (!options.sourceRoot.has_value())
        {
            throw std::runtime_error("--source-root is required when --compdb is provided");
        }

        if (!options.noToolingBanner)
        {
            PrintToolingModeBanner(options.strictTooling);
        }

        request.symbols = apisig::ExtractPublicSymbolsFromCompilationDatabase(
            options.compdb.value(),
            options.sourceRoot.value(),
            options.strictTooling,
            options.toolingStripArgPrefixes,
            options.toolingSuppressions);
    }
    else
    {
        if (!options.symbolsFile.has_value())
        {
            throw std::runtime_error("--symbols is required when --compdb is not used");
        }
        request.symbols = apisig::ReadSymbolLines(options.symbolsFile.value());
    }

    if (options.metadataFile.has_value())
    {
        request.metadata = apisig::ReadMetadata(options.metadataFile.value());
    }

    return request;
}

void PrintSymbols(const std::vector<std::string>& symbols, bool json)
{
    std::vector<std::string> canonical = symbols;
    std::sort(canonical.begin(), canonical.end());
    canonical.erase(std::unique(canonical.begin(), canonical.end()), canonical.end());

    if (json)
    {
        std::cout << "{\n  \"symbols\": [\n";
        for (std::size_t i = 0; i < canonical.size(); ++i)
        {
            std::cout << "    \"" << EscapeJson(canonical[i]) << "\"";
            if (i + 1 < canonical.size())
            {
                std::cout << ',';
            }
            std::cout << '\n';
        }
        std::cout << "  ]\n}\n";
        return;
    }

    for (const std::string& symbol : canonical)
    {
        std::cout << symbol << '\n';
    }
}

void PrintResult(const apisig::SignaturePair& result, bool json)
{
    if (json)
    {
        std::cout << SignatureJson(result);
        return;
    }

    std::cout << "api_hash=" << result.apiHash << '\n';
    std::cout << "rebuild_hash=" << result.rebuildHash << '\n';
}

void PrintToolingModeBanner(bool strictTooling)
{
    std::cerr << "apisig: LibTooling mode: "
              << (strictTooling ? "strict" : "compatibility")
              << '\n';
}
} // namespace

int RunCli(const std::vector<std::string>& args)
{
    try
    {
        if (args.empty())
        {
            PrintUsage();
            return ExitUsage;
        }

        const CliOptions options = ParseArguments(args);
        if (options.help)
        {
            PrintUsage();
            return ExitOk;
        }
        if (options.version)
        {
            PrintVersion();
            return ExitOk;
        }

        if (options.command == CommandType::Extract)
        {
            if (options.compdb.has_value())
            {
                if (!options.sourceRoot.has_value())
                {
                    throw std::runtime_error("--source-root is required when --compdb is provided");
                }

                if (!options.noToolingBanner)
                {
                    PrintToolingModeBanner(options.strictTooling);
                }

                const std::vector<std::string> symbols = apisig::ExtractPublicSymbolsFromCompilationDatabase(
                    options.compdb.value(),
                    options.sourceRoot.value(),
                    options.strictTooling,
                    options.toolingStripArgPrefixes,
                    options.toolingSuppressions);
                PrintSymbols(symbols, options.json);
                return ExitOk;
            }

            if (!options.symbolsFile.has_value())
            {
                throw std::runtime_error("--symbols is required when --compdb is not used");
            }

            const std::vector<std::string> symbols = apisig::ReadSymbolLines(options.symbolsFile.value());
            PrintSymbols(symbols, options.json);
            return ExitOk;
        }

        const apisig::ComputeRequest request = BuildRequest(options);
        const apisig::SignaturePair result = apisig::ComputeSignatures(request);

        if (options.command == CommandType::Compute)
        {
            PrintResult(result, options.json);
            return ExitOk;
        }

        if (options.command == CommandType::Snapshot)
        {
            if (!options.outputFile.has_value())
            {
                throw std::runtime_error("--out is required for snapshot command");
            }

            WriteSnapshot(options.outputFile.value(), result);
            if (options.json)
            {
                std::cout << SignatureJson(result);
            }
            else
            {
                std::cout << "snapshot=" << options.outputFile.value().string() << '\n';
                PrintResult(result, false);
            }
            return ExitOk;
        }

        if (!options.baselineFile.has_value())
        {
            throw std::runtime_error("--baseline is required for compare command");
        }

        const Baseline baseline = ReadBaseline(options.baselineFile.value());

        const bool apiChanged = (result.apiHash != baseline.apiHash);
        const bool rebuildChanged = (result.rebuildHash != baseline.rebuildHash);

        if (options.json)
        {
            const std::string status = apiChanged ? "api_changed" : (rebuildChanged ? "rebuild_changed" : "unchanged");
            std::string currentJson = SignatureJson(result);
            if (!currentJson.empty() && currentJson.back() == '\n')
            {
                currentJson.pop_back();
            }
            std::cout << "{\n"
                      << "  \"status\": \"" << status << "\",\n"
                      << "  \"current\": " << currentJson << ",\n"
                      << "  \"baseline\": {\n"
                      << "    \"api_hash\": \"" << baseline.apiHash << "\",\n"
                      << "    \"rebuild_hash\": \"" << baseline.rebuildHash << "\"\n"
                      << "  }\n"
                      << "}\n";
        }
        else
        {
            std::cout << "baseline=" << options.baselineFile.value().string() << '\n';
            if (!apiChanged && !rebuildChanged)
            {
                std::cout << "status=unchanged\n";
            }
            else if (apiChanged)
            {
                std::cout << "status=api_changed\n";
            }
            else
            {
                std::cout << "status=rebuild_changed\n";
            }

            std::cout << "baseline_api_hash=" << baseline.apiHash << '\n';
            std::cout << "current_api_hash=" << result.apiHash << '\n';
            std::cout << "baseline_rebuild_hash=" << baseline.rebuildHash << '\n';
            std::cout << "current_rebuild_hash=" << result.rebuildHash << '\n';
        }

        if (apiChanged)
        {
            return ExitApiChanged;
        }
        if (rebuildChanged)
        {
            return ExitRebuildChanged;
        }

        return ExitOk;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "apisig: " << ex.what() << '\n';
        return ExitError;
    }
}
