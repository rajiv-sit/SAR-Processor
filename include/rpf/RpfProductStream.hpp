#pragma once

#include <cstdint>
#include <string>

#include "rpf/AnnotationStruct.hpp"
#include "rpf/LatLongGrid.hpp"

namespace rpf {

class RpfProductStream {
public:
    explicit RpfProductStream(std::string path);

    bool nextBlock(AnnotationStruct& annotation,
                   LatLongGrid& grid,
                   bool skipImageData);

private:
    std::string path_;
    std::uint32_t currentBlock_ = 1;
};

}  // namespace rpf
