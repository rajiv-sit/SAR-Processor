#include "sar/SarTapeReader.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "sar/SarTapeConstants.hpp"

namespace sar {

namespace {
bool readBytes(std::ifstream& input, std::uint8_t* buffer, std::size_t size) {
    input.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return static_cast<bool>(input);
}

bool readU16Be(std::ifstream& input, std::uint16_t& value) {
    std::array<std::uint8_t, 2> buf{};
    if (!readBytes(input, buf.data(), buf.size())) return false;
    value = static_cast<std::uint16_t>((buf[0] << 8) | buf[1]);
    return true;
}

bool readI16Be(std::ifstream& input, std::int16_t& value) {
    std::uint16_t temp = 0;
    if (!readU16Be(input, temp)) return false;
    value = static_cast<std::int16_t>(temp);
    return true;
}

bool readU24Be(std::ifstream& input, std::uint32_t& value) {
    std::array<std::uint8_t, 3> buf{};
    if (!readBytes(input, buf.data(), buf.size())) return false;
    value = (static_cast<std::uint32_t>(buf[0]) << 16) |
            (static_cast<std::uint32_t>(buf[1]) << 8) |
            static_cast<std::uint32_t>(buf[2]);
    return true;
}

bool readU32Be(std::ifstream& input, std::uint32_t& value) {
    std::array<std::uint8_t, 4> buf{};
    if (!readBytes(input, buf.data(), buf.size())) return false;
    value = (static_cast<std::uint32_t>(buf[0]) << 24) |
            (static_cast<std::uint32_t>(buf[1]) << 16) |
            (static_cast<std::uint32_t>(buf[2]) << 8) |
            static_cast<std::uint32_t>(buf[3]);
    return true;
}

std::string sliceToString(const std::vector<std::uint8_t>& buffer,
                           std::size_t offset,
                           std::size_t length) {
    if (offset + length > buffer.size()) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(buffer.data() + offset), length);
}

bool parseAccessoryRecord(const std::vector<std::uint8_t>& buffer,
                          std::vector<SarTargetPositionMessage>& messages) {
    const std::size_t start =
        std::min<std::size_t>(SarTapeConstants::kTargetPosDataStart, buffer.size());
    const std::size_t end =
        std::min<std::size_t>(SarTapeConstants::kTargetPosDataEnd, buffer.size());
    if (start >= end || end - start < SarTapeConstants::kTargetPosMsgSize) {
        return false;
    }

    for (std::size_t pos = start; pos + SarTapeConstants::kTargetPosMsgSize <= end; ++pos) {
        if (buffer[pos] != static_cast<std::uint8_t>('T') ||
            buffer[pos + 1] != static_cast<std::uint8_t>('R')) {
            continue;
        }

        const std::size_t rangeOffset = pos + 2;
        const std::size_t timeOffset = rangeOffset + 6;
        const std::size_t lonOffset = timeOffset + 8;
        const std::size_t latOffset = lonOffset + 9;
        const std::size_t endOffset = latOffset + 8;
        if (endOffset + 2 > buffer.size()) {
            break;
        }

        const std::uint8_t end1 = buffer[endOffset];
        const std::uint8_t end2 = buffer[endOffset + 1];
        if (!((end1 == 10 && end2 == 13) || (end1 == 13 && end2 == 10))) {
            continue;
        }

        SarTargetPositionMessage msg{};
        const std::string rangeStr = sliceToString(buffer, rangeOffset, 6);
        const std::string timeStr = sliceToString(buffer, timeOffset, 8);
        msg.targetLong = sliceToString(buffer, lonOffset, 9);
        msg.targetLat = sliceToString(buffer, latOffset, 8);

        if (!rangeStr.empty()) {
            msg.targetRange = std::strtod(rangeStr.c_str(), nullptr);
        }
        if (!timeStr.empty()) {
            msg.timeStamp = static_cast<std::uint32_t>(std::strtoul(timeStr.c_str(), nullptr, 16));
        }

        messages.push_back(std::move(msg));
        pos += SarTapeConstants::kTargetPosMsgSize - 1;
    }

    return !messages.empty();
}

std::int16_t readStrangeShort(std::ifstream& input, bool byte1Signed, bool byte2Signed) {
    std::uint8_t raw1 = 0;
    std::uint8_t raw2 = 0;
    if (!readBytes(input, &raw1, 1) || !readBytes(input, &raw2, 1)) return 0;
    std::int16_t byte1 = byte1Signed ? static_cast<std::int8_t>(raw1)
                                     : static_cast<std::int16_t>(raw1);
    std::int16_t byte2 = byte2Signed ? static_cast<std::int8_t>(raw2)
                                     : static_cast<std::int16_t>(raw2);
    return static_cast<std::int16_t>(256 * byte1 + byte2);
}

double perkinElmerToFloat(std::uint32_t value) {
    const std::uint32_t signBitMask = 0x80000000;
    const std::uint32_t exponentMask = 0x7F000000;
    const std::uint32_t mantissaMask = 0x00FFFFFF;

    const bool signBit = (value & signBitMask) != 0;
    const double sign = signBit ? -1.0 : 1.0;
    double mantissa = static_cast<double>(value & mantissaMask) / std::pow(2.0, 24.0);
    double exponent = static_cast<double>((value & exponentMask) >> 24) - 64.0;
    return sign * mantissa * std::pow(16.0, exponent);
}

bool readPerkinElmerFloat(std::ifstream& input, double& value) {
    std::uint32_t raw = 0;
    if (!readU32Be(input, raw)) return false;
    value = perkinElmerToFloat(raw);
    return true;
}

bool readSceneHeader(std::ifstream& input, SarSceneHeader& sceneHeader, std::string& error) {
    std::uint16_t byteCount = 0;
    if (!readU16Be(input, byteCount)) return (error = "failed to read scene header byteCount", false);
    if (byteCount > SarTapeConstants::kMaxSceneHeaderSize) {
        byteCount = SarTapeConstants::kMaxSceneHeaderSize;
    }
    sceneHeader.byteCount = byteCount;

    if (!readU16Be(input, sceneHeader.sceneNumber)) return (error = "failed to read scene header preamble", false);
    if (!readU32Be(input, sceneHeader.timeStampStartCollect)) return (error = "failed to read scene header preamble", false);

    std::uint32_t coarseDelay = 0;
    std::uint8_t fineDelay = 0;
    if (!readU24Be(input, coarseDelay) || !readBytes(input, &fineDelay, 1)) return (error = "failed to read initial range delay", false);
    const double delayConvFactor = 20.0 / 256.0;
    sceneHeader.initRangeDelay = (256.0 * coarseDelay + fineDelay) * delayConvFactor;

    std::int16_t rangeDelayIncr = 0;
    if (!readI16Be(input, rangeDelayIncr)) return (error = "failed to read range delay increment", false);
    sceneHeader.rangeDelayIncr = rangeDelayIncr * delayConvFactor;
    sceneHeader.numPulsesSpot = static_cast<std::uint16_t>(readStrangeShort(input, false, true));
    sceneHeader.endCountStrip = readStrangeShort(input, true, true);

    if (!readU16Be(input, sceneHeader.sarTapeVolNum)) return (error = "failed to read sarTapeVolNum", false);

    std::array<char, 4> mission{};
    if (!readBytes(input, reinterpret_cast<std::uint8_t*>(mission.data()), mission.size())) return (error = "failed to read missionID", false);
    sceneHeader.missionId.assign(mission.data(), mission.size());

    std::uint8_t samplingFreqCode = 0;
    if (!readBytes(input, &samplingFreqCode, 1)) return (error = "failed to read samplingFreq", false);
    if (samplingFreqCode == 255) {
        sceneHeader.samplingFreq = 500.0;
    } else if (samplingFreqCode == 31) {
        sceneHeader.samplingFreq = 31.25;
    } else {
        sceneHeader.samplingFreq = samplingFreqCode;
    }

    if (!readBytes(input, &sceneHeader.sarMode, 1)) return (error = "failed to read scene header fields", false);
    if (!readU16Be(input, sceneHeader.unUsed1)) return (error = "failed to read scene header fields", false);
    if (!readU32Be(input, sceneHeader.timeStampT0)) return (error = "failed to read scene header fields", false);
    if (!readU16Be(input, sceneHeader.pri)) return (error = "failed to read scene header fields", false);
    if (!readBytes(input, &sceneHeader.targSelectMethod, 1)) return (error = "failed to read scene header fields", false);
    if (!readBytes(input, &sceneHeader.recvrGain, 1)) return (error = "failed to read scene header fields", false);
    if (!readPerkinElmerFloat(input, sceneHeader.headingT0)) return (error = "failed to read scene header fields", false);
    if (!readPerkinElmerFloat(input, sceneHeader.velocityT0)) return (error = "failed to read scene header fields", false);
    if (!readPerkinElmerFloat(input, sceneHeader.trackAngleT0)) return (error = "failed to read scene header fields", false);
    if (!readU32Be(input, sceneHeader.altitudeT0)) return (error = "failed to read scene header fields", false);
    if (!readPerkinElmerFloat(input, sceneHeader.targLatitudeT0)) return (error = "failed to read scene header fields", false);
    if (!readPerkinElmerFloat(input, sceneHeader.targLongitudeT0)) return (error = "failed to read scene header fields", false);
    if (!readU32Be(input, sceneHeader.targRangeT0)) return (error = "failed to read scene header fields", false);
    if (!readPerkinElmerFloat(input, sceneHeader.targAzimuthT0)) return (error = "failed to read scene header fields", false);
    if (!readPerkinElmerFloat(input, sceneHeader.targDepAngleT0)) return (error = "failed to read scene header fields", false);
    if (!readU32Be(input, sceneHeader.targRangeSceCtr)) return (error = "failed to read scene header fields", false);
    if (!readPerkinElmerFloat(input, sceneHeader.targEtaSceCtr)) return (error = "failed to read scene header fields", false);
    if (!readPerkinElmerFloat(input, sceneHeader.targDepAngSceCtr)) return (error = "failed to read scene header fields", false);
    if (!readU16Be(input, sceneHeader.tauAmplitude)) return (error = "failed to read scene header fields", false);
    if (!readU16Be(input, sceneHeader.radarFreq)) return (error = "failed to read scene header fields", false);
    if (!readU16Be(input, sceneHeader.radarPulseWidth)) return (error = "failed to read scene header fields", false);

    std::uint16_t linearFMRate = 0;
    if (!readU16Be(input, linearFMRate)) return (error = "failed to read linearFMRate", false);
    sceneHeader.linearFMRate = linearFMRate;

    if (sceneHeader.linearFMRate == 22.0) {
        sceneHeader.linearFMRate = 22.5;
    }

    if (!readBytes(input, &sceneHeader.priChangeFlag, 1)) return (error = "failed to read flag bytes", false);
    if (!readBytes(input, &sceneHeader.varRngDelayIncrFlag, 1)) return (error = "failed to read flag bytes", false);
    if (!readBytes(input, &sceneHeader.phaseCorrFlag, 1)) return (error = "failed to read flag bytes", false);
    if (!readBytes(input, &sceneHeader.rngCurvDisabledFlag, 1)) return (error = "failed to read flag bytes", false);

    std::array<char, 4> ctrlVer{};
    std::array<char, 4> navVer{};
    if (!readBytes(input, reinterpret_cast<std::uint8_t*>(ctrlVer.data()), ctrlVer.size())) return (error = "failed to read version/footer fields", false);
    if (!readBytes(input, reinterpret_cast<std::uint8_t*>(navVer.data()), navVer.size())) return (error = "failed to read version/footer fields", false);
    if (!readU32Be(input, sceneHeader.unUsed2)) return (error = "failed to read version/footer fields", false);
    if (!readU16Be(input, sceneHeader.endMsgCode)) return (error = "failed to read version/footer fields", false);
    sceneHeader.ctrlCompSwVersion.assign(ctrlVer.data(), ctrlVer.size());
    sceneHeader.navCompSwVersion.assign(navVer.data(), navVer.size());

    return true;
}

}  // namespace

