#pragma once

namespace fixture {

enum class Mode
{
    Off = 0,
    On = 1
};

struct Pair
{
    int left;
    int right;

    int Sum() const;
};

} // namespace fixture
