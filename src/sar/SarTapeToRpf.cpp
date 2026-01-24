#include "sar/SarTapeToRpf.hpp"

#include <cmath>
#include <string>
#include <vector>

#include <Eigen/Core>

#include "rpf/RpfWriter.hpp"
#include "sar/SarTapeConstants.hpp"
#include "sar/SarTapeReader.hpp"

namespace sar {

bool writeRpfFromSarTape(const std::string& sarTapePath,
                         const std::string& rpfPath,
                         std::uint32_t maxLines,
                         std::string& error) {
    SarTapeReader reader(sarTapePath);
    if (!reader.isOpen()) {
        error = "Failed to open SarTape input.";
        return false;
    }

    std::vector<std::vector<float>> lines;
    std::size_t width = 0;

    SarTraceRecord record;
    while (reader.readRecord(record)) {
        if (record.header.recordType != SarTapeConstants::kRecordTypeData) {
            continue;
        }

        const std::size_t samples = record.iqBytes.size() / 2;
        if (samples == 0) {
            continue;
        }

        if (width == 0) {
            width = samples;
        }

        std::vector<float> line(width, 0.0f);
        const std::size_t maxSamples = std::min(width, samples);
        for (std::size_t i = 0; i < maxSamples; ++i) {
            const float iVal = static_cast<float>(record.iqBytes[2 * i]);
            const float qVal = static_cast<float>(record.iqBytes[2 * i + 1]);
            line[i] = std::sqrt(iVal * iVal + qVal * qVal);
        }
        lines.push_back(std::move(line));

        if (maxLines > 0 && lines.size() >= maxLines) {
            break;
        }
    }

    if (lines.empty() || width == 0) {
        error = "No SarTape data records were converted.";
        return false;
    }

    Eigen::MatrixXf image(static_cast<int>(lines.size()), static_cast<int>(width));
    for (std::size_t row = 0; row < lines.size(); ++row) {
        for (std::size_t col = 0; col < width; ++col) {
            image(static_cast<int>(row), static_cast<int>(col)) = lines[row][col];
        }
    }

    rpf::RpfWriteOptions options{};
    options.pixelType = 2;
    options.radarMode = 1;
    options.fileType = 0;
    options.geolocationGridNumLines = 4;

    return rpf::writeRpfFile(rpfPath, image, options, error);
}

}  // namespace sar
