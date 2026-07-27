#include "apisig/Hash.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace
{
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
} // namespace

namespace apisig
{
std::string StableHashHex64(std::string_view payload)
{
    std::uint64_t hash = kFnvOffsetBasis;
    for (const unsigned char ch : payload)
    {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= kFnvPrime;
    }

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    return oss.str();
}
} // namespace apisig
