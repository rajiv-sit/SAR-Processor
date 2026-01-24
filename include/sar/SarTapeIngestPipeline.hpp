#pragma once

#include <cstdint>
#include <string>

namespace sar {

enum class ErrorPolicy {
    kFatal,
    kBestEffort
};

struct IngestOptions {
    ErrorPolicy errorPolicy = ErrorPolicy::kFatal;
    bool outputComplexIq = false;
};

class SarTapeIngestPipeline {
public:
    SarTapeIngestPipeline(std::string inputPath, std::string outputPrefix, IngestOptions options);

    std::uint32_t run();

private:
    std::string inputPath_;
    std::string outputPrefix_;
    IngestOptions options_;
};

}  // namespace sar
