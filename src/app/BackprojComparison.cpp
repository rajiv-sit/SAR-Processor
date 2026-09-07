#include "app/BackprojComparison.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace app {
namespace {

struct FloatMatrix {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> data;
};

std::uint32_t readU32Le(std::ifstream& input) {
    std::array<std::uint8_t, 4> buffer{};
    input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    return static_cast<std::uint32_t>(buffer[0]) | (static_cast<std::uint32_t>(buffer[1]) << 8) |
           (static_cast<std::uint32_t>(buffer[2]) << 16) |
           (static_cast<std::uint32_t>(buffer[3]) << 24);
}

FloatMatrix readMatrix(const std::string& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto bytes = input.tellg();
    if (bytes < 8) {
        throw std::runtime_error("Missing or truncated raw matrix: " + path);
    }
    input.seekg(0);
    FloatMatrix matrix;
    matrix.width = readU32Le(input);
    matrix.height = readU32Le(input);
    const std::uint64_t count = static_cast<std::uint64_t>(matrix.width) * matrix.height;
    const auto payloadBytes = bytes - std::streamoff(8);
    if (count == 0 || count != static_cast<std::uint64_t>(payloadBytes) / sizeof(float) ||
        payloadBytes % sizeof(float) != 0 || count > matrix.data.max_size()) {
        throw std::runtime_error("Invalid raw matrix dimensions or payload size: " + path);
    }
    matrix.data.resize(static_cast<std::size_t>(count));
    for (float& value : matrix.data) {
        value = std::bit_cast<float>(readU32Le(input));
    }
    if (!input) {
        throw std::runtime_error("Failed to read raw matrix: " + path);
    }
    return matrix;
}

void validateMetadata(const std::string& path, const FloatMatrix& matrix) {
    std::ifstream input(path);
    nlohmann::json meta;
    input >> meta;
    if (!meta.is_object()) {
        throw std::runtime_error("Metadata must be an object: " + path);
    }
    if ((meta.contains("width") && meta.at("width") != matrix.width) ||
        (meta.contains("height") && meta.at("height") != matrix.height)) {
        throw std::runtime_error("Metadata dimensions disagree with raw matrix: " + path);
    }
    for (const auto* key : {"minValue", "maxValue"}) {
        if (meta.contains(key) &&
            (!meta.at(key).is_number() || !std::isfinite(meta.at(key).get<double>()))) {
            throw std::runtime_error("Invalid metadata numeric value: " + path);
        }
    }
}

double parseTolerance(const std::string& value) {
    std::size_t consumed = 0;
    const double tolerance = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(tolerance) || tolerance < 0.0) {
        throw std::runtime_error("Tolerances must be finite nonnegative numbers");
    }
    return tolerance;
}

void printUsage(std::ostream& output) {
    output << "Usage: backproj_compare_cli --cpp-raw <path> --cpp-meta <path> "
              "--ref-raw <path> --ref-meta <path> [--atol <number>] [--rtol <number>]\n"
              "Pass requires equal dimensions, finite pixels, and abs(actual-reference) <= "
              "atol + rtol*abs(reference) for every pixel. Defaults: atol=0, rtol=0.\n";
}

}  // namespace

int runBackprojComparison(int argc, const char* const* argv, std::ostream& output,
                          std::ostream& errors) {
    try {
        std::string cppRaw, cppMeta, refRaw, refMeta;
        double atol = 0.0;
        double rtol = 0.0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                printUsage(output);
                return 0;
            }
            if (i + 1 >= argc) {
                throw std::runtime_error("Incomplete argument: " + arg);
            }
            const std::string value = argv[++i];
            if (arg == "--cpp-raw")
                cppRaw = value;
            else if (arg == "--cpp-meta")
                cppMeta = value;
            else if (arg == "--ref-raw")
                refRaw = value;
            else if (arg == "--ref-meta")
                refMeta = value;
            else if (arg == "--atol")
                atol = parseTolerance(value);
            else if (arg == "--rtol")
                rtol = parseTolerance(value);
            else
                throw std::runtime_error("Unknown argument: " + arg);
        }
        if (cppRaw.empty() || cppMeta.empty() || refRaw.empty() || refMeta.empty()) {
            throw std::runtime_error("All raw matrix and metadata paths must be provided");
        }
        const FloatMatrix actual = readMatrix(cppRaw);
        const FloatMatrix reference = readMatrix(refRaw);
        if (actual.width != reference.width || actual.height != reference.height) {
            throw std::runtime_error("Matrix dimensions must match exactly");
        }
        validateMetadata(cppMeta, actual);
        validateMetadata(refMeta, reference);
        double sumSq = 0.0;
        double maxAbs = 0.0;
        std::size_t failures = 0;
        for (std::size_t i = 0; i < actual.data.size(); ++i) {
            const double a = actual.data[i];
            const double b = reference.data[i];
            if (!std::isfinite(a) || !std::isfinite(b)) {
                throw std::runtime_error("Matrix pixels must all be finite");
            }
            const double difference = std::abs(a - b);
            const double tolerance = atol + rtol * std::abs(b);
            if (!std::isfinite(tolerance)) {
                throw std::runtime_error("Tolerance calculation overflowed");
            }
            failures += difference > tolerance;
            sumSq += difference * difference;
            maxAbs = std::max(maxAbs, difference);
        }
        output << "Compared pixels: " << actual.data.size() << " (" << actual.width << 'x'
               << actual.height << ")\nRMS difference: "
               << std::sqrt(sumSq / static_cast<double>(actual.data.size()))
               << "\nMax absolute difference: " << maxAbs
               << "\nPixels outside tolerance: " << failures
               << "\nResult: " << (failures == 0 ? "PASS" : "FAIL") << '\n';
        return failures == 0 ? 0 : 2;
    } catch (const std::exception& error) {
        errors << error.what() << '\n';
        return 1;
    }
}

}  // namespace app
