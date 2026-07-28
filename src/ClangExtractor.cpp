#include "apisig/ClangExtractor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>

#if defined(APISIG_HAVE_LIBTOOLING) && APISIG_HAVE_LIBTOOLING
    #include <clang/AST/ASTConsumer.h>
    #include <clang/AST/Decl.h>
    #include <clang/AST/RecursiveASTVisitor.h>
    #include <clang/Frontend/CompilerInstance.h>
    #include <clang/Frontend/FrontendActions.h>
    #include <clang/Index/USRGeneration.h>
    #include <clang/Tooling/ArgumentsAdjusters.h>
    #include <clang/Tooling/CompilationDatabase.h>
    #include <clang/Tooling/JSONCompilationDatabase.h>
    #include <clang/Tooling/Tooling.h>
    #include <llvm/ADT/APSInt.h>
    #include <llvm/ADT/SmallString.h>
    #include <llvm/Config/llvm-config.h>
#endif

namespace
{
#if defined(APISIG_HAVE_LIBTOOLING) && APISIG_HAVE_LIBTOOLING
std::string BuildCanonicalSemanticRecord(const apisig::AstDeclarationRecord& declaration);

class SymbolCollector
{
public:
    void Add(const std::string& symbol, const apisig::AstDeclarationRecord& declaration)
    {
        if (!symbol.empty())
        {
            m_Symbols.insert(symbol);

            const std::string semanticRecord = BuildCanonicalSemanticRecord(declaration);
            m_SemanticModel.insert(semanticRecord);

            if (m_SeenDeclarationRecords.insert(semanticRecord).second)
            {
                m_Declarations.push_back(declaration);
            }
        }
    }

    apisig::ExtractionReport ToReport() const
    {
        std::vector<apisig::AstDeclarationRecord> declarations = m_Declarations;
        std::sort(declarations.begin(), declarations.end(), [](const apisig::AstDeclarationRecord& left, const apisig::AstDeclarationRecord& right) {
            if (left.symbol != right.symbol)
            {
                return left.symbol < right.symbol;
            }
            if (left.qualifiedName != right.qualifiedName)
            {
                return left.qualifiedName < right.qualifiedName;
            }
            return left.apiSignature < right.apiSignature;
        });

        return apisig::ExtractionReport{{m_Symbols.begin(), m_Symbols.end()}, {m_SemanticModel.begin(), m_SemanticModel.end()}, declarations};
    }

private:
    std::set<std::string> m_Symbols;
    std::set<std::string> m_SemanticModel;
    std::set<std::string> m_SeenDeclarationRecords;
    std::vector<apisig::AstDeclarationRecord> m_Declarations;
};

bool IsPathUnderRoot(const std::string& filePath, const std::filesystem::path& sourceRoot)
{
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(sourceRoot, ec);
    if (ec)
    {
        return true;
    }

    const auto canonicalFile = std::filesystem::weakly_canonical(std::filesystem::path(filePath), ec);
    if (ec)
    {
        return false;
    }

    const auto rootText = canonicalRoot.generic_string();
    const auto fileText = canonicalFile.generic_string();
    return fileText.rfind(rootText, 0) == 0;
}

std::string NormalizePathForReport(const std::string& filePath, const std::filesystem::path& sourceRoot)
{
    if (filePath.empty())
    {
        return filePath;
    }

    std::error_code ec;
    const std::filesystem::path absolutePath = std::filesystem::weakly_canonical(std::filesystem::path(filePath), ec);
    if (ec)
    {
        return filePath;
    }

    const std::filesystem::path rootPath = std::filesystem::weakly_canonical(sourceRoot, ec);
    if (ec)
    {
        return absolutePath.generic_string();
    }

    const std::filesystem::path relativePath = absolutePath.lexically_relative(rootPath);
    if (!relativePath.empty() && *relativePath.begin() != "..")
    {
        return relativePath.generic_string();
    }

    return absolutePath.generic_string();
}

std::string BuildFunctionSignature(const clang::FunctionDecl* decl)
{
    std::ostringstream oss;
    oss << decl->getReturnType().getAsString() << ' ' << decl->getQualifiedNameAsString() << '(';

    for (unsigned i = 0; i < decl->getNumParams(); ++i)
    {
        if (i > 0)
        {
            oss << ", ";
        }

        const clang::ParmVarDecl* param = decl->getParamDecl(i);
        oss << param->getType().getAsString();
        const std::string paramName = param->getNameAsString();
        if (!paramName.empty())
        {
            oss << ' ' << paramName;
        }
    }

    if (decl->isVariadic())
    {
        if (decl->getNumParams() > 0)
        {
            oss << ", ";
        }
        oss << "...";
    }

    oss << ')';

    if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(decl))
    {
        if (method->isConst())
        {
            oss << " const";
        }
        if (method->isVolatile())
        {
            oss << " volatile";
        }
    }

