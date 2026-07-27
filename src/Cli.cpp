#include "apisig/ClangExtractor.h"
#include "apisig/Input.h"
#include "apisig/SignatureEngine.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
struct CliOptions
{
    std::optional<std::filesystem::path> symbolsFile;
    std::optional<std::filesystem::path> metadataFile;
    std::optional<std::filesystem::path> compdb;
    std::optional<std::filesystem::path> sourceRoot;
    bool json = false;
};

void PrintUsage()
{
    std::cout << "apisig compute [options]\n\n"
              << "Options:\n"
              << "  --symbols <file>     Public symbols file (one symbol per line)\n"
              << "  --metadata <file>    Metadata file (key=value per line)\n"
              << "  --compdb <file>      compile_commands.json for LibTooling mode\n"
              << "  --source-root <dir>  Source root used with --compdb\n"
              << "  --json               Print JSON output\n";
}

CliOptions ParseComputeArguments(const std::vector<std::string>& args)
{
    CliOptions options;

    for (std::size_t i = 0; i < args.size(); ++i)
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
        else if (arg == "--json")
        {
            options.json = true;
        }
        else
        {
            throw std::runtime_error("Unknown option: " + arg);
        }
    }

    return options;
}

void PrintResult(const apisig::SignaturePair& result, bool json)
{
    if (json)
    {
        std::cout << "{\n"
                  << "  \"api_hash\": \"" << result.apiHash << "\",\n"
                  << "  \"rebuild_hash\": \"" << result.rebuildHash << "\"\n"
                  << "}\n";
        return;
    }

    std::cout << "api_hash=" << result.apiHash << '\n';
    std::cout << "rebuild_hash=" << result.rebuildHash << '\n';
}
} // namespace

int RunCli(const std::vector<std::string>& args)
{
    if (args.empty() || args[0] != "compute")
    {
        PrintUsage();
        return 2;
    }

    try
    {
        const std::vector<std::string> computeArgs(args.begin() + 1, args.end());
        const CliOptions options = ParseComputeArguments(computeArgs);

        apisig::ComputeRequest request;

        if (options.compdb.has_value())
        {
            if (!options.sourceRoot.has_value())
            {
                throw std::runtime_error("--source-root is required when --compdb is provided");
            }

            request.symbols = apisig::ExtractPublicSymbolsFromCompilationDatabase(
                options.compdb.value(),
                options.sourceRoot.value());
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

        const apisig::SignaturePair result = apisig::ComputeSignatures(request);
        PrintResult(result, options.json);
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "apisig: " << ex.what() << '\n';
        return 1;
    }
}
