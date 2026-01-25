#include <filesystem>

#include <gtest/gtest.h>

#include "sar/SarTapeToRpf.hpp"
#include "sartape2/SarTapeRecordWriter.hpp"

TEST(SarTapeToRpfTests, FailsWhenInputMissing) {
    std::string error;
    EXPECT_FALSE(sar::writeRpfFromSarTape("missing.dat", "out.rpf", 1, error));
    EXPECT_FALSE(error.empty());
}

TEST(SarTapeToRpfTests, FailsWhenNoDataRecords) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto input = tempDir / "sartape_no_data.dat";
    const auto output = tempDir / "sartape_no_data.rpf";

    sartape2::SarTapeRecordWriter writer;
    ASSERT_TRUE(writer.open(input.string()));
    EXPECT_TRUE(writer.writeSceneHeader(1, 100));
    writer.close();

    std::string error;
    EXPECT_FALSE(sar::writeRpfFromSarTape(input.string(), output.string(), 1, error));
    EXPECT_FALSE(error.empty());
}

TEST(SarTapeToRpfTests, WritesRpfFromValidSarTape) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto input = tempDir / "sartape_with_data.dat";
    const auto output = tempDir / "sartape_with_data.rpf";

    std::vector<std::int16_t> iq(256 * 2);
    for (std::size_t i = 0; i < iq.size(); ++i) {
        iq[i] = static_cast<std::int16_t>(i);
    }

    sartape2::SarTapeRecordWriter writer;
    ASSERT_TRUE(writer.open(input.string()));
    for (int record = 0; record < 4; ++record) {
        EXPECT_TRUE(writer.writeDataRecord(1, static_cast<std::uint16_t>(record + 1),
                                           static_cast<std::uint32_t>(200 + record),
                                           iq));
    }
    writer.close();

    std::string error;
    EXPECT_TRUE(sar::writeRpfFromSarTape(input.string(), output.string(), 2, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(output));
}
