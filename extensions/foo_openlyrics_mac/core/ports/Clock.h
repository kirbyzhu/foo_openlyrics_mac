#pragma once
#include <cstdint>

namespace openlyrics {

class Clock {
public:
    virtual ~Clock() = default;
    virtual int64_t nowMs() = 0;
};

}  // namespace openlyrics
