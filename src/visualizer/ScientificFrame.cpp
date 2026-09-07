#include "visualizer/ScientificFrame.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>

namespace sar::visualizer {
namespace {

constexpr std::uint64_t kMaxPixels = 64'000'000;
constexpr std::uintmax_t kMaxManifestBytes = 2'000'000;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint32_t dimension(const nlohmann::json& value) {
    require(value.is_number_unsigned() || value.is_number_integer(),
            "Image dimensions must be positive integers");
    const auto result = value.get<std::int64_t>();
    require(result > 0 && result <= static_cast<std::int64_t>(kMaxPixels),
            "Image dimension exceeds the supported range");
    return static_cast<std::uint32_t>(result);
}

std::string requiredText(const nlohmann::json& value) {
    const auto result = value.get<std::string>();
    require(!result.empty() && result.size() <= 4096 && result.find('\0') == std::string::npos,
            "Text metadata must contain 1..4096 characters without null bytes");
    return result;
}

std::vector<double> gridAxis(const nlohmann::json& value, std::uint32_t extent) {
    require(value.is_array() && !value.empty() && value.size() <= 65,
            "Geolocation grid axes require 1..65 entries");
    auto axis = value.get<std::vector<double>>();
    require(axis.front() == 0 && axis.back() == static_cast<double>(extent - 1),
            "Geolocation axes must span the image pixel centers");
    for (std::size_t i = 0; i < axis.size(); ++i) {
        require(std::isfinite(axis[i]) && (i == 0 || axis[i] > axis[i - 1]),
                "Geolocation axes must be finite and strictly increasing");
    }
    return axis;
}

std::vector<double> gridValues(const nlohmann::json& value, std::size_t rows, std::size_t cols,
                               double limit) {
    require(value.is_array() && value.size() == rows, "Geolocation grid row count mismatch");
    std::vector<double> result;
    result.reserve(rows * cols);
    for (const auto& row : value) {
        require(row.is_array() && row.size() == cols, "Geolocation grid column count mismatch");
        for (const auto& entry : row) {
            const double v = entry.get<double>();
            require(std::isfinite(v) && std::abs(v) <= limit,
                    "Geolocation coordinates are nonfinite or out of bounds");
            result.push_back(v);
        }
    }
    return result;
}

std::uint32_t littleEndianWord(const unsigned char* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

struct Bracket {
    std::size_t lower;
    std::size_t upper;
    double fraction;
};

Bracket bracket(const std::vector<double>& axis, double value) {
    if (axis.size() == 1) {
        return {0, 0, 0};
    }
    const auto it = std::upper_bound(axis.begin(), axis.end(), value);
    const auto upper = std::min(static_cast<std::size_t>(it - axis.begin()), axis.size() - 1);
    const auto lower = upper - 1;
    return {lower, upper, (value - axis[lower]) / (axis[upper] - axis[lower])};
}

}  // namespace

ScientificFrame loadScientificFrame(const std::filesystem::path& manifestPath,
                                    std::uint64_t pixelBudget) {
    try {
        require(pixelBudget > 0 && pixelBudget <= kMaxPixels,
                "Scientific frame pixel budget must be in 1..64 million");
        require(std::filesystem::is_regular_file(manifestPath),
                "Manifest is missing or not a file");
        require(std::filesystem::file_size(manifestPath) <= kMaxManifestBytes,
                "Scientific manifest exceeds 2 MB");
        std::ifstream manifestStream(manifestPath);
        require(manifestStream.is_open(), "Cannot open scientific manifest");
        const auto metadata = nlohmann::json::parse(manifestStream);
        require(metadata.at("schema") == "sar-scientific-frame-v1",
                "Unsupported scientific frame schema");
        ScientificFrame frame;
        frame.width = dimension(metadata.at("width"));
        frame.height = dimension(metadata.at("height"));
        const auto count = static_cast<std::uint64_t>(frame.width) * frame.height;
        require(count <= kMaxPixels, "Scientific frame exceeds 64 million pixels");
        require(count <= pixelBudget, "Scientific frame exceeds the remaining scan pixel budget");
        frame.product = requiredText(metadata.at("product"));
        const bool phase =
            frame.product == "focused_phase" || frame.product == "backprojected_phase";
        require(phase || frame.product == "geocoded_magnitude" ||
                    frame.product == "scene_local_magnitude" ||
                    frame.product == "range_profile_magnitude" ||
                    frame.product == "focused_magnitude" ||
                    frame.product == "backprojected_magnitude",
                "Unsupported scientific frame product");
        require(frame.product != "geocoded_magnitude" || metadata.contains("geolocation"),
                "Geocoded frames require geolocation");
        require(frame.product != "scene_local_magnitude" || !metadata.contains("geolocation"),
                "Scene-local frames must omit geolocation");
        if (metadata.contains("display_scale")) {
            frame.displayScale = requiredText(metadata.at("display_scale"));
        }
        require(frame.displayScale == (phase ? "phase_radians" : "amplitude_db"),
                "Scientific product and display scale do not agree");
        frame.units = requiredText(metadata.at("units"));
        require(frame.units == (phase ? "radians" : "uncalibrated magnitude DN"),
                "Scientific product and units do not agree");
        frame.source = requiredText(metadata.at("source"));
        const auto payloadName = requiredText(metadata.at("data_file"));
        require(payloadName != "." && payloadName != ".." &&
                    payloadName.find_first_of("/\\:") == std::string::npos,
                "Scientific payload must be a sibling basename without traversal");
        const auto payloadPath = manifestPath.parent_path() / payloadName;
        require(std::filesystem::is_regular_file(payloadPath),
                "Scientific payload is missing or not a file");
        // A sibling symlink must not redirect the data reader outside the manifest directory.
        require(std::filesystem::canonical(payloadPath).parent_path() ==
                    std::filesystem::canonical(manifestPath).parent_path(),
                "Scientific payload resolves outside the manifest directory");
        require(std::filesystem::file_size(payloadPath) == 8 + count * sizeof(float),
                "Scientific payload size does not match the manifest");
        std::ifstream payload(payloadPath, std::ios::binary);
        require(payload.is_open(), "Cannot open scientific payload");
        std::array<unsigned char, 8> header{};
        payload.read(reinterpret_cast<char*>(header.data()), header.size());
        require(littleEndianWord(header.data()) == frame.width &&
                    littleEndianWord(header.data() + 4) == frame.height,
                "Scientific payload dimensions do not match the manifest");
        frame.pixels.resize(static_cast<std::size_t>(count));
        payload.read(reinterpret_cast<char*>(frame.pixels.data()),
                     static_cast<std::streamsize>(count * sizeof(float)));
        require(payload.good(), "Scientific payload changed or was truncated while reading");
        frame.minValue = std::numeric_limits<float>::infinity();
        frame.maxValue = -std::numeric_limits<float>::infinity();
        for (auto& pixel : frame.pixels) {
            if constexpr (std::endian::native == std::endian::big) {
                pixel = std::bit_cast<float>(std::byteswap(std::bit_cast<std::uint32_t>(pixel)));
            }
            require(!std::isinf(pixel), "Scientific pixels must be finite or nodata NaN");
            if (phase) {
                // pi rounded to the payload's float precision is a valid endpoint.
                require(std::isnan(pixel) || std::abs(pixel) <= std::numbers::pi_v<float>,
                        "Scientific phase must be in [-pi, pi] radians or nodata NaN");
            } else {
                require(!(pixel < 0), "Scientific magnitudes must be nonnegative or nodata NaN");
            }
            if (std::isfinite(pixel)) {
                frame.minValue = std::min(frame.minValue, pixel);
                frame.maxValue = std::max(frame.maxValue, pixel);
            }
        }
        require(std::isfinite(frame.minValue), "Scientific frame has no valid pixels");
        if (metadata.contains("geolocation")) {
            const auto& geo = metadata.at("geolocation");
            GeolocationGrid grid;
            grid.rows = gridAxis(geo.at("rows"), frame.height);
            grid.cols = gridAxis(geo.at("cols"), frame.width);
            grid.longitude =
                gridValues(geo.at("longitude"), grid.rows.size(), grid.cols.size(), 180);
            grid.latitude = gridValues(geo.at("latitude"), grid.rows.size(), grid.cols.size(), 90);
            grid.interpolationErrorM = geo.at("interpolation_error_m").get<double>();
            require(std::isfinite(grid.interpolationErrorM) && grid.interpolationErrorM >= 0,
                    "Geolocation interpolation residual must be finite and nonnegative");
            frame.geolocation = std::move(grid);
        }
        return frame;
    } catch (const std::exception& error) {
        throw std::runtime_error(manifestPath.filename().string() + ": " + error.what());
    }
}

std::optional<GeographicCoordinate> geolocate(const ScientificFrame& frame, double row,
                                              double col) {
    if (!frame.geolocation || !std::isfinite(row) || !std::isfinite(col) || row < 0 || col < 0 ||
        row >= frame.height || col >= frame.width || row > frame.height - 1 ||
        col > frame.width - 1) {
        return std::nullopt;
    }
    const auto& grid = *frame.geolocation;
    const auto r = bracket(grid.rows, row);
    const auto c = bracket(grid.cols, col);
    const auto interpolate = [&](const std::vector<double>& values, bool longitude) {
        const auto index = [&](std::size_t y, std::size_t x) { return y * grid.cols.size() + x; };
        const double origin = values[index(r.lower, c.lower)];
        const auto value = [&](std::size_t y, std::size_t x) {
            const double v = values[index(y, x)];
            return longitude ? origin + std::remainder(v - origin, 360) : v;
        };
        return std::lerp(std::lerp(origin, value(r.lower, c.upper), c.fraction),
                         std::lerp(value(r.upper, c.lower), value(r.upper, c.upper), c.fraction),
                         r.fraction);
    };
    return GeographicCoordinate{std::remainder(interpolate(grid.longitude, true), 360),
                                interpolate(grid.latitude, false), grid.interpolationErrorM};
}

std::vector<std::uint8_t> magnitudeToGray(const ScientificFrame& frame, float dynamicRangeDb) {
    require(frame.displayScale == "amplitude_db", "Amplitude tone mapping requires magnitude data");
    require(std::isfinite(dynamicRangeDb) && dynamicRangeDb > 0 && dynamicRangeDb <= 200,
            "Display dynamic range must be in (0, 200] dB");
    std::vector<std::uint8_t> result(frame.pixels.size(), 0);
    if (frame.maxValue <= 0) {
        return result;
    }
    const double peakDb = 20 * std::log10(static_cast<double>(frame.maxValue));
    for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
        const float pixel = frame.pixels[i];
        if (std::isfinite(pixel) && pixel > 0) {
            const double relativeDb = 20 * std::log10(static_cast<double>(pixel)) - peakDb;
            const double value = std::clamp(1 + relativeDb / dynamicRangeDb, 0.0, 1.0);
            result[i] = static_cast<std::uint8_t>(std::lround(value * 255));
        }
    }
    return result;
}

std::vector<std::uint8_t> displayToGray(const ScientificFrame& frame, float dynamicRangeDb) {
    if (frame.displayScale == "amplitude_db") {
        return magnitudeToGray(frame, dynamicRangeDb);
    }
    require(frame.displayScale == "phase_radians", "Unsupported scientific display scale");
    std::vector<std::uint8_t> result(frame.pixels.size(), 0);
    constexpr double pi = std::numbers::pi_v<float>;
    for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
        const float pixel = frame.pixels[i];
        if (std::isnan(pixel)) {
            continue;
        }
        require(std::isfinite(pixel) && std::abs(pixel) <= pi,
                "Phase display requires finite [-pi, pi] radians or nodata NaN");
        result[i] = static_cast<std::uint8_t>(
            std::lround((static_cast<double>(pixel) + pi) * 255 / (2 * pi)));
    }
    return result;
}

}  // namespace sar::visualizer
