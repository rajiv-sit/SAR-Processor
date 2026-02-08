#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "rto/MemoryMappedFile.hpp"
#include "rto/RtoFrame.hpp"

namespace rto {

struct SharedFrameHeader {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t timestampNs = 0;
    std::uint64_t version = 0;
    std::uint64_t pixelCount = 0;
};

static_assert(sizeof(SharedFrameHeader) == 32, "Unexpected header size");

class SharedFramePublisher {
public:
    static constexpr std::size_t kSharedMemorySize = 64ULL * 1024ULL * 1024ULL;

    explicit SharedFramePublisher(std::filesystem::path path);
    SharedFramePublisher(const SharedFramePublisher&) = delete;
    SharedFramePublisher& operator=(const SharedFramePublisher&) = delete;

    bool publish(const RtoFrame& frame);

private:
    bool ensureMapped();
    void flush(std::size_t length);

    std::filesystem::path path_;
    MemoryMappedFile mapping_;
    std::uint64_t version_ = 0;
};

}  // namespace rto
