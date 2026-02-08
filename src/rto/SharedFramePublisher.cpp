#include "rto/SharedFramePublisher.hpp"

#include <cstring>
#include <limits>

namespace rto {

namespace {
constexpr std::size_t kHeaderSize = sizeof(SharedFrameHeader);
}  // namespace

SharedFramePublisher::SharedFramePublisher(std::filesystem::path path)
    : path_(std::move(path)) {}

bool SharedFramePublisher::ensureMapped() {
    if (mapping_.data()) {
        return true;
    }
    return mapping_.map(path_, kSharedMemorySize);
}

void SharedFramePublisher::flush(std::size_t length) {
    mapping_.flush(length);
}

bool SharedFramePublisher::publish(const RtoFrame& frame) {
    const std::size_t pixelBytes = frame.pixels.size() * sizeof(float);
    const std::size_t required = kHeaderSize + pixelBytes;
    if (required > kSharedMemorySize) {
        return false;
    }
    if (!ensureMapped()) {
        return false;
    }
    auto* data = static_cast<std::uint8_t*>(mapping_.data());
    auto* header = reinterpret_cast<SharedFrameHeader*>(data);
    header->width = frame.width;
    header->height = frame.height;
    header->timestampNs = frame.timestampNs;
    header->pixelCount = frame.pixels.size();
    header->version = ++version_;
    if (!frame.pixels.empty()) {
        std::memcpy(data + kHeaderSize, frame.pixels.data(), pixelBytes);
    }
    flush(required);
    return true;
}

}  // namespace rto
