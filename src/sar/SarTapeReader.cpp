#include "sar/SarTapeReader.hpp"

#include <array>
#include <cmath>
#include <vector>

#include "sar/SarTapeConstants.hpp"

namespace sar {

namespace {

bool setError(std::string& error, const char* message) {
    error = message;
    return false;
}

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
    if (!readU16Be(input, byteCount)) {
        return setError(error, "failed to read scene header byteCount");
    }
    if (byteCount > SarTapeConstants::kMaxSceneHeaderSize) {
        byteCount = SarTapeConstants::kMaxSceneHeaderSize;
    }
    sceneHeader.byteCount = byteCount;

    if (!readU16Be(input, sceneHeader.sceneNumber) ||
        !readU32Be(input, sceneHeader.timeStampStartCollect)) {
        return setError(error, "failed to read scene header preamble");
    }

    std::uint32_t coarseDelay = 0;
    std::uint8_t fineDelay = 0;
    if (!readU24Be(input, coarseDelay) || !readBytes(input, &fineDelay, 1)) {
        return setError(error, "failed to read initial range delay");
    }
    const double delayConvFactor = 20.0 / 256.0;
    sceneHeader.initRangeDelay = (256.0 * coarseDelay + fineDelay) * delayConvFactor;

    std::int16_t rangeDelayIncr = 0;
    if (!readI16Be(input, rangeDelayIncr)) {
        return setError(error, "failed to read range delay increment");
    }
    sceneHeader.rangeDelayIncr = rangeDelayIncr * delayConvFactor;
    sceneHeader.numPulsesSpot = static_cast<std::uint16_t>(readStrangeShort(input, false, true));
    sceneHeader.endCountStrip = readStrangeShort(input, true, true);

    if (!readU16Be(input, sceneHeader.sarTapeVolNum)) {
        return setError(error, "failed to read sarTapeVolNum");
    }

    std::array<char, 4> mission{};
    if (!readBytes(input, reinterpret_cast<std::uint8_t*>(mission.data()), mission.size())) {
        return setError(error, "failed to read missionID");
    }
    sceneHeader.missionId.assign(mission.data(), mission.size());

    std::uint8_t samplingFreqCode = 0;
    if (!readBytes(input, &samplingFreqCode, 1)) {
        return setError(error, "failed to read samplingFreq");
    }
    if (samplingFreqCode == 255) {
        sceneHeader.samplingFreq = 500.0;
    } else if (samplingFreqCode == 31) {
        sceneHeader.samplingFreq = 31.25;
    } else {
        sceneHeader.samplingFreq = samplingFreqCode;
    }

    if (!readBytes(input, &sceneHeader.sarMode, 1) ||
        !readU16Be(input, sceneHeader.unUsed1) ||
        !readU32Be(input, sceneHeader.timeStampT0) ||
        !readU16Be(input, sceneHeader.pri) ||
        !readBytes(input, &sceneHeader.targSelectMethod, 1) ||
        !readBytes(input, &sceneHeader.recvrGain, 1) ||
        !readPerkinElmerFloat(input, sceneHeader.headingT0) ||
        !readPerkinElmerFloat(input, sceneHeader.velocityT0) ||
        !readPerkinElmerFloat(input, sceneHeader.trackAngleT0) ||
        !readU32Be(input, sceneHeader.altitudeT0) ||
        !readPerkinElmerFloat(input, sceneHeader.targLatitudeT0) ||
        !readPerkinElmerFloat(input, sceneHeader.targLongitudeT0) ||
        !readU32Be(input, sceneHeader.targRangeT0) ||
        !readPerkinElmerFloat(input, sceneHeader.targAzimuthT0) ||
        !readPerkinElmerFloat(input, sceneHeader.targDepAngleT0) ||
        !readU32Be(input, sceneHeader.targRangeSceCtr) ||
        !readPerkinElmerFloat(input, sceneHeader.targEtaSceCtr) ||
        !readPerkinElmerFloat(input, sceneHeader.targDepAngSceCtr) ||
        !readU16Be(input, sceneHeader.tauAmplitude) ||
        !readU16Be(input, sceneHeader.radarFreq) ||
        !readU16Be(input, sceneHeader.radarPulseWidth)) {
        return setError(error, "failed to read scene header fields");
    }

    std::uint16_t linearFMRate = 0;
    if (!readU16Be(input, linearFMRate)) {
        return setError(error, "failed to read linearFMRate");
    }
    sceneHeader.linearFMRate = linearFMRate;

    if (sceneHeader.linearFMRate == 22.0) {
        sceneHeader.linearFMRate = 22.5;
    }

    if (!readBytes(input, &sceneHeader.priChangeFlag, 1) ||
        !readBytes(input, &sceneHeader.varRngDelayIncrFlag, 1) ||
        !readBytes(input, &sceneHeader.phaseCorrFlag, 1) ||
        !readBytes(input, &sceneHeader.rngCurvDisabledFlag, 1)) {
        return setError(error, "failed to read flag bytes");
    }

    std::array<char, 4> ctrlVer{};
    std::array<char, 4> navVer{};
    if (!readBytes(input, reinterpret_cast<std::uint8_t*>(ctrlVer.data()), ctrlVer.size()) ||
        !readBytes(input, reinterpret_cast<std::uint8_t*>(navVer.data()), navVer.size()) ||
        !readU32Be(input, sceneHeader.unUsed2) ||
        !readU16Be(input, sceneHeader.endMsgCode)) {
        return setError(error, "failed to read version/footer fields");
    }
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

bool SarTapeReader::readRecord(SarTraceRecord& record) {
    if (!input_.is_open() || input_.eof()) {
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

    std::size_t firstGoodVideoByte = 1 + SarTapeConstants::kRecordHeaderSize;
    const std::size_t lastGoodVideoByte = SarTapeConstants::kRecordSize - SarTapeConstants::kTestRampSize;

    if (header.recordType == SarTapeConstants::kRecordTypeSceneHeader) {
        std::uint16_t recordLength = 0;
        std::uint16_t videoByteCount = 0;
        if (!readU16Be(input_, recordLength) || !readU16Be(input_, videoByteCount)) {
            return false;
        }
        sceneHeader_.recordLength = recordLength;
        sceneHeader_.videoByteCount = videoByteCount;

        std::string error;
        if (!readSceneHeader(input_, sceneHeader_, error)) {
            return false;
        }
        hasSceneHeader_ = true;

        firstGoodVideoByte += 4 + sceneHeader_.byteCount;
    }

    if (firstGoodVideoByte <= lastGoodVideoByte) {
        const std::size_t goodBytes = lastGoodVideoByte - firstGoodVideoByte + 1;
        input_.read(reinterpret_cast<char*>(record.iqBytes.data() + (firstGoodVideoByte - 1)),
                    static_cast<std::streamsize>(goodBytes));
        if (!input_) {
            return false;
        }
    }

    if (header.recordType == SarTapeConstants::kRecordTypeDummy && header.recordNumber == 1) {
        for (std::size_t i = 0; i + 1 < record.iqBytes.size(); i += 2) {
            std::swap(record.iqBytes[i], record.iqBytes[i + 1]);
        }
    }

    if (SarTapeConstants::kTestRampSize > 0) {
        input_.seekg(static_cast<std::streamoff>(SarTapeConstants::kTestRampSize), std::ios::cur);
        if (!input_) {
            return false;
        }
    }

    return true;
}

}  // namespace sar
