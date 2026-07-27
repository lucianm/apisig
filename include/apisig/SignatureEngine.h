#pragma once

#include "apisig/SignatureTypes.h"

namespace apisig
{
SignaturePair ComputeSignatures(const ComputeRequest& request);
} // namespace apisig
