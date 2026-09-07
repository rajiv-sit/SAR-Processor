#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace backproj {

struct BackProjOperatorConfig {
    bool outputDebugRpf = false;
    // Explicit demonstration mode; never substitutes for an invalid named input.
    bool allowSyntheticInput = false;
    std::string rpfBaseFileName;
    std::string inputFilePath;
    std::string inputFileName;
    std::uint32_t inputNumFiles = 0;
    std::string replicaSelection;
    std::string pulseReplicaFileName;
    std::uint32_t firstRangeLine = 1;
    std::uint32_t numLinesToProcess = 0;
    std::string outputProjection;
    std::uint32_t nPixX = 0;
    std::uint32_t nPixY = 0;
    std::string imageOffsetSpec;
    std::vector<double> imageOffset;
    std::uint32_t collapseFactor = 1;
    double apOverlapFrac = 0.0;
    std::uint32_t maxNumFrames = 0;
    bool applyAutoFocus = true;
    bool applyFrameRegistration = true;
    std::string azmResSelection;
    double azimuthCollapseFactor = 1.0;
    double azimuthRes = 0.0;
    std::string pixSpacingSelection;
    double pixelSpacing = 0.0;
    std::string outputTiffFrames;
    std::string tileSelection;
    std::uint32_t numTilesY = 1;
    std::uint32_t numTilesX = 1;
    bool useGpu = false;
    bool fastMode = false;
};

}  // namespace backproj
