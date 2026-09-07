#include "rto/RtoFrameBuffer.hpp"

#include <stdexcept>

namespace rto {

RtoFrameBuffer::RtoFrameBuffer(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument("RtoFrameBuffer capacity must be positive");
    }
}

void RtoFrameBuffer::push(RtoFrame frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= capacity_) {
        queue_.pop_front();
    }
    queue_.push_back(std::move(frame));
}

bool RtoFrameBuffer::pop(RtoFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    frame = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

std::size_t RtoFrameBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

}  // namespace rto
