#pragma once

#include <string>
#include <string_view>

namespace apisig
{
std::string StableHashHex64(std::string_view payload);
} // namespace apisig
