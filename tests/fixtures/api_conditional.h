#pragma once

namespace fixture {

#ifdef APISIG_ALT_API
int ComputeValue(double value);
#else
int ComputeValue(int value);
#endif

} // namespace fixture
