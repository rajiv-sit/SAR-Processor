#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sar::visualizer {

struct GeolocationGrid {
    std::vector<double> rows;
    std::vector<double> cols;
    std::vector<double> longitude;
    std::vector<double> latitude;
    double interpolationErrorM = 0;
};

struct ScientificFrame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> pixels;  // Row-major, upper-left origin. NaN means nodata.
    float minValue = 0;
    float maxValue = 0;
    std::string product;
    std::string units;
    std::string source;
    std::string displayScale = "amplitude_db";
    std::optional<GeolocationGrid> geolocation;
};

struct GeographicCoordinate {
    double longitude = 0;
    double latitude = 0;
    double interpolationErrorM = 0;
};

// Load the versioned JSON manifest and its sibling little-endian float payload.
// Throws std::runtime_error for invalid metadata, malformed pixels or I/O errors.
ScientificFrame loadScientificFrame(const std::filesystem::path& manifestPath,
                                    std::uint64_t pixelBudget = 64'000'000);

// Bilinear interpolation at source pixel centers; nullopt outside the image or
// when a scene-local frame has no Earth coordinates. Longitudes wrap at +/-180.
std::optional<GeographicCoordinate> geolocate(const ScientificFrame& frame, double row, double col);

// 20*log10(magnitude/peak), clipped to [-dynamicRangeDb, 0].
// Nodata and zero magnitude display as black; source floats remain unchanged.
std::vector<std::uint8_t> magnitudeToGray(const ScientificFrame& frame, float dynamicRangeDb);

// Magnitudes use the amplitude-dB mapping above. Phase uses a fixed linear
// [-pi, pi] -> [0, 255] mapping; dynamicRangeDb is irrelevant for phase.
std::vector<std::uint8_t> displayToGray(const ScientificFrame& frame, float dynamicRangeDb);

}  // namespace sar::visualizer