SarTapeReader::SarTapeReader(const std::string& path)
    : input_(path, std::ios::binary) {}

SarTapeReader::~SarTapeReader() = default;

bool SarTapeReader::isOpen() const {
    return input_.is_open();
}

bool SarTapeReader::hasSceneHeader() const {
    return hasSceneHeader_;
}

const SarSceneHeader& SarTapeReader::sceneHeader() const {
    return sceneHeader_;
}

bool SarTapeReader::hasAccessoryRecord() const {
    return hasAccessoryRecord_;
}

const std::vector<SarTargetPositionMessage>& SarTapeReader::targetPositionMessages() const {
    return targetPositionMessages_;
}

bool SarTapeReader::ensureAccessoryParsed() {
    if (accessoryParsed_) {
        return true;
    }
    accessoryParsed_ = true;
    if (!input_.is_open()) {
        return false;
    }

    input_.clear();
    input_.seekg(0, std::ios::beg);
    std::uint32_t firstWord = 0;
    if (!readU32Be(input_, firstWord)) {
        return false;
    }

    if (firstWord == SarTapeConstants::kSyncWord) {
        input_.clear();
        input_.seekg(0, std::ios::beg);
        return true;
    }

    input_.clear();
    input_.seekg(0, std::ios::end);
    const std::streamoff fileSize = input_.tellg();
    if (fileSize < static_cast<std::streamoff>(SarTapeConstants::kAccessoryRecordSize + 4)) {
        input_.clear();
        input_.seekg(0, std::ios::beg);
        return true;
    }

    input_.seekg(static_cast<std::streamoff>(SarTapeConstants::kAccessoryRecordSize),
                 std::ios::beg);
    std::uint32_t syncAtOffset = 0;
    if (!readU32Be(input_, syncAtOffset) || syncAtOffset != SarTapeConstants::kSyncWord) {
        input_.clear();
        input_.seekg(0, std::ios::beg);
        return true;
    }

    input_.clear();
    input_.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buffer(SarTapeConstants::kAccessoryRecordSize);
    input_.read(reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(buffer.size()));
    const std::size_t bytesRead = static_cast<std::size_t>(input_.gcount());
    buffer.resize(bytesRead);
    hasAccessoryRecord_ = bytesRead >= SarTapeConstants::kTargetPosDataStart;
    if (hasAccessoryRecord_) {
        parseAccessoryRecord(buffer, targetPositionMessages_);
    }

    input_.clear();
    const std::streamoff targetOffset =
        static_cast<std::streamoff>(std::min<std::size_t>(bytesRead,
                                                          SarTapeConstants::kAccessoryRecordSize));
    input_.seekg(targetOffset, std::ios::beg);
    return true;
}

