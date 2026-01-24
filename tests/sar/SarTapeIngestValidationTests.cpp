#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "sar/SarTapeIngestPipeline.hpp"

namespace {

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

}  // namespace

TEST(SarTapeIngestValidationTests, MatchesReferenceDatWhenConfigured) {
    const char* inputPath = std::getenv("SAR_TAPE_INPUT");
    const char* refDatPath = std::getenv("SAR_TAPE_REF_DAT");
    const char* outputPrefix = std::getenv("SAR_TAPE_OUTPUT_PREFIX");

    if (!inputPath || !refDatPath || !outputPrefix) {
        GTEST_SKIP() << "Set SAR_TAPE_INPUT, SAR_TAPE_REF_DAT, SAR_TAPE_OUTPUT_PREFIX to run.";
    }

    sar::IngestOptions options{};
    options.errorPolicy = sar::ErrorPolicy::kFatal;
    sar::SarTapeIngestPipeline pipeline(inputPath, outputPrefix, options);
    pipeline.run();

    const std::filesystem::path generatedDat = std::string(outputPrefix) + ".dat";
    ASSERT_TRUE(std::filesystem::exists(generatedDat));
    ASSERT_TRUE(std::filesystem::exists(refDatPath));

    const auto generatedBytes = readFileBytes(generatedDat);
    const auto referenceBytes = readFileBytes(refDatPath);
    EXPECT_EQ(generatedBytes, referenceBytes);
}