    return oss.str();
}

std::string BuildEnumValueText(const llvm::APSInt& value)
{
    llvm::SmallString<32> text;
    value.toString(text, 10);
    return text.str().str();
}

std::vector<apisig::AstEnumValueRecord> BuildEnumValues(const clang::EnumDecl* decl)
{
    std::vector<apisig::AstEnumValueRecord> values;
    values.reserve(static_cast<std::size_t>(std::distance(decl->enumerator_begin(), decl->enumerator_end())));

    for (const clang::EnumConstantDecl* constant : decl->enumerators())
    {
        values.push_back(apisig::AstEnumValueRecord{
            constant->getNameAsString(),
            BuildEnumValueText(constant->getInitVal())});
    }

    return values;
}

std::vector<std::string> BuildBaseTypes(const clang::CXXRecordDecl* decl)
{
    std::vector<std::string> bases;
    bases.reserve(static_cast<std::size_t>(std::distance(decl->bases_begin(), decl->bases_end())));

    for (const clang::CXXBaseSpecifier& base : decl->bases())
    {
        bases.push_back(base.getType().getAsString());
    }

    return bases;
}

std::vector<apisig::AstFieldRecord> BuildFields(const clang::CXXRecordDecl* decl)
{
    std::vector<apisig::AstFieldRecord> fields;
    fields.reserve(std::distance(decl->field_begin(), decl->field_end()));

    for (const clang::FieldDecl* field : decl->fields())
    {
        fields.push_back(apisig::AstFieldRecord{
            field->getNameAsString(),
            field->getType().getAsString()});
    }

    return fields;
}

std::vector<std::string> BuildMethodSignatures(const clang::CXXRecordDecl* decl)
{
    std::vector<std::string> methods;
    methods.reserve(std::distance(decl->method_begin(), decl->method_end()));

    for (const clang::CXXMethodDecl* method : decl->methods())
    {
        methods.push_back(BuildFunctionSignature(method));
    }

    return methods;
}

