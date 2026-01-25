#pragma once

#include <cstdint>
#include <string>

#include <Eigen/Core>

namespace rpf {

struct RpfWriteOptions {
    std::int32_t frameSeqNum = 1;
    std::int32_t pixelType = 2;
    std::int32_t rspInhibit = 0;
    std::uint16_t pixelMarginStart = 0;
    std::uint16_t pixelMarginEnd = 0;
    std::uint16_t lineMarginStart = 0;
    std::uint16_t lineMarginEnd = 0;
    std::int32_t fileType = 0;
    std::int32_t radarMode = 2;
    std::uint16_t geolocationGridNumLines = 4;
    std::uint32_t startLine = 1;
    std::uint32_t startPixel = 1;
    std::string fileId;
};

bool writeRpfFile(const std::string& path,
                  const Eigen::MatrixXf& image,
                  const RpfWriteOptions& options,
                  std::string& error);

}  // namespace rpf
