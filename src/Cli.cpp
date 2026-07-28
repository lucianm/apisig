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
    std::optional<std::filesystem::path> baselineAstReportJson;
    std::optional<std::filesystem::path> astReportOut;
    std::optional<std::filesystem::path> astReportJson;
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
    std::optional<std::string> rebuildHash;
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
              << "  compute   Compute api_hash and rebuild_hash from one input mode\n"
              << "  snapshot  Compute signatures and write the hash pair as baseline JSON\n"
              << "  compare   Compute current signatures and compare them to a baseline JSON\n"
              << "  extract   Print normalized symbols to stdout; optionally also write an AST report JSON\n\n"
              << "Input options (choose one input mode for compute/snapshot/compare):\n"
              << "  --symbols <file>     Text input: one symbol/declaration line per entry\n"
              << "  --compdb <file>      LibTooling input: compile_commands.json\n"
              << "  --ast-report-json <file>  AST report JSON carrying semantic_model\n"
              << "  --source-root <dir>  Source root used with --compdb\n"
              << "  --metadata <file>    Metadata key=value file affecting rebuild_hash only\n\n"
              << "Artifact file options:\n"
              << "  --out <file>         Write snapshot baseline JSON (snapshot only)\n"
              << "  --baseline <file>    Read baseline hash JSON (compare only)\n"
              << "  --baseline-ast-report-json <file>  Read baseline API from an AST report JSON (compare only)\n"
              << "  --ast-report-out <file>  Write AST report JSON during compdb extraction\n\n"
              << "Stdout formatting:\n"
              << "  --json               Print the command result to stdout as JSON\n\n"
              << "Tooling controls:\n"
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

void WriteAstReportJson(const std::filesystem::path& path, const apisig::ExtractionReport& report)
{
     std::ofstream output(path);
     if (!output)
     {
          throw std::runtime_error("Could not write AST report file: " + path.string());
     }

     std::vector<std::string> canonicalSymbols = report.symbols;
     std::sort(canonicalSymbols.begin(), canonicalSymbols.end());
     canonicalSymbols.erase(std::unique(canonicalSymbols.begin(), canonicalSymbols.end()), canonicalSymbols.end());

    output << "{\n"
              "  \"schema\": \"apisig.ast-report.v1\",\n"
              "  \"format_version\": 1,\n"
                  "  \"symbols\": [\n";
     for (std::size_t i = 0; i < canonicalSymbols.size(); ++i)
     {
          output << "    \"" << EscapeJson(canonicalSymbols[i]) << "\"";
          if (i + 1 < canonicalSymbols.size())
          {
                output << ',';
          }
          output << '\n';
     }
     output << "  ],\n"
                 "  \"semantic_model\": [\n";
         for (std::size_t i = 0; i < report.semanticModel.size(); ++i)
         {
            output << "    \"" << EscapeJson(report.semanticModel[i]) << "\"";
            if (i + 1 < report.semanticModel.size())
            {
                output << ',';
            }
            output << '\n';
         }
         output << "  ],\n"
                  "  \"declarations\": [\n";

    auto writeStringArray = [&](const std::vector<std::string>& values, const char* fieldName, const char* indent) {
        output << indent << "\"" << fieldName << "\": [";
        if (!values.empty())
        {
            output << '\n';
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                output << indent << "  \"" << EscapeJson(values[index]) << "\"";
                if (index + 1 < values.size())
                {
                    output << ',';
                }
                output << '\n';
            }
            output << indent << ']';
            return;
        }

        output << ']';
    };

     for (std::size_t i = 0; i < report.declarations.size(); ++i)
     {
          const auto& declaration = report.declarations[i];
          output << "    {\n"
                        "      \"symbol\": \""
                    << EscapeJson(declaration.symbol)
                    << "\",\n"
                        "      \"kind\": \""
                    << EscapeJson(declaration.kind)
                    << "\",\n"
                        "      \"usr\": \""
                    << EscapeJson(declaration.usr)
                    << "\",\n"
                        "      \"qualified_name\": \""
                    << EscapeJson(declaration.qualifiedName)
                    << "\",\n"
                          "      \"api_signature\": \""
                       << EscapeJson(declaration.apiSignature)
                       << "\",\n"
                        "      \"file\": \""
                    << EscapeJson(declaration.file)
                    << "\",\n"
                        "      \"line\": "
                    << declaration.line
                    << ",\n"
                        "      \"column\": "
                       << declaration.column
                       << ",\n";

                output << "      \"enum_values\": [";
                if (!declaration.enumValues.empty())
                {
                    output << '\n';
                    for (std::size_t enumIndex = 0; enumIndex < declaration.enumValues.size(); ++enumIndex)
                    {
                        const auto& enumValue = declaration.enumValues[enumIndex];
                        output << "        {\"name\": \"" << EscapeJson(enumValue.name) << "\", \"value\": \""
                               << EscapeJson(enumValue.value) << "\"}";
                        if (enumIndex + 1 < declaration.enumValues.size())
                        {
                            output << ',';
                        }
                        output << '\n';
                    }
                    output << "      ],\n";
                }
                else
                {
                    output << "],\n";
                }

                writeStringArray(declaration.baseTypes, "base_types", "      ");
                output << ",\n";

                output << "      \"fields\": [";
                if (!declaration.fields.empty())
                {
                    output << '\n';
                    for (std::size_t fieldIndex = 0; fieldIndex < declaration.fields.size(); ++fieldIndex)
                    {
                        const auto& field = declaration.fields[fieldIndex];
                        output << "        {\"name\": \"" << EscapeJson(field.name) << "\", \"type\": \""
                               << EscapeJson(field.type) << "\"}";
                        if (fieldIndex + 1 < declaration.fields.size())
                        {
                            output << ',';
                        }
                        output << '\n';
                    }
                    output << "      ],\n";
                }
                else
                {
                    output << "],\n";
                }

                writeStringArray(declaration.methodSignatures, "method_signatures", "      ");
                output << "\n"
                          "    }";
          if (i + 1 < report.declarations.size())
          {
                output << ',';
          }
          output << '\n';
     }
     output << "  ],\n"
                  "  \"summary\": {\n"
                  "    \"symbol_count\": "
              << canonicalSymbols.size()
              << ",\n"
                 "    \"semantic_record_count\": "
              << report.semanticModel.size()
              << ",\n"
                  "    \"declaration_count\": "
              << report.declarations.size()
              << "\n"
                  "  }\n"
                  "}\n";
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

