#include <filesystem>

#include <gtest/gtest.h>

#include <Eigen/Core>

#include "rpf/RpfProductStream.hpp"
#include "rpf/RpfWriter.hpp"

TEST(RpfProductStreamTests, IteratesBlocksAndStops) {
    Eigen::MatrixXf image(1, 2);
    image << 1.0f, 2.0f;
    rpf::RpfWriteOptions options{};
    options.pixelType = 0;
    options.radarMode = 1;
    const auto path = std::filesystem::temp_directory_path() / "rpf_stream_0001.rpf";
    std::string error;
    ASSERT_TRUE(rpf::writeRpfFile(path.string(), image, options, error)) << error;

    rpf::RpfProductStream stream(path.string());
    rpf::AnnotationStruct annotation{};
    rpf::LatLongGrid grid{};
    EXPECT_TRUE(stream.nextBlock(annotation, grid, true));
    EXPECT_FALSE(stream.nextBlock(annotation, grid, true));
}

TEST(RpfProductStreamTests, FailsWhenFileMissing) {
    rpf::RpfProductStream stream("missing.rpf");
    rpf::AnnotationStruct annotation{};
    rpf::LatLongGrid grid{};
    EXPECT_FALSE(stream.nextBlock(annotation, grid, true));
}
