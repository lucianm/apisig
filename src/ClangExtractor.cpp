#include "apisig/ClangExtractor.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>

#if defined(APISIG_HAVE_LIBTOOLING) && APISIG_HAVE_LIBTOOLING
    #include <clang/AST/ASTConsumer.h>
    #include <clang/AST/Decl.h>
    #include <clang/AST/RecursiveASTVisitor.h>
    #include <clang/Frontend/CompilerInstance.h>
    #include <clang/Frontend/FrontendActions.h>
    #include <clang/Index/USRGeneration.h>
    #include <clang/Tooling/CompilationDatabase.h>
    #include <clang/Tooling/JSONCompilationDatabase.h>
    #include <clang/Tooling/Tooling.h>
    #include <llvm/ADT/SmallString.h>
#endif

namespace
{
#if defined(APISIG_HAVE_LIBTOOLING) && APISIG_HAVE_LIBTOOLING
class SymbolCollector
{
public:
    void Add(const std::string& symbol)
    {
        if (!symbol.empty())
        {
            m_Symbols.insert(symbol);
        }
    }

    std::vector<std::string> ToVector() const
    {
        return {m_Symbols.begin(), m_Symbols.end()};
    }

private:
    std::set<std::string> m_Symbols;
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
        llvm::SmallString<256> usr;
        if (!clang::index::generateUSRForDecl(decl, usr))
        {
            m_Collector.Add(std::string(kind) + ":" + usr.str().str());
            return;
        }

        m_Collector.Add(std::string(kind) + ":" + decl->getQualifiedNameAsString());
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
#endif
} // namespace

namespace apisig
{
std::vector<std::string> ExtractPublicSymbolsFromCompilationDatabase(
    const std::filesystem::path& compilationDatabase,
    const std::filesystem::path& sourceRoot)
{
#if defined(APISIG_HAVE_LIBTOOLING) && APISIG_HAVE_LIBTOOLING

    std::string error;
    std::unique_ptr<clang::tooling::CompilationDatabase> database =
        clang::tooling::JSONCompilationDatabase::loadFromFile(compilationDatabase.string(), error);
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
    PublicApiActionFactory factory(collector, sourceRoot);
    if (tool.run(&factory) != 0)
    {
        throw std::runtime_error("LibTooling extraction failed while parsing translation units.");
    }

    return collector.ToVector();
#else
    (void)compilationDatabase;
    (void)sourceRoot;
    throw std::runtime_error(
        "LibTooling is not available in this build. "
        "Configure with Clang packages installed or use --symbols.");
#endif
}
} // namespace apisig
