#include "rpf/RpfProductStream.hpp"

#include "rpf/RpfChunkReader.hpp"

namespace rpf {

RpfProductStream::RpfProductStream(std::string path)
    : path_(std::move(path)) {}

bool RpfProductStream::nextBlock(AnnotationStruct& annotation,
                                 LatLongGrid& grid,
                                 bool skipImageData) {
    RpfChunkReader reader(path_);
    if (!reader.isOpen()) {
        return false;
    }

    const bool ok = reader.readBlock(currentBlock_, annotation, grid, skipImageData);
    if (ok) {
        ++currentBlock_;
    }
    return ok;
}

}  // namespace rpf
