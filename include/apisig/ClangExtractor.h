#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace apisig
{
struct AstEnumValueRecord
{
    std::string name;
    std::string value;
};

struct AstFieldRecord
{
    std::string name;
    std::string type;
};

struct AstDeclarationRecord
{
    std::string symbol;
    std::string kind;
    std::string usr;
    std::string qualifiedName;
    std::string apiSignature;
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::vector<AstEnumValueRecord> enumValues;
    std::vector<std::string> baseTypes;
    std::vector<AstFieldRecord> fields;
    std::vector<std::string> methodSignatures;
};

struct ExtractionReport
{
    std::vector<std::string> symbols;
    std::vector<std::string> semanticModel;
    std::vector<AstDeclarationRecord> declarations;
};

ExtractionReport ExtractReportFromCompilationDatabase(
    const std::filesystem::path& compilationDatabase,
    const std::filesystem::path& sourceRoot,
    bool strictTooling = false,
    const std::vector<std::string>& extraToolingStripArgPrefixes = {},
    const std::vector<std::string>& extraToolingSuppressions = {});

std::vector<std::string> ExtractPublicSymbolsFromCompilationDatabase(
    const std::filesystem::path& compilationDatabase,
    const std::filesystem::path& sourceRoot,
    bool strictTooling = false,
    const std::vector<std::string>& extraToolingStripArgPrefixes = {},
    const std::vector<std::string>& extraToolingSuppressions = {});
} // namespace apisig