std::string EscapeSemanticToken(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value)
    {
        if (ch == '\\' || ch == '|' || ch == ';' || ch == ',' || ch == '=' || ch == '\n' || ch == '\r')
        {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string BuildCanonicalSemanticRecord(const apisig::AstDeclarationRecord& declaration)
{
    std::ostringstream oss;
    oss << "kind=" << EscapeSemanticToken(declaration.kind)
        << "|usr=" << EscapeSemanticToken(declaration.usr)
        << "|qname=" << EscapeSemanticToken(declaration.qualifiedName)
        << "|sig=" << EscapeSemanticToken(declaration.apiSignature)
        << "|enum=";

    std::vector<std::string> enumItems;
    enumItems.reserve(declaration.enumValues.size());
    for (const auto& enumValue : declaration.enumValues)
    {
        enumItems.push_back(EscapeSemanticToken(enumValue.name) + "=" + EscapeSemanticToken(enumValue.value));
    }
    std::sort(enumItems.begin(), enumItems.end());
    for (std::size_t index = 0; index < enumItems.size(); ++index)
    {
        if (index > 0)
        {
            oss << ';';
        }
        oss << enumItems[index];
    }

    oss << "|bases=";
    std::vector<std::string> bases = declaration.baseTypes;
    std::sort(bases.begin(), bases.end());
    for (std::size_t index = 0; index < bases.size(); ++index)
    {
        if (index > 0)
        {
            oss << ';';
        }
        oss << EscapeSemanticToken(bases[index]);
    }

    oss << "|fields=";
    std::vector<std::string> fieldItems;
    fieldItems.reserve(declaration.fields.size());
    for (const auto& field : declaration.fields)
    {
        fieldItems.push_back(EscapeSemanticToken(field.name) + ":" + EscapeSemanticToken(field.type));
    }
    std::sort(fieldItems.begin(), fieldItems.end());
    for (std::size_t index = 0; index < fieldItems.size(); ++index)
    {
        if (index > 0)
        {
            oss << ';';
        }
        oss << fieldItems[index];
    }

    oss << "|methods=";
    std::vector<std::string> methods = declaration.methodSignatures;
    std::sort(methods.begin(), methods.end());
    for (std::size_t index = 0; index < methods.size(); ++index)
    {
        if (index > 0)
        {
            oss << ';';
        }
        oss << EscapeSemanticToken(methods[index]);
    }

    return oss.str();
}

class PublicApiVisitor final : public clang::RecursiveASTVisitor<PublicApiVisitor>
{
public:
    PublicApiVisitor(clang::ASTContext& context, SymbolCollector& collector, const std::filesystem::path& sourceRoot)
        : m_Context(context)
        , m_Collector(collector)
        , m_SourceRoot(sourceRoot)
    {
    }

    bool VisitFunctionDecl(clang::FunctionDecl* decl)
    {
        if (!ShouldIncludeDecl(decl) || decl->isImplicit())
        {
            return true;
        }

        AddDecl("fn", decl);
        return true;
    }

    bool VisitCXXRecordDecl(clang::CXXRecordDecl* decl)
    {
        if (!ShouldIncludeDecl(decl) || !decl->isThisDeclarationADefinition() || decl->isImplicit())
        {
            return true;
        }

        AddDecl("type", decl);
        return true;
    }

    bool VisitEnumDecl(clang::EnumDecl* decl)
    {
        if (!ShouldIncludeDecl(decl) || !decl->isThisDeclarationADefinition())
        {
            return true;
        }

        AddDecl("enum", decl);
        return true;
    }

    bool VisitVarDecl(clang::VarDecl* decl)
    {
        if (!ShouldIncludeDecl(decl) || decl->isStaticLocal() || decl->isStaticDataMember())
        {
            return true;
        }

        AddDecl("var", decl);
        return true;
    }

private:
    bool ShouldIncludeDecl(const clang::NamedDecl* decl) const
    {
        if (decl == nullptr || !decl->hasExternalFormalLinkage())
        {
            return false;
        }

        const clang::SourceManager& sm = m_Context.getSourceManager();
        clang::SourceLocation loc = sm.getExpansionLoc(decl->getLocation());
        if (!loc.isValid())
        {
            return false;
        }

        const std::string filePath = sm.getFilename(loc).str();
        if (filePath.empty())
        {
            return false;
        }

        return IsPathUnderRoot(filePath, m_SourceRoot);
    }

    void AddDecl(const char* kind, const clang::NamedDecl* decl)
    {
        const clang::SourceManager& sm = m_Context.getSourceManager();
        clang::SourceLocation loc = sm.getExpansionLoc(decl->getLocation());

        std::string filePath = sm.getFilename(loc).str();
        std::uint32_t line = 0;
        std::uint32_t column = 0;
        if (loc.isValid())
        {
            const clang::PresumedLoc presumed = sm.getPresumedLoc(loc);
            if (presumed.isValid())
            {
                line = static_cast<std::uint32_t>(presumed.getLine());
                column = static_cast<std::uint32_t>(presumed.getColumn());
                if (filePath.empty() && presumed.getFilename() != nullptr)
                {
                    filePath = presumed.getFilename();
                }
            }
        }
        filePath = NormalizePathForReport(filePath, m_SourceRoot);

        const std::string kindText = kind;
        const std::string qualifiedName = decl->getQualifiedNameAsString();
        std::string apiSignature;
        std::vector<apisig::AstEnumValueRecord> enumValues;
        std::vector<std::string> baseTypes;
        std::vector<apisig::AstFieldRecord> fields;
        std::vector<std::string> methodSignatures;

        llvm::SmallString<256> usr;
        std::string usrText;
        if (!clang::index::generateUSRForDecl(decl, usr))
        {
            usrText = usr.str().str();
        }
        else
        {
            usrText = qualifiedName;
        }

        if (const auto* functionDecl = llvm::dyn_cast<clang::FunctionDecl>(decl))
        {
            apiSignature = BuildFunctionSignature(functionDecl);
        }
        else if (const auto* enumDecl = llvm::dyn_cast<clang::EnumDecl>(decl))
        {
            apiSignature = "enum " + qualifiedName;
            enumValues = BuildEnumValues(enumDecl);
        }
        else if (const auto* recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
        {
            apiSignature = "type " + qualifiedName;
            baseTypes = BuildBaseTypes(recordDecl);
            fields = BuildFields(recordDecl);
            methodSignatures = BuildMethodSignatures(recordDecl);
        }
        else if (const auto* varDecl = llvm::dyn_cast<clang::VarDecl>(decl))
        {
            apiSignature = varDecl->getType().getAsString() + " " + qualifiedName;
        }
        else
        {
            apiSignature = qualifiedName;
        }

        const std::string symbol = kindText + ":" + usrText;
        m_Collector.Add(symbol, apisig::AstDeclarationRecord{
                                symbol,
                                kindText,
                                usrText,
                                qualifiedName,
                                apiSignature,
                                filePath,
                                line,
                                column,
                                enumValues,
                                baseTypes,
                                fields,
                                methodSignatures});
    }
    clang::ASTContext& m_Context;
    SymbolCollector& m_Collector;
    std::filesystem::path m_SourceRoot;
};

class PublicApiConsumer final : public clang::ASTConsumer
{
public:
    PublicApiConsumer(SymbolCollector& collector, const std::filesystem::path& sourceRoot)
        : m_Collector(collector)
        , m_SourceRoot(sourceRoot)
    {
    }

    void HandleTranslationUnit(clang::ASTContext& context) override
    {
        PublicApiVisitor visitor(context, m_Collector, m_SourceRoot);
        visitor.TraverseDecl(context.getTranslationUnitDecl());
    }

private:
    SymbolCollector& m_Collector;
    std::filesystem::path m_SourceRoot;
};

class PublicApiAction final : public clang::ASTFrontendAction
{
public:
    PublicApiAction(SymbolCollector& collector, const std::filesystem::path& sourceRoot)
        : m_Collector(collector)
        , m_SourceRoot(sourceRoot)
    {
    }

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance&, llvm::StringRef) override
    {
        return std::make_unique<PublicApiConsumer>(m_Collector, m_SourceRoot);
    }

private:
    SymbolCollector& m_Collector;
    std::filesystem::path m_SourceRoot;
};

class PublicApiActionFactory final : public clang::tooling::FrontendActionFactory
{
public:
    PublicApiActionFactory(SymbolCollector& collector, const std::filesystem::path& sourceRoot)
        : m_Collector(collector)
        , m_SourceRoot(sourceRoot)
    {
    }

    std::unique_ptr<clang::FrontendAction> create() override
    {
        return std::make_unique<PublicApiAction>(m_Collector, m_SourceRoot);
    }

private:
    SymbolCollector& m_Collector;
    std::filesystem::path m_SourceRoot;
};

std::string ToLowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

clang::tooling::ArgumentsAdjuster CreateMsvcCompatAdjuster()
{
    return [](const clang::tooling::CommandLineArguments& arguments, llvm::StringRef)
    {
        clang::tooling::CommandLineArguments adjusted;
        adjusted.reserve(arguments.size() + 2);

        for (const std::string& argument : arguments)
        {
            const std::string lower = ToLowerAscii(argument);
            if (lower.rfind("/pathmap:", 0) == 0)
            {
                continue;
            }
            if (lower == "/c" || lower == "/wx" || lower == "/wx-")
            {
                continue;
            }
            if (lower == "-homeparams")
            {
                continue;
            }
            if (lower.rfind("/experimental:", 0) == 0)
            {
                continue;
            }

            adjusted.push_back(argument);
        }

        // hIC headers rely on MSVC-only pragma forms that clang reports as unknown.
        adjusted.push_back("-Wno-error");
        adjusted.push_back("-Wno-unknown-pragmas");
        adjusted.push_back("-Wno-error=unknown-pragmas");
        adjusted.push_back("-Wno-unused-command-line-argument");
        return adjusted;
    };
}

std::vector<std::string> BuildExplicitSuppressionFlags(const std::vector<std::string>& suppressions)
{
    std::vector<std::string> flags;
    flags.reserve(suppressions.size());

    for (const std::string& suppression : suppressions)
    {
        std::string value = suppression;
        value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }), value.end());
        if (value.empty())
        {
            continue;
        }

        const std::string lower = ToLowerAscii(value);
        if (lower.rfind("-wno-", 0) == 0 || lower.rfind("/wd", 0) == 0)
        {
            flags.push_back(value);
            continue;
        }
        if (lower.rfind("-w", 0) == 0)
        {
            flags.push_back("-Wno-" + value.substr(2));
            continue;
        }

        // Accept MSVC warning code forms: C4100 or 4100.
           if ((value.size() == 5 && (value[0] == 'C' || value[0] == 'c')
               && std::all_of(value.begin() + 1, value.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
              || (value.size() == 4 && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; })))
        {
            const std::string digits = (value.size() == 5) ? value.substr(1) : value;
            flags.push_back("/wd" + digits);
            continue;
        }

        // Treat all other values as clang warning groups, e.g. unknown-pragmas.
        flags.push_back("-Wno-" + value);
    }

    return flags;
}

