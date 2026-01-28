#pragma once

#include <vector>

#include "sar/SarSceneHeader.hpp"
#include "sar/SarTraceRecord.hpp"

namespace sar {

struct SarTargetPositionMessage;

class ISarTapeReader {
public:
    virtual ~ISarTapeReader() = default;

    virtual bool isOpen() const = 0;
    virtual bool hasSceneHeader() const = 0;
    virtual const SarSceneHeader& sceneHeader() const = 0;
    virtual bool hasAccessoryRecord() const = 0;
    virtual const std::vector<SarTargetPositionMessage>& targetPositionMessages() const = 0;
    virtual bool readRecord(SarTraceRecord& record) = 0;
};

}  // namespace sar
