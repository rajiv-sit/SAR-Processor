#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "sartape2/Types.hpp"

namespace sartape2 {

class SarTapeRecordWriter {
public:
    bool open(const std::string& path);
    void close();
    bool isOpen() const;

    bool writeSceneHeader(std::uint16_t sceneNumber, std::uint32_t timeStamp);
    bool writeDataRecord(std::uint16_t sceneNumber,
                         std::uint16_t recordNumber,
                         std::uint32_t timeStamp,
                         const std::vector<std::int16_t>& iqInterleaved);

private:
    bool writeRecordHeader(std::uint16_t recordType,
                           std::uint16_t sceneNumber,
                           std::uint16_t recordNumber,
                           std::uint32_t timeStamp,
                           std::vector<std::uint8_t>& record);

    std::ofstream output_;
};

}  // namespace sartape2
