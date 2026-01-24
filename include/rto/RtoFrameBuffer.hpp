#pragma once

#include <deque>
#include <mutex>

#include "rto/RtoFrame.hpp"

namespace rto {

class RtoFrameBuffer {
public:
    explicit RtoFrameBuffer(std::size_t capacity);

    void push(RtoFrame frame);
    bool pop(RtoFrame& frame);
    std::size_t size() const;

private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<RtoFrame> queue_;
};

}  // namespace rto
