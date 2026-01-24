#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sar/SarTapeConstants.hpp"
#include "sar/SarTapeReader.hpp"

namespace {

std::filesystem::path makeTempPath(const std::string& stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (stem + "_" + std::to_string(now) + ".bin");
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

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

}  // namespace

TEST(SarTapeConstantsTests, RecordSizeIsNonZero) {
    EXPECT_GT(sar::SarTapeConstants::kRecordSize, 0u);
}

TEST(SarTapeReaderTests, ReadsDataRecordAndPreservesIqBytes) {
    auto record = buildRecord(sar::SarTapeConstants::kSyncWord,
                              sar::SarTapeConstants::kRecordTypeData,
                              1,
                              7,
                              123u);

    const std::size_t firstDataIndex = sar::SarTapeConstants::kRecordHeaderSize;
    record[firstDataIndex] = 0x10;
    record[firstDataIndex + 1] = 0x11;
    record[firstDataIndex + 2] = 0x12;
    record[firstDataIndex + 3] = 0x13;

    const auto path = makeTempPath("sar_record");
    writeFile(path, record);

    sar::SarTapeReader reader(path.string());
    sar::SarTraceRecord out{};
    ASSERT_TRUE(reader.readRecord(out));
    EXPECT_TRUE(out.header.syncValid);
    EXPECT_EQ(out.header.recordType, sar::SarTapeConstants::kRecordTypeData);
    EXPECT_EQ(out.header.recordNumber, 7u);
    EXPECT_EQ(out.header.timeStamp, 123u);

    EXPECT_EQ(out.iqBytes[firstDataIndex], 0x10);
    EXPECT_EQ(out.iqBytes[firstDataIndex + 1], 0x11);
    EXPECT_EQ(out.iqBytes[firstDataIndex + 2], 0x12);
    EXPECT_EQ(out.iqBytes[firstDataIndex + 3], 0x13);

    const std::size_t rampStart = sar::SarTapeConstants::kRecordSize - sar::SarTapeConstants::kTestRampSize;
    EXPECT_EQ(out.iqBytes[rampStart], 0);
    EXPECT_EQ(out.iqBytes.back(), 0);
}

TEST(SarTapeReaderTests, DummyRecordSwapsBytePairs) {
    auto record = buildRecord(sar::SarTapeConstants::kSyncWord,
                              sar::SarTapeConstants::kRecordTypeDummy,
                              1,
                              1,
                              42u);

    const std::size_t firstDataIndex = sar::SarTapeConstants::kRecordHeaderSize;
    record[firstDataIndex] = 0x01;
    record[firstDataIndex + 1] = 0x02;
    record[firstDataIndex + 2] = 0x03;
    record[firstDataIndex + 3] = 0x04;

    const auto path = makeTempPath("sar_dummy");
    writeFile(path, record);

    sar::SarTapeReader reader(path.string());
    sar::SarTraceRecord out{};
    ASSERT_TRUE(reader.readRecord(out));

    EXPECT_EQ(out.iqBytes[firstDataIndex], 0x02);
    EXPECT_EQ(out.iqBytes[firstDataIndex + 1], 0x01);
    EXPECT_EQ(out.iqBytes[firstDataIndex + 2], 0x04);
    EXPECT_EQ(out.iqBytes[firstDataIndex + 3], 0x03);
}