std::vector<std::string> BuildLowerStripPrefixes(const std::vector<std::string>& stripPrefixes)
{
    std::vector<std::string> lowered;
    lowered.reserve(stripPrefixes.size());

    for (const std::string& prefix : stripPrefixes)
    {
        std::string value = prefix;
        value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }), value.end());
        if (value.empty())
        {
            continue;
        }

        lowered.push_back(ToLowerAscii(value));
    }

    return lowered;
}

clang::tooling::ArgumentsAdjuster CreateExplicitStripArgAdjuster(const std::vector<std::string>& stripPrefixes)
{
    const std::vector<std::string> loweredPrefixes = BuildLowerStripPrefixes(stripPrefixes);
    return [loweredPrefixes](const clang::tooling::CommandLineArguments& arguments, llvm::StringRef)
    {
        if (loweredPrefixes.empty())
        {
            return arguments;
        }

        clang::tooling::CommandLineArguments adjusted;
        adjusted.reserve(arguments.size());
        for (const std::string& argument : arguments)
        {
            const std::string lowerArg = ToLowerAscii(argument);
            bool shouldStrip = false;
            for (const std::string& prefix : loweredPrefixes)
            {
                if (lowerArg.rfind(prefix, 0) == 0)
                {
                    shouldStrip = true;
                    break;
                }
            }

            if (!shouldStrip)
            {
                adjusted.push_back(argument);
            }
        }

        return adjusted;
    };
}

