#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "app/BackprojComparison.hpp"

namespace {

class BackprojComparisonTests : public ::testing::Test {
   protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() / ("comparison_" + std::to_string(stamp));
        std::filesystem::create_directory(root_);
        writeRaw("actual", 2, 1, {1.0f, 2.0f});
        writeRaw("reference", 2, 1, {1.0f, 2.0f});
        writeMeta("actual", "{\"width\":2,\"height\":1,\"minValue\":1,\"maxValue\":2}");
        writeMeta("reference", "{}");
    }

    static void writeU32(std::ofstream& output, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) output.put(static_cast<char>(value >> shift));
    }

    void writeRaw(const std::string& name, std::uint32_t width, std::uint32_t height,
                  const std::vector<float>& pixels) {
        std::ofstream output(root_ / (name + ".raw"), std::ios::binary);
        writeU32(output, width);
        writeU32(output, height);
        for (float value : pixels) writeU32(output, std::bit_cast<std::uint32_t>(value));
    }

    void writeMeta(const std::string& name, const std::string& json) {
        std::ofstream(root_ / (name + ".json")) << json;
    }

    int invoke(std::vector<std::string> args) {
        std::vector<const char*> argv;
        for (const auto& arg : args) argv.push_back(arg.c_str());
        output_.str("");
        errors_.str("");
        return app::runBackprojComparison(static_cast<int>(argv.size()), argv.data(), output_,
                                          errors_);
    }

    int compare(const std::vector<std::string>& options = {}) {
        std::vector<std::string> args{"compare",
                                      "--cpp-raw",
                                      (root_ / "actual.raw").string(),
                                      "--cpp-meta",
                                      (root_ / "actual.json").string(),
                                      "--ref-raw",
                                      (root_ / "reference.raw").string(),
                                      "--ref-meta",
                                      (root_ / "reference.json").string()};
        args.insert(args.end(), options.begin(), options.end());
        return invoke(args);
    }

    std::filesystem::path root_;
    std::ostringstream output_, errors_;
};

TEST_F(BackprojComparisonTests, ExactMatchPassesAndEveryDifferentPixelFailsByDefault) {
    EXPECT_EQ(compare(), 0);
    EXPECT_NE(output_.str().find("Result: PASS"), std::string::npos);
    EXPECT_TRUE(errors_.str().empty());
    writeRaw("actual", 2, 1, {1.0f, 2.5f});
    EXPECT_EQ(compare(), 2);
    EXPECT_NE(output_.str().find("Pixels outside tolerance: 1"), std::string::npos);
    EXPECT_NE(output_.str().find("Result: FAIL"), std::string::npos);
}

TEST_F(BackprojComparisonTests, AbsoluteAndRelativeToleranceIncludeBoundary) {
    writeRaw("actual", 2, 1, {1.25f, 2.5f});
    EXPECT_EQ(compare({"--atol", "0.5"}), 0);
    EXPECT_EQ(compare({"--atol", "0.49"}), 2);
    EXPECT_EQ(compare({"--rtol", "0.25"}), 0);
    EXPECT_EQ(compare({"--rtol", "0.24"}), 2);
    EXPECT_EQ(compare({"--atol", "0.25", "--rtol", "0.125"}), 0);
}

TEST_F(BackprojComparisonTests, RejectsDifferentDimensionsInsteadOfCropping) {
    writeRaw("actual", 1, 2, {1.0f, 2.0f});
    EXPECT_EQ(compare(), 1);
    EXPECT_NE(errors_.str().find("dimensions must match"), std::string::npos);
    writeRaw("actual", 2, 2, {1.0f, 2.0f, 3.0f, 4.0f});
    EXPECT_EQ(compare(), 1);
}

TEST_F(BackprojComparisonTests, RejectsEveryNonfiniteValueInEitherMatrix) {
    for (float value :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
          -std::numeric_limits<float>::infinity()}) {
        writeRaw("actual", 2, 1, {1.0f, value});
        writeRaw("reference", 2, 1, {1.0f, 2.0f});
        EXPECT_EQ(compare(), 1);
        writeRaw("actual", 2, 1, {1.0f, 2.0f});
        writeRaw("reference", 2, 1, {1.0f, value});
        EXPECT_EQ(compare(), 1);
    }
}

TEST_F(BackprojComparisonTests, RejectsMissingEmptyTruncatedAndTrailingRawData) {
    EXPECT_EQ(compare({"--cpp-raw", (root_ / "missing.raw").string()}), 1);
    EXPECT_EQ(compare({"--ref-raw", (root_ / "missing.raw").string()}), 1);
    std::ofstream(root_ / "actual.raw").put(0);
    EXPECT_EQ(compare(), 1);
    writeRaw("actual", 0, 1, {});
    EXPECT_EQ(compare(), 1);
    writeRaw("actual", 2, 1, {1.0f});
    EXPECT_EQ(compare(), 1);
    writeRaw("actual", 0xFFFFFFFFu, 0xFFFFFFFFu, {});
    EXPECT_EQ(compare(), 1);
    writeRaw("actual", 2, 1, {1.0f, 2.0f});
    std::ofstream(root_ / "actual.raw", std::ios::binary | std::ios::app).put('X');
    EXPECT_EQ(compare(), 1);
}

TEST_F(BackprojComparisonTests, RejectsInvalidTolerancesAndOverflow) {
    for (const auto* value : {"-1", "nan", "inf", "0.1junk", "garbage"}) {
        EXPECT_EQ(compare({"--atol", value}), 1);
    }
    EXPECT_EQ(compare({"--atol", "1e308", "--rtol", "1e308"}), 1);
}

TEST_F(BackprojComparisonTests, RejectsMalformedAndInconsistentMetadata) {
    EXPECT_EQ(compare({"--cpp-meta", (root_ / "missing.json").string()}), 1);
    EXPECT_EQ(compare({"--ref-meta", (root_ / "missing.json").string()}), 1);
    for (const auto* json : {"broken", "[]", "{\"width\":3}", "{\"height\":2}",
                             "{\"minValue\":null}", "{\"maxValue\":\"nan\"}"}) {
        writeMeta("actual", json);
        EXPECT_EQ(compare(), 1);
    }
}

TEST_F(BackprojComparisonTests, HelpAndArgumentErrorsAreActionable) {
    EXPECT_EQ(invoke({"compare", "--help"}), 0);
    EXPECT_NE(output_.str().find("--atol"), std::string::npos);
    EXPECT_EQ(invoke({"compare", "-h"}), 0);
    EXPECT_EQ(invoke({"compare"}), 1);
    EXPECT_EQ(invoke({"compare", "--cpp-raw"}), 1);
    EXPECT_EQ(invoke({"compare", "--unknown", "value"}), 1);
}

}  // namespace
