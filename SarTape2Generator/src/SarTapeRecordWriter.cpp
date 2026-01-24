#include "sartape2/SarTapeRecordWriter.hpp"

#include <algorithm>
#include <cstring>

#include "sar/SarTapeConstants.hpp"

namespace sartape2 {

namespace {

void writeU16Be(std::vector<std::uint8_t>& buffer, std::size_t offset, std::uint16_t value) {
    buffer[offset] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 1] = static_cast<std::uint8_t>(value & 0xFF);
}

void writeU32Be(std::vector<std::uint8_t>& buffer, std::size_t offset, std::uint32_t value) {
    buffer[offset] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    buffer[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    buffer[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    buffer[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

}  // namespace

bool SarTapeRecordWriter::open(const std::string& path) {
    output_.open(path, std::ios::binary);
    return static_cast<bool>(output_);
}

void SarTapeRecordWriter::close() {
    if (output_.is_open()) {
        output_.close();
    }
}

bool SarTapeRecordWriter::isOpen() const {
    return output_.is_open();
}

bool SarTapeRecordWriter::writeRecordHeader(std::uint16_t recordType,
                                            std::uint16_t sceneNumber,
                                            std::uint16_t recordNumber,
                                            std::uint32_t timeStamp,
                                            std::vector<std::uint8_t>& record) {
    if (record.size() < sar::SarTapeConstants::kRecordSize) {
        return false;
    }
    writeU32Be(record, 0, sar::SarTapeConstants::kSyncWord);
    const std::uint16_t packed = static_cast<std::uint16_t>((recordType << 12) |
                                                            (sceneNumber & 0x0FFF));
    std::size_t offset = 4;
    for (int i = 0; i < 3; ++i) {
        writeU16Be(record, offset, packed);
        writeU16Be(record, offset + 2, recordNumber);
        writeU32Be(record, offset + 4, timeStamp);
        offset += 8;
    }
    return true;
}

bool SarTapeRecordWriter::writeSceneHeader(std::uint16_t sceneNumber, std::uint32_t timeStamp) {
    if (!output_) {
        return false;
    }
    std::vector<std::uint8_t> record(sar::SarTapeConstants::kRecordSize, 0);
    if (!writeRecordHeader(sar::SarTapeConstants::kRecordTypeSceneHeader,
                           sceneNumber,
                           0,
                           timeStamp,
                           record)) {
        return false;
    }

    const std::size_t offset = sar::SarTapeConstants::kRecordHeaderSize;
    writeU16Be(record, offset, static_cast<std::uint16_t>(sar::SarTapeConstants::kRecordSize));
    writeU16Be(record, offset + 2, 0);
    writeU16Be(record, offset + 4, sar::SarTapeConstants::kMaxSceneHeaderSize);

    output_.write(reinterpret_cast<const char*>(record.data()),
                  static_cast<std::streamsize>(record.size()));
    return static_cast<bool>(output_);
}

bool SarTapeRecordWriter::writeDataRecord(std::uint16_t sceneNumber,
                                          std::uint16_t recordNumber,
                                          std::uint32_t timeStamp,
                                          const std::vector<std::int16_t>& iqInterleaved) {
    if (!output_) {
        return false;
    }
    std::vector<std::uint8_t> record(sar::SarTapeConstants::kRecordSize, 0);
    if (!writeRecordHeader(sar::SarTapeConstants::kRecordTypeData,
                           sceneNumber,
                           recordNumber,
                           timeStamp,
                           record)) {
        return false;
    }

    const std::size_t firstGood = 1 + sar::SarTapeConstants::kRecordHeaderSize;
    const std::size_t lastGood = sar::SarTapeConstants::kRecordSize - sar::SarTapeConstants::kTestRampSize;
    const std::size_t capacity = (lastGood >= firstGood) ? (lastGood - firstGood + 1) : 0;
    const std::size_t copyBytes = std::min(capacity, iqInterleaved.size() * sizeof(std::int16_t));
    if (copyBytes > 0) {
        std::memcpy(record.data() + (firstGood - 1),
                    iqInterleaved.data(),
                    copyBytes);
    }

    output_.write(reinterpret_cast<const char*>(record.data()),
                  static_cast<std::streamsize>(record.size()));
    return static_cast<bool>(output_);
}

}  // namespace sartape2
