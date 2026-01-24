#include "backproj/ImageWriter.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
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

}  // namespace

bool writeTiff(const std::string& path, const Eigen::MatrixXf& image) {
    if (image.size() == 0) {
        return false;
    }

    const std::uint32_t width = static_cast<std::uint32_t>(image.cols());
    const std::uint32_t height = static_cast<std::uint32_t>(image.rows());
    const std::uint32_t bytesPerSample = 2;
    const std::uint32_t pixelBytes = width * height * bytesPerSample;
    const std::uint32_t ifdOffset = 8 + pixelBytes;

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }

    output.put('I');
    output.put('I');
    writeU16Le(output, 42);
    writeU32Le(output, ifdOffset);

    const float minValue = image.minCoeff();
    const float maxValue = image.maxCoeff();
    const float range = (maxValue > minValue) ? (maxValue - minValue) : 1.0f;

    std::vector<std::uint16_t> pixels;
    pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int row = 0; row < image.rows(); ++row) {
        for (int col = 0; col < image.cols(); ++col) {
            const float normalized = (image(row, col) - minValue) / range;
            const float clamped = std::clamp(normalized, 0.0f, 1.0f);
            const auto value = static_cast<std::uint16_t>(clamped * 65535.0f + 0.5f);
            pixels.push_back(value);
        }
    }

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

    return static_cast<bool>(output);
}

}  // namespace backproj
