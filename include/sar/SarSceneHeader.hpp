#pragma once

#include <cstdint>
#include <string>

namespace sar {

struct SarSceneHeader {
    std::uint16_t recordLength = 0;
    std::uint16_t videoByteCount = 0;
    std::uint16_t byteCount = 0;
    std::uint16_t sceneNumber = 0;
    std::uint32_t timeStampStartCollect = 0;
    double initRangeDelay = 0.0;
    double rangeDelayIncr = 0.0;
    std::uint16_t numPulsesSpot = 0;
    std::int16_t endCountStrip = 0;
    std::uint16_t sarTapeVolNum = 0;
    std::string missionId;
    double samplingFreq = 0.0;
    std::uint8_t sarMode = 0;
    std::uint16_t unUsed1 = 0;
    std::uint32_t timeStampT0 = 0;
    std::uint16_t pri = 0;
    std::uint8_t targSelectMethod = 0;
    std::uint8_t recvrGain = 0;
    double headingT0 = 0.0;
    double velocityT0 = 0.0;
    double trackAngleT0 = 0.0;
    std::uint32_t altitudeT0 = 0;
    double targLatitudeT0 = 0.0;
    double targLongitudeT0 = 0.0;
    std::uint32_t targRangeT0 = 0;
    double targAzimuthT0 = 0.0;
    double targDepAngleT0 = 0.0;
    std::uint32_t targRangeSceCtr = 0;
    double targEtaSceCtr = 0.0;
    double targDepAngSceCtr = 0.0;
    std::uint16_t tauAmplitude = 0;
    std::uint16_t radarFreq = 0;
    std::uint16_t radarPulseWidth = 0;
    double linearFMRate = 0.0;
    std::uint8_t priChangeFlag = 0;
    std::uint8_t varRngDelayIncrFlag = 0;
    std::uint8_t phaseCorrFlag = 0;
    std::uint8_t rngCurvDisabledFlag = 0;
    std::string ctrlCompSwVersion;
    std::string navCompSwVersion;
    std::uint32_t unUsed2 = 0;
    std::uint16_t endMsgCode = 0;
};

}  // namespace sar
