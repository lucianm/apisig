#include "apisig/SignatureEngine.h"

#include "apisig/Hash.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace
{
std::string BuildApiPayload(const std::vector<std::string>& symbols)
{
    std::set<std::string> normalized(symbols.begin(), symbols.end());

    std::ostringstream oss;
    for (const std::string& symbol : normalized)
    {
        oss << symbol << '\n';
    }
    return oss.str();
}

std::string BuildRebuildPayload(
    const std::string& apiHash,
    const std::vector<std::pair<std::string, std::string>>& metadata)
{
    std::ostringstream oss;
    oss << "api_hash=" << apiHash << '\n';
    for (const auto& [key, value] : metadata)
    {
        oss << key << '=' << value << '\n';
    }
    return oss.str();
}
} // namespace

namespace apisig
{
SignaturePair ComputeSignatures(const ComputeRequest& request)
{
    const std::string apiPayload = BuildApiPayload(request.symbols);
    const std::string apiHash = StableHashHex64(apiPayload);

    const std::string rebuildPayload = BuildRebuildPayload(apiHash, request.metadata);
    const std::string rebuildHash = StableHashHex64(rebuildPayload);

    return SignaturePair{apiHash, rebuildHash};
}
} // namespace apisig