bool SarTapeReader::readRecord(SarTraceRecord& record) {
    if (!input_.is_open() || input_.eof()) {
        return false;
    }
    if (!ensureAccessoryParsed()) {
        return false;
    }

    const std::streamoff recordStart = static_cast<std::streamoff>(input_.tellg());
    if (recordStart < 0) {
        return false;
    }

    SarTraceHeader header{};
    if (!readU32Be(input_, header.syncWord)) {
        return false;
    }

    std::uint16_t firstTypeScene = 0;
    std::uint16_t firstRecordNumber = 0;
    std::uint32_t firstTimeStamp = 0;

    for (int i = 0; i < 3; ++i) {
        std::uint16_t packed = 0;
        std::uint16_t recordNumber = 0;
        std::uint32_t timeStamp = 0;
        if (!readU16Be(input_, packed) || !readU16Be(input_, recordNumber) || !readU32Be(input_, timeStamp)) {
            return false;
        }
        if (i == 0) {
            firstTypeScene = packed;
            firstRecordNumber = recordNumber;
            firstTimeStamp = timeStamp;
        }
    }

    header.recordType = static_cast<std::uint16_t>((firstTypeScene >> 12) & 0xF);
    header.sceneNumber = static_cast<std::uint16_t>(firstTypeScene & 0x0FFF);
    header.recordNumber = firstRecordNumber;
    header.timeStamp = firstTimeStamp;
    header.syncValid = (header.syncWord == SarTapeConstants::kSyncWord);

    record.header = header;
    record.iqBytes.assign(SarTapeConstants::kRecordSize, 0);
    const std::size_t lastGoodVideoByte = SarTapeConstants::kRecordSize - SarTapeConstants::kTestRampSize;

    if (header.recordType == SarTapeConstants::kRecordTypeSceneHeader) {
        std::uint16_t recordLength = 0;
        std::uint16_t videoByteCount = 0;
        if (!readU16Be(input_, recordLength) || !readU16Be(input_, videoByteCount)) return false;
        sceneHeader_.recordLength = recordLength;
        sceneHeader_.videoByteCount = videoByteCount;

        std::string error;
        if (!readSceneHeader(input_, sceneHeader_, error)) return false;
        hasSceneHeader_ = true;

    }

    std::size_t firstGoodVideoByte = 1 + SarTapeConstants::kRecordHeaderSize;
    const std::streamoff currentPos = static_cast<std::streamoff>(input_.tellg());
    if (currentPos >= recordStart) {
        const std::size_t offset = static_cast<std::size_t>(currentPos - recordStart);
        const std::size_t positionByte = offset + 1;
        if (positionByte > firstGoodVideoByte) {
            firstGoodVideoByte = positionByte;
        }
    }

    if (firstGoodVideoByte <= lastGoodVideoByte) {
        const std::size_t goodBytes = lastGoodVideoByte - firstGoodVideoByte + 1;
        input_.read(reinterpret_cast<char*>(record.iqBytes.data() + (firstGoodVideoByte - 1)),
                    static_cast<std::streamsize>(goodBytes));
        if (!input_) return false;
    }

    if (header.recordType == SarTapeConstants::kRecordTypeDummy && header.recordNumber == 1) {
        for (std::size_t i = 0; i + 1 < record.iqBytes.size(); i += 2) {
            std::swap(record.iqBytes[i], record.iqBytes[i + 1]);
        }
    }

    if (SarTapeConstants::kTestRampSize > 0) {
        input_.seekg(static_cast<std::streamoff>(SarTapeConstants::kTestRampSize), std::ios::cur);
        if (!input_) return false;
    }

    return true;
}

}  // namespace sar
