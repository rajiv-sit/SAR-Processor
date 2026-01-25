#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rpf {

struct ImageDataChunkHeader {
    std::uint32_t dataWidth = 0;
    std::uint32_t dataHeight = 0;
    std::int32_t frameSeqNum = 0;
    std::int32_t pixelType = 0;
    std::int32_t rspInhibit = 0;
    std::uint16_t pixelMarginStart = 0;
    std::uint16_t pixelMarginEnd = 0;
    std::uint16_t lineMarginStart = 0;
    std::uint16_t lineMarginEnd = 0;
};

struct FileIdParams {
    std::uint8_t radarMode = 0;
    std::int32_t fileType = 0;
};

struct UtcDateTime {
    std::uint16_t year = 0;
    std::uint8_t month = 0;
    std::uint8_t day = 0;
    std::uint8_t hour = 0;
    std::uint8_t minute = 0;
    std::uint8_t second = 0;
    std::uint16_t millisec = 0;
};

struct ProcessedImageFileId {
    std::int32_t fileType = 0;
    std::int32_t radarMode = 0;
    std::string formatVersion;
};

struct DataAcquisitionInfo {
    std::string aircraftId;
    std::string sortieNumber;
    UtcDateTime currentMissionStartTime{};
    UtcDateTime rawDataMissionStartTime{};
    std::int32_t currentAcqId = 0;
    std::int32_t rawDataAcqId = 0;
    UtcDateTime currentAcqStartTime{};
    UtcDateTime rawDataAcqStartTime{};
    std::uint32_t resolution = 0;
    std::uint32_t polarization = 0;
    std::string hddrFileName;
};

struct SeaspotTarget {
    std::uint32_t tgtSelect = 0;
    std::int32_t trackId = 0;
    std::int32_t useCounter = 0;
    std::uint32_t tgtVelocity = 0;
    double tgtLatitude = 0.0;
    double tgtLongitude = 0.0;
    float tgtSpeed = 0.0f;
    float tgtCourse = 0.0f;
    float tgtElevation = 0.0f;
};

struct LandspotTarget {
    std::uint32_t tgtSelect = 0;
    std::uint32_t trackId = 0;
    std::uint32_t useCounter = 0;
    float tgtElevation = 0.0f;
    double tgtLatitude = 0.0;
    double tgtLongitude = 0.0;
};

struct StripmapTarget {
    std::uint32_t tgtSelect = 0;
    float tgtElevation = 0.0f;
    double tgtLatitude = 0.0;
    double tgtLongitude = 0.0;
    double tgt2Latitude = 0.0;
    double tgt2Longitude = 0.0;
};

struct OwnAircraftInfo {
    float acHeading = 0.0f;
    float acSpeed = 0.0f;
    double acLatitude = 0.0;
    double acLongitude = 0.0;
    double acAltitude = 0.0;
};

struct RawAnnotationBlock {
    std::vector<std::uint8_t> bytes;
};

struct LatLongOutput {
    std::uint16_t geolocationGridNumLines = 0;
};

struct ImageRect {
    std::uint32_t startLine = 1;
    std::uint32_t startPixel = 1;
    std::uint32_t numLines = 0;
    std::uint32_t numPixels = 0;
};

struct AcquisitionMetadata {
    std::int32_t frameSeqNum = 0;
    std::int32_t pixelType = 0;
    std::int32_t rspInhibit = 0;
    std::uint16_t pixelMarginStart = 0;
    std::uint16_t pixelMarginEnd = 0;
    std::uint16_t lineMarginStart = 0;
    std::uint16_t lineMarginEnd = 0;
};

struct AnnotationNotes {
    std::string summary;
};

struct AnnotationStruct {
    FileIdParams fileIdParams{};
    ProcessedImageFileId processedImageFileId{};
    LatLongOutput latLongOutput{};
    ImageDataChunkHeader imageDataChunkHeader{};
    ImageRect imageRect{};
    AcquisitionMetadata acquisition{};
    DataAcquisitionInfo dataAcquisition{};
    SeaspotTarget seaspotTarget{};
    LandspotTarget landspotTarget{};
    StripmapTarget stripmapTarget{};
    OwnAircraftInfo ownAircraftInfo{};
    RawAnnotationBlock imgDisplayParams{};
    RawAnnotationBlock procInParams{};
    RawAnnotationBlock dataProcOutput{};
    RawAnnotationBlock procIdParams{};
    AnnotationNotes notes{};
    std::string fileName;
};

}  // namespace rpf
