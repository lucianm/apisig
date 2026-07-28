#pragma once

#include <string>
#include <vector>

namespace apisig
{
struct SignaturePair
{
    std::string apiHash;
    std::string rebuildHash;
};

struct ComputeRequest
{
    std::vector<std::string> symbols;
    std::vector<std::string> semanticModel;
    std::vector<std::pair<std::string, std::string>> metadata;
};
} // namespace apisig
