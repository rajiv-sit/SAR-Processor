#pragma once

#include <cstdint>
#include <string>

namespace backproj {

struct FilterParams {
    double windowCoef = 0.0;
    double windowBroadening = 0.0;
    std::string scalingMethod;
};

struct QuadTreeParams {
    std::uint32_t minSubImageSize = 32;
    double azOverSampFact = 1.0;
    std::uint32_t nExtraRngSamps = 0;
    std::uint32_t nRngTaper = 0;
    std::uint32_t nAzmTaper = 0;
    std::uint32_t nPadAzFftEachEnd = 0;
};

struct FrameRegistrationParams {
    double alphaAccum = 0.0;
    bool preShiftImageGrid = false;
    std::uint32_t regisSearchSize = 0;
    std::uint32_t marginBlanking = 0;
    std::uint32_t chipSize = 0;
    std::uint32_t magFactor = 0;
    double accumPow = 0.0;
    double detPow = 0.0;
};

struct BackProjSecondaryConfig {
    std::string algorithmSelection;
    FilterParams rngFilterParams;
    FilterParams azmFilterParams;
    QuadTreeParams quadParams;
    FrameRegistrationParams frameRegistrationParams;
    std::string rgCompMode;
    std::uint32_t blockSizeInBytes = 0;
    std::uint32_t nExtraRngSamps = 0;
    std::uint32_t tileOverlap = 0;
    std::uint32_t edgeTaperPixels = 0;
    bool applyIqCalCorrection = false;
    bool applyAgcCorrection = false;
    bool applyStcCorrection = false;
};

}  // namespace backproj
