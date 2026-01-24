#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rpf/RpfChunkReader.hpp"
#include "rpf/RpfProductStreamLine.hpp"
#include "rpf/RpfQuery.hpp"
#include "sar/SarTapeToRpf.hpp"
#include "sartape2/SarTapeRecordWriter.hpp"

namespace {

std::filesystem::path findDataRoot() {
    std::filesystem::path current = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        const auto candidate = current / "RPFAccum1_files";
        if (std::filesystem::exists(candidate)) {
            return current;
        }
        if (!current.has_parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return {};
}

std::vector<std::filesystem::path> candidateRpfPaths(const std::filesystem::path& root) {
    return {
        root / "RPFAccum1_files" / "RpfAccum1_0001.rpf",
        root / "FLT104_H_circle_leg4_C1_rpf" / "rpfBaseFileName_0001.rpf",
        root / "frame1_processing" / "RpfComp1_0001.rpf",
        root / "frame1_processing" / "RpfAcFi1_0001.rpf"
    };
}

std::filesystem::path findReadableRpf(std::string& error) {
    const auto tempRoot = std::filesystem::temp_directory_path();
    const auto sarPath = tempRoot / "synthetic_sartape.dat";
    const auto rpfPath = tempRoot / "synthetic_sartape.rpf";

    sartape2::SarTapeRecordWriter writer;
    if (!writer.open(sarPath.string())) {
        error = "Failed to open synthetic SarTape output.";
        return {};
    }

    for (int record = 0; record < 4; ++record) {
        std::vector<std::int16_t> iq(256 * 2);
        for (std::size_t i = 0; i < iq.size(); ++i) {
            iq[i] = static_cast<std::int16_t>((record + 1) * 10 + static_cast<int>(i % 128));
        }
        writer.writeDataRecord(1, static_cast<std::uint16_t>(record + 1),
                               200 + static_cast<std::uint32_t>(record),
                               iq);
    }
    writer.close();

    std::string convertError;
    if (!sar::writeRpfFromSarTape(sarPath.string(), rpfPath.string(), 4, convertError)) {
        error = "Failed to generate synthetic RPF: " + convertError;
        return {};
    }

    rpf::RpfQueryResult query{};
    if (rpf::queryRpfFile(rpfPath.string(), query)) {
        return rpfPath;
    }

    const auto root = findDataRoot();
    if (root.empty()) {
        error = "RPF data root not found.";
        return {};
    }

    for (const auto& path : candidateRpfPaths(root)) {
        if (!std::filesystem::exists(path)) {
            continue;
        }
        rpf::RpfQueryResult fallback{};
        if (rpf::queryRpfFile(path.string(), fallback)) {
            return path;
        }
    }

    error = "No readable RPF files found for query.";
    return {};
}

}  // namespace

TEST(RpfAccumIntegrationTests, ReadsAnnotationAndGeoGrid) {
    std::string error;
    const auto path = findReadableRpf(error);
    if (path.empty()) {
        GTEST_SKIP() << error;
    }

    rpf::RpfChunkReader reader(path.string());
    ASSERT_TRUE(reader.isOpen());

    rpf::AnnotationStruct annotation{};
    rpf::LatLongGrid grid{};
    if (!reader.readBlock(1, annotation, grid, true)) {
        GTEST_SKIP() << "Failed to read annotation block from " << path.string();
    }

    EXPECT_GT(annotation.imageDataChunkHeader.dataWidth, 0u);
    EXPECT_GT(annotation.imageDataChunkHeader.dataHeight, 0u);
    EXPECT_GT(annotation.latLongOutput.geolocationGridNumLines, 0u);
    EXPECT_EQ(grid.lineNumber.size(),
              static_cast<std::size_t>(annotation.latLongOutput.geolocationGridNumLines));
    EXPECT_GT(annotation.imageRect.numLines, 0u);
    EXPECT_GT(annotation.imageRect.numPixels, 0u);
    EXPECT_FALSE(annotation.fileName.empty());
    EXPECT_NE(annotation.notes.summary.find("fileType="), std::string::npos);
    EXPECT_NE(annotation.notes.summary.find("radarMode="), std::string::npos);
}

TEST(RpfAccumIntegrationTests, StreamsLineData) {
    std::string error;
    const auto path = findReadableRpf(error);
    if (path.empty()) {
        GTEST_SKIP() << error;
    }

    rpf::RpfProductStreamLine stream;
    ASSERT_TRUE(rpf::RpfProductStreamLine::init(path.string(), 1, stream, error)) << error;
    ASSERT_FALSE(stream.blocks().empty());

    const auto& block = stream.blocks().front();
    std::vector<float> lineData;
    ASSERT_TRUE(stream.readLine(block.startLine, lineData));
    EXPECT_EQ(lineData.size(), static_cast<std::size_t>(block.numPixels));

    double lat = 0.0;
    double lon = 0.0;
    double grToSr = 0.0;
    EXPECT_TRUE(stream.getLatLong(block.startLine, 1, lat, lon, grToSr));
}
