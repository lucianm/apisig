#include "apisig/ClangExtractor.h"

#include <stdexcept>

namespace apisig
{
std::vector<std::string> ExtractPublicSymbolsFromCompilationDatabase(
    const std::filesystem::path& compilationDatabase,
    const std::filesystem::path& sourceRoot)
{
#if defined(APISIG_HAVE_LIBTOOLING)
    (void)compilationDatabase;
    (void)sourceRoot;
    throw std::runtime_error(
        "LibTooling backend is enabled but extractor pass is not implemented yet. "
        "Use --symbols file-driven mode for now.");
#else
    (void)compilationDatabase;
    (void)sourceRoot;
    throw std::runtime_error(
        "LibTooling is not available in this build. "
        "Configure with Clang packages installed or use --symbols.");
#endif
}
} // namespace apisig
