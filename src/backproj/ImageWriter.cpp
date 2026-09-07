#include "backproj/ImageWriter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <optional>
#include <vector>

namespace backproj {

namespace {

void writeU16Le(std::ofstream& output, std::uint16_t value) {
    output.put(static_cast<char>(value & 0xFF));
    output.put(static_cast<char>((value >> 8) & 0xFF));
}

void writeU32Le(std::ofstream& output, std::uint32_t value) {
    output.put(static_cast<char>(value & 0xFF));
    output.put(static_cast<char>((value >> 8) & 0xFF));
    output.put(static_cast<char>((value >> 16) & 0xFF));
    output.put(static_cast<char>((value >> 24) & 0xFF));
}

bool writeTiffBuffer(const std::string& path,
                     std::uint32_t width,
                     std::uint32_t height,
                     const std::vector<std::uint16_t>& pixels) {
    if (pixels.size() != static_cast<std::size_t>(width) * height) {
        return false;
    }

    const std::uint32_t pixelBytes =
        static_cast<std::uint32_t>(pixels.size()) * static_cast<std::uint32_t>(sizeof(std::uint16_t));
    const std::uint32_t ifdOffset = 8 + pixelBytes;

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }

    output.put('I');
    output.put('I');
    writeU16Le(output, 42);
    writeU32Le(output, ifdOffset);

    for (std::uint16_t value : pixels) {
        writeU16Le(output, value);
    }

    constexpr std::uint16_t entryCount = 9;
    writeU16Le(output, entryCount);

    auto writeTag = [&output](std::uint16_t tag,
                              std::uint16_t type,
                              std::uint32_t count,
                              std::uint32_t value) {
        writeU16Le(output, tag);
        writeU16Le(output, type);
        writeU32Le(output, count);
        writeU32Le(output, value);
    };

    writeTag(256, 4, 1, width);
    writeTag(257, 4, 1, height);
    writeTag(258, 3, 1, 16);
    writeTag(259, 3, 1, 1);
    writeTag(262, 3, 1, 1);
    writeTag(273, 4, 1, 8);
    writeTag(277, 3, 1, 1);
    writeTag(278, 4, 1, height);
    writeTag(279, 4, 1, pixelBytes);

    writeU32Le(output, 0);

    output.close();
    return static_cast<bool>(output);
}

bool computeQuantileRange(const Eigen::MatrixXf& image,
                          double lowerPercentile,
                          double upperPercentile,
                          float& lowerValue,
                          float& upperValue) {
    if (image.size() == 0) {
        return false;
    }

    std::vector<float> values;
    values.reserve(static_cast<std::size_t>(image.rows()) * static_cast<std::size_t>(image.cols()));
    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            values.push_back(image(row, col));
        }
    }

    const std::size_t size = values.size();
    auto indexForPercentile = [size](double percentile) -> std::size_t {
        if (percentile <= 0.0) {
            return 0;
        }
        if (percentile >= 1.0) {
            return size - 1;
        }
        return static_cast<std::size_t>(percentile * static_cast<double>(size - 1));
    };

    const std::size_t lowerIdx = indexForPercentile(lowerPercentile);
    const std::size_t upperIdx = indexForPercentile(upperPercentile);

    std::nth_element(values.begin(), values.begin() + lowerIdx, values.end());
    lowerValue = values[lowerIdx];
    std::nth_element(values.begin(), values.begin() + upperIdx, values.end());
    upperValue = values[upperIdx];

    if (upperValue < lowerValue) {
        std::swap(lowerValue, upperValue);
    }
    return true;
}

}  // namespace

bool writeTiff(const std::string& path, const Eigen::MatrixXf& image) {
    if (image.size() == 0) {
        return false;
    }

    const std::uint32_t width = static_cast<std::uint32_t>(image.cols());
    const std::uint32_t height = static_cast<std::uint32_t>(image.rows());
    const float minValue = image.minCoeff();
    const float maxValue = image.maxCoeff();
    const float range = (maxValue > minValue) ? (maxValue - minValue) : 1.0f;

    std::vector<std::uint16_t> pixels;
    pixels.reserve(static_cast<std::size_t>(width) * height);
    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            const float normalized = (image(row, col) - minValue) / range;
            const float clamped = std::clamp(normalized, 0.0f, 1.0f);
            const auto value = static_cast<std::uint16_t>(clamped * 65535.0f + 0.5f);
            pixels.push_back(value);
        }
    }

    return writeTiffBuffer(path, width, height, pixels);
}

bool writeNormalizedTiff(const std::string& path,
                         const Eigen::MatrixXf& image,
                         const ImageScalingParams& scaling) {
    if (image.size() == 0) {
        return false;
    }

    float lower = 0.0f;
    float upper = 0.0f;
    if (!computeQuantileRange(image, scaling.lowerPercentile, scaling.upperPercentile, lower, upper)) {
        return false;
    }

    const float range = (upper > lower) ? (upper - lower) : 1.0f;
    const float factor = static_cast<float>(scaling.outputScalingFactor);
    const std::uint32_t width = static_cast<std::uint32_t>(image.cols());
    const std::uint32_t height = static_cast<std::uint32_t>(image.rows());

    std::vector<std::uint16_t> pixels;
    pixels.reserve(static_cast<std::size_t>(width) * height);
    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            const float normalized = (image(row, col) - lower) / range;
            const float scaled = std::clamp(normalized * factor, 0.0f, 1.0f);
            const auto value = static_cast<std::uint16_t>(scaled * 65535.0f + 0.5f);
            pixels.push_back(value);
        }
    }

    return writeTiffBuffer(path, width, height, pixels);
}

bool writeRawFloat(const std::string& path, const Eigen::MatrixXf& image) {
    if (image.size() == 0) {
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }

    const std::uint32_t width = static_cast<std::uint32_t>(image.cols());
    const std::uint32_t height = static_cast<std::uint32_t>(image.rows());
    writeU32Le(output, width);
    writeU32Le(output, height);

    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            const float value = image(row, col);
            output.write(reinterpret_cast<const char*>(&value), sizeof(value));
        }
    }

    output.close();
    return static_cast<bool>(output);
}

bool readRawFloat(const std::string& path, Eigen::MatrixXf& image) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    auto readU32 = [&input]() -> std::optional<std::uint32_t> {
        std::uint32_t value = 0;
        char buffer[4];
        if (!input.read(buffer, sizeof(buffer))) {
            return std::nullopt;
        }
        value = static_cast<std::uint8_t>(buffer[0]) |
                (static_cast<std::uint8_t>(buffer[1]) << 8) |
                (static_cast<std::uint8_t>(buffer[2]) << 16) |
                (static_cast<std::uint8_t>(buffer[3]) << 24);
        return value;
    };

    const auto widthOpt = readU32();
    const auto heightOpt = readU32();
    if (!widthOpt.has_value() || !heightOpt.has_value()) {
        return false;
    }

    const std::uint32_t width = widthOpt.value();
    const std::uint32_t height = heightOpt.value();
    if (width == 0 || height == 0) {
        return false;
    }

    image.resize(static_cast<int>(height), static_cast<int>(width));
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t col = 0; col < width; ++col) {
            float value = 0.0f;
            if (!input.read(reinterpret_cast<char*>(&value), sizeof(value))) {
                return false;
            }
            image(static_cast<int>(row), static_cast<int>(col)) = value;
        }
    }

    return true;
}

}  // namespace backproj