Baseline ReadSemanticModelBaseline(const std::filesystem::path& path)
{
    apisig::ComputeRequest request;
    request.semanticModel = apisig::ReadSemanticModelFromAstReport(path);

    const apisig::SignaturePair result = apisig::ComputeSignatures(request);
    return Baseline{result.apiHash, std::nullopt};
}

int CountSelectedInputModes(const CliOptions& options)
{
    int count = 0;
    if (options.symbolsFile.has_value())
    {
        ++count;
    }
    if (options.compdb.has_value())
    {
        ++count;
    }
    if (options.astReportJson.has_value())
    {
        ++count;
    }
    return count;
}

void ValidateOptions(const CliOptions& options)
{
    if (options.command != CommandType::Extract && options.astReportOut.has_value())
    {
        throw std::runtime_error("--ast-report-out is supported only with the extract command");
    }

    if (options.command == CommandType::Extract)
    {
        if (options.astReportJson.has_value())
        {
            throw std::runtime_error("--ast-report-json is not supported with the extract command");
        }

        if (options.metadataFile.has_value())
        {
            throw std::runtime_error("--metadata is not supported with the extract command");
        }

        if (options.outputFile.has_value())
        {
            throw std::runtime_error("--out is supported only with the snapshot command");
        }

        if (options.baselineFile.has_value())
        {
            throw std::runtime_error("--baseline is supported only with the compare command");
        }

        if (options.baselineAstReportJson.has_value())
        {
            throw std::runtime_error("--baseline-ast-report-json is supported only with the compare command");
        }

        if (CountSelectedInputModes(options) != 1)
        {
            throw std::runtime_error("extract requires exactly one input mode: --symbols or --compdb");
        }

        if (options.compdb.has_value() && !options.sourceRoot.has_value())
        {
            throw std::runtime_error("--source-root is required when --compdb is provided");
        }

        if (!options.compdb.has_value() && options.sourceRoot.has_value())
        {
            throw std::runtime_error("--source-root is only valid together with --compdb");
        }

        return;
    }

    if (options.sourceRoot.has_value() && !options.compdb.has_value())
    {
        throw std::runtime_error("--source-root is only valid together with --compdb");
    }

    if (options.command != CommandType::Snapshot && options.outputFile.has_value())
    {
        throw std::runtime_error("--out is supported only with the snapshot command");
    }

    if (options.command != CommandType::Compare && options.baselineFile.has_value())
    {
        throw std::runtime_error("--baseline is supported only with the compare command");
    }

    if (options.command != CommandType::Compare && options.baselineAstReportJson.has_value())
    {
        throw std::runtime_error("--baseline-ast-report-json is supported only with the compare command");
    }

    if (CountSelectedInputModes(options) != 1)
    {
        throw std::runtime_error(
            "compute, snapshot, and compare require exactly one input mode: --symbols, --compdb, or --ast-report-json");
    }

    if (options.compdb.has_value() && !options.sourceRoot.has_value())
    {
        throw std::runtime_error("--source-root is required when --compdb is provided");
    }

    if (options.command == CommandType::Compare)
    {
        const int baselineModeCount = (options.baselineFile.has_value() ? 1 : 0)
                                    + (options.baselineAstReportJson.has_value() ? 1 : 0);
        if (baselineModeCount != 1)
        {
            throw std::runtime_error(
                "compare requires exactly one baseline mode: --baseline or --baseline-ast-report-json");
        }

        if (options.baselineAstReportJson.has_value() && options.metadataFile.has_value())
        {
            throw std::runtime_error(
                "--metadata is not supported when compare uses --baseline-ast-report-json because rebuild_hash is not comparable in that mode");
        }
    }
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
        else if (arg == "--baseline-ast-report-json")
        {
            options.baselineAstReportJson = requireValue(arg);
        }
        else if (arg == "--ast-report-out")
        {
            options.astReportOut = requireValue(arg);
        }
        else if (arg == "--ast-report-json")
        {
            options.astReportJson = requireValue(arg);
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

    if (options.astReportJson.has_value())
    {
        request.semanticModel = apisig::ReadSemanticModelFromAstReport(options.astReportJson.value());
    }

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

        const apisig::ExtractionReport report = apisig::ExtractReportFromCompilationDatabase(
            options.compdb.value(),
            options.sourceRoot.value(),
            options.strictTooling,
            options.toolingStripArgPrefixes,
            options.toolingSuppressions);
        request.symbols = report.symbols;
        request.semanticModel = report.semanticModel;
    }
    else if (options.symbolsFile.has_value())
    {
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

        ValidateOptions(options);

        if (options.command == CommandType::Extract)
        {
            if (options.compdb.has_value())
            {
                if (!options.noToolingBanner)
                {
                    PrintToolingModeBanner(options.strictTooling);
                }

                const apisig::ExtractionReport report = apisig::ExtractReportFromCompilationDatabase(
                    options.compdb.value(),
                    options.sourceRoot.value(),
                    options.strictTooling,
                    options.toolingStripArgPrefixes,
                    options.toolingSuppressions);
                if (options.astReportOut.has_value())
                {
                    WriteAstReportJson(options.astReportOut.value(), report);
                }
                PrintSymbols(report.symbols, options.json);
                return ExitOk;
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

        const Baseline baseline = options.baselineAstReportJson.has_value()
                          ? ReadSemanticModelBaseline(options.baselineAstReportJson.value())
                                      : ReadBaseline(options.baselineFile.value());

        const bool apiChanged = (result.apiHash != baseline.apiHash);
        const bool rebuildChanged = baseline.rebuildHash.has_value() && (result.rebuildHash != baseline.rebuildHash.value());

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
                      << "    \"api_hash\": \"" << baseline.apiHash << "\"";
            if (baseline.rebuildHash.has_value())
            {
                std::cout << ",\n"
                          << "    \"rebuild_hash\": \"" << baseline.rebuildHash.value() << "\"\n";
            }
            else
            {
                std::cout << "\n";
            }
            std::cout << "  }\n"
                      << "}\n";
        }
        else
        {
            const std::filesystem::path baselinePath = options.baselineAstReportJson.has_value()
                                                           ? options.baselineAstReportJson.value()
                                                           : options.baselineFile.value();
            std::cout << "baseline=" << baselinePath.string() << '\n';
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
            if (baseline.rebuildHash.has_value())
            {
                std::cout << "baseline_rebuild_hash=" << baseline.rebuildHash.value() << '\n';
                std::cout << "current_rebuild_hash=" << result.rebuildHash << '\n';
            }
            else
            {
                std::cout << "rebuild_hash_comparison=unavailable\n";
            }
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