clang::tooling::ArgumentsAdjuster CreateExplicitSuppressionsAdjuster(const std::vector<std::string>& suppressions)
{
    const std::vector<std::string> flags = BuildExplicitSuppressionFlags(suppressions);
    return [flags](const clang::tooling::CommandLineArguments& arguments, llvm::StringRef)
    {
        if (flags.empty())
        {
            return arguments;
        }

        clang::tooling::CommandLineArguments adjusted = arguments;
        adjusted.reserve(arguments.size() + flags.size());
        for (const std::string& flag : flags)
        {
            adjusted.push_back(flag);
        }
        return adjusted;
    };
}
#endif
} // namespace

namespace apisig
{
ExtractionReport ExtractReportFromCompilationDatabase(
    const std::filesystem::path& compilationDatabase,
    const std::filesystem::path& sourceRoot,
    bool strictTooling,
    const std::vector<std::string>& extraToolingStripArgPrefixes,
    const std::vector<std::string>& extraToolingSuppressions)
{
#if defined(APISIG_HAVE_LIBTOOLING) && APISIG_HAVE_LIBTOOLING

    std::string error;
    std::unique_ptr<clang::tooling::CompilationDatabase> database =
    #if LLVM_VERSION_MAJOR >= 22
        clang::tooling::JSONCompilationDatabase::loadFromFile(
            compilationDatabase.string(),
            error,
            clang::tooling::JSONCommandLineSyntax::AutoDetect);
    #else
        clang::tooling::JSONCompilationDatabase::loadFromFile(compilationDatabase.string(), error);
    #endif
    if (!database)
    {
        throw std::runtime_error("Could not load compile_commands.json: " + error);
    }

    std::vector<std::string> files = database->getAllFiles();
    std::vector<std::string> selectedFiles;
    selectedFiles.reserve(files.size());
    for (const std::string& file : files)
    {
        if (IsPathUnderRoot(file, sourceRoot))
        {
            selectedFiles.push_back(file);
        }
    }
    if (selectedFiles.empty())
    {
        selectedFiles = std::move(files);
    }

    SymbolCollector collector;
    clang::tooling::ClangTool tool(*database, selectedFiles);
    if (!strictTooling)
    {
        tool.appendArgumentsAdjuster(CreateMsvcCompatAdjuster());
    }
    tool.appendArgumentsAdjuster(CreateExplicitStripArgAdjuster(extraToolingStripArgPrefixes));
    tool.appendArgumentsAdjuster(CreateExplicitSuppressionsAdjuster(extraToolingSuppressions));
    PublicApiActionFactory factory(collector, sourceRoot);
    if (tool.run(&factory) != 0)
    {
        throw std::runtime_error("LibTooling extraction failed while parsing translation units.");
    }

    return collector.ToReport();
#else
    (void)compilationDatabase;
    (void)sourceRoot;
    (void)strictTooling;
    (void)extraToolingStripArgPrefixes;
    (void)extraToolingSuppressions;
    throw std::runtime_error(
        "LibTooling is not available in this build. "
        "Configure with Clang packages installed or use --symbols.");
#endif
}

std::vector<std::string> ExtractPublicSymbolsFromCompilationDatabase(
    const std::filesystem::path& compilationDatabase,
    const std::filesystem::path& sourceRoot,
    bool strictTooling,
    const std::vector<std::string>& extraToolingStripArgPrefixes,
    const std::vector<std::string>& extraToolingSuppressions)
{
    return ExtractReportFromCompilationDatabase(
               compilationDatabase,
               sourceRoot,
               strictTooling,
               extraToolingStripArgPrefixes,
               extraToolingSuppressions)
        .symbols;
}
} // namespace apisig
