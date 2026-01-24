#include "rto/RtoLatencyStats.hpp"

namespace rto {

void RtoLatencyStats::reset() {
    count_ = 0;
    minNs_ = 0;
    maxNs_ = 0;
    meanNs_ = 0.0;
}

void RtoLatencyStats::update(std::uint64_t frameTimestampNs, std::uint64_t nowNs) {
    if (nowNs < frameTimestampNs) {
        return;
    }
    const std::uint64_t latency = nowNs - frameTimestampNs;
    if (count_ == 0) {
        minNs_ = latency;
        maxNs_ = latency;
        meanNs_ = static_cast<double>(latency);
        count_ = 1;
        return;
    }
    if (latency < minNs_) {
        minNs_ = latency;
    }
    if (latency > maxNs_) {
        maxNs_ = latency;
    }
    meanNs_ += (static_cast<double>(latency) - meanNs_) / static_cast<double>(count_ + 1);
    ++count_;
}

RtoLatencySnapshot RtoLatencyStats::snapshot() const {
    return {count_, minNs_, maxNs_, meanNs_};
}

}  // namespace rto
