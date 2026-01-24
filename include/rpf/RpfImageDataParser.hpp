#pragma once

#include <cstdint>

#include <Eigen/Core>

#include "rpf/AnnotationStruct.hpp"

namespace rpf {

class RpfImageDataParser {
public:
    bool parseImageData(std::ifstream& input,
                        const ImageDataChunkHeader& header,
                        bool skipImageData,
                        Eigen::MatrixXf& outImage);
};

}  // namespace rpf
