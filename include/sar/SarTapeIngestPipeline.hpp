#pragma once

#include <cstdint>
#include <string>

namespace sar {

struct SarSceneHeader;

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

std::uint32_t computeExpectedLines(const SarSceneHeader& header);

}  // namespace sar
