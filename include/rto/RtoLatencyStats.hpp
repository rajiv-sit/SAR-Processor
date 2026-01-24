#pragma once

#include <cstdint>

namespace rto {

struct RtoLatencySnapshot {
    std::uint64_t count = 0;
    std::uint64_t minNs = 0;
    std::uint64_t maxNs = 0;
    double meanNs = 0.0;
};

class RtoLatencyStats {
public:
    void reset();
    void update(std::uint64_t frameTimestampNs, std::uint64_t nowNs);
    RtoLatencySnapshot snapshot() const;

private:
    std::uint64_t count_ = 0;
    std::uint64_t minNs_ = 0;
    std::uint64_t maxNs_ = 0;
    double meanNs_ = 0.0;
};

}  // namespace rto
