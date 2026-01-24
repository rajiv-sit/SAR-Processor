#include <chrono>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sar/SarTapeConstants.hpp"
#include "sar/SarTapeIngestPipeline.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem, const std::string& suffix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now) + suffix);
}

void writeU16Be(std::vector<std::uint8_t>& buffer, std::size_t offset, std::uint16_t value) {
    buffer[offset] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 1] = static_cast<std::uint8_t>(value & 0xFF);
}

void writeU32Be(std::vector<std::uint8_t>& buffer, std::size_t offset, std::uint32_t value) {
    buffer[offset] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    buffer[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

std::vector<std::uint8_t> buildRecord(std::uint32_t syncWord,
                                      std::uint16_t recordType,
                                      std::uint16_t sceneNumber,
                                      std::uint16_t recordNumber,
                                      std::uint32_t timeStamp) {
    std::vector<std::uint8_t> record(sar::SarTapeConstants::kRecordSize, 0);
    writeU32Be(record, 0, syncWord);
    const std::uint16_t packed = static_cast<std::uint16_t>((recordType << 12) | (sceneNumber & 0x0FFF));
    std::size_t offset = 4;
    for (int i = 0; i < 3; ++i) {
        writeU16Be(record, offset, packed);
        writeU16Be(record, offset + 2, recordNumber);
        writeU32Be(record, offset + 4, timeStamp);
        offset += 8;
    }
    return record;
}

void appendRecord(std::ofstream& output, const std::vector<std::uint8_t>& record) {
    output.write(reinterpret_cast<const char*>(record.data()), static_cast<std::streamsize>(record.size()));
}

std::uintmax_t fileSizeOrZero(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return 0;
    }
    return std::filesystem::file_size(path);
}

}  // namespace

TEST(SarTapeIngestPipelineTests, FatalPolicyStopsOnInvalidSync) {
    const auto inputPath = makeTempPath("sar_ingest_input", ".dat");
    std::ofstream output(inputPath, std::ios::binary);
    ASSERT_TRUE(output);

    appendRecord(output, buildRecord(0xDEADBEEF,
                                     sar::SarTapeConstants::kRecordTypeData,
                                     1,
                                     1,
                                     10u));
    appendRecord(output, buildRecord(sar::SarTapeConstants::kSyncWord,
                                     sar::SarTapeConstants::kRecordTypeData,
                                     1,
                                     2,
                                     11u));
    output.close();

    const auto outputPrefix = makeTempPath("sar_ingest_output", "");
    sar::IngestOptions options{};
    options.errorPolicy = sar::ErrorPolicy::kFatal;
    sar::SarTapeIngestPipeline pipeline(inputPath.string(), outputPrefix.string(), options);
    const std::uint32_t linesWritten = pipeline.run();

    EXPECT_EQ(linesWritten, 0u);
    EXPECT_EQ(fileSizeOrZero(outputPrefix.string() + ".dat"), 0u);
}

TEST(SarTapeIngestPipelineTests, BestEffortWritesValidRecords) {
    const auto inputPath = makeTempPath("sar_ingest_input_best", ".dat");
    std::ofstream output(inputPath, std::ios::binary);
    ASSERT_TRUE(output);

    appendRecord(output, buildRecord(0xDEADBEEF,
                                     sar::SarTapeConstants::kRecordTypeData,
                                     1,
                                     1,
                                     10u));
    appendRecord(output, buildRecord(sar::SarTapeConstants::kSyncWord,
                                     sar::SarTapeConstants::kRecordTypeData,
                                     1,
                                     2,
                                     11u));
    output.close();

    const auto outputPrefix = makeTempPath("sar_ingest_output_best", "");
    sar::IngestOptions options{};
    options.errorPolicy = sar::ErrorPolicy::kBestEffort;
    sar::SarTapeIngestPipeline pipeline(inputPath.string(), outputPrefix.string(), options);
    const std::uint32_t linesWritten = pipeline.run();

    EXPECT_EQ(linesWritten, 2u);
    EXPECT_EQ(fileSizeOrZero(outputPrefix.string() + ".dat"),
              sar::SarTapeConstants::kRecordSize * 2u);
}

TEST(SarTapeIngestPipelineTests, ComplexOutputWritesFloatPairs) {
    const auto inputPath = makeTempPath("sar_ingest_input_complex", ".dat");
    std::ofstream output(inputPath, std::ios::binary);
    ASSERT_TRUE(output);

    appendRecord(output, buildRecord(sar::SarTapeConstants::kSyncWord,
                                     sar::SarTapeConstants::kRecordTypeData,
                                     1,
                                     1,
                                     10u));
    output.close();

    const auto outputPrefix = makeTempPath("sar_ingest_output_complex", "");
    sar::IngestOptions options{};
    options.errorPolicy = sar::ErrorPolicy::kBestEffort;
    options.outputComplexIq = true;
    sar::SarTapeIngestPipeline pipeline(inputPath.string(), outputPrefix.string(), options);
    const std::uint32_t linesWritten = pipeline.run();

    EXPECT_EQ(linesWritten, 1u);
    const std::uintmax_t expectedSamples = sar::SarTapeConstants::kRecordSize / 2u;
    const std::uintmax_t expectedBytes = expectedSamples * sizeof(std::complex<float>);
    EXPECT_EQ(fileSizeOrZero(outputPrefix.string() + ".dat"), expectedBytes);
}
