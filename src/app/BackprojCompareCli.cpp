#include <cmath>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

struct FloatMatrix {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> data;
};

bool readU32Le(std::ifstream& input, std::uint32_t& value) {
    std::array<std::uint8_t, 4> buffer{};
    if (!input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()))) {
        return false;
    }
    value = static_cast<std::uint32_t>(buffer[0]) |
            (static_cast<std::uint32_t>(buffer[1]) << 8) |
            (static_cast<std::uint32_t>(buffer[2]) << 16) |
            (static_cast<std::uint32_t>(buffer[3]) << 24);
    return true;
}

bool readFloatMatrix(const std::string& path, FloatMatrix& matrix) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    if (!readU32Le(input, matrix.width) || !readU32Le(input, matrix.height)) {
        return false;
    }

    const std::size_t count = static_cast<std::size_t>(matrix.width) * matrix.height;
    matrix.data.resize(count);
    if (!input.read(reinterpret_cast<char*>(matrix.data.data()),
                    static_cast<std::streamsize>(count * sizeof(float)))) {
        return false;
    }
    return true;
}

bool readJson(const std::string& path, nlohmann::json& output) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    try {
        input >> output;
        return true;
    } catch (...) {
        return false;
    }
}

void printUsage() {
    std::cout << "Usage: backproj_compare_cli --cpp-raw <path> --cpp-meta <path> "
                 "--ref-raw <path> --ref-meta <path>\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string cppRaw;
    std::string cppMeta;
    std::string refRaw;
    std::string refMeta;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--cpp-raw" && i + 1 < argc) {
            cppRaw = argv[++i];
        } else if (arg == "--cpp-meta" && i + 1 < argc) {
            cppMeta = argv[++i];
        } else if (arg == "--ref-raw" && i + 1 < argc) {
            refRaw = argv[++i];
        } else if (arg == "--ref-meta" && i + 1 < argc) {
            refMeta = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            printUsage();
            return 1;
        }
    }

    if (cppRaw.empty() || cppMeta.empty() || refRaw.empty() || refMeta.empty()) {
        std::cerr << "All --cpp-raw/--cpp-meta/--ref-raw/--ref-meta must be provided.\n";
        printUsage();
        return 1;
    }

    FloatMatrix cppMatrix;
    FloatMatrix refMatrix;
    if (!readFloatMatrix(cppRaw, cppMatrix)) {
        std::cerr << "Failed to read C++ raw matrix: " << cppRaw << "\n";
        return 1;
    }
    if (!readFloatMatrix(refRaw, refMatrix)) {
        std::cerr << "Failed to read reference raw matrix: " << refRaw << "\n";
        return 1;
    }

    nlohmann::json cppInfo;
    nlohmann::json refInfo;
    if (!readJson(cppMeta, cppInfo)) {
        std::cerr << "Failed to read C++ metadata: " << cppMeta << "\n";
        return 1;
    }
    if (!readJson(refMeta, refInfo)) {
        std::cerr << "Failed to read reference metadata: " << refMeta << "\n";
        return 1;
    }

    const std::uint32_t compWidth = std::min(cppMatrix.width, refMatrix.width);
    const std::uint32_t compHeight = std::min(cppMatrix.height, refMatrix.height);
    const std::size_t totalPixels = static_cast<std::size_t>(compWidth) * compHeight;

    double sumSq = 0.0;
    double sumAbs = 0.0;
    double maxAbs = 0.0;
    for (std::uint32_t row = 0; row < compHeight; ++row) {
        for (std::uint32_t col = 0; col < compWidth; ++col) {
            const std::size_t idxCpp = static_cast<std::size_t>(row) * cppMatrix.width + col;
            const std::size_t idxRef = static_cast<std::size_t>(row) * refMatrix.width + col;
            const double diff = static_cast<double>(cppMatrix.data[idxCpp]) - static_cast<double>(refMatrix.data[idxRef]);
            const double absDiff = std::abs(diff);
            sumSq += diff * diff;
            sumAbs += absDiff;
            if (absDiff > maxAbs) {
                maxAbs = absDiff;
            }
        }
    }

    std::cout << "Comparison summary:\n";
    std::cout << "- Compared pixels: " << totalPixels << " (" << compWidth << "x" << compHeight << ")\n";
    if (totalPixels > 0) {
        const double rms = std::sqrt(sumSq / static_cast<double>(totalPixels));
        std::cout << "- RMS difference: " << rms << "\n";
        std::cout << "- Mean abs difference: " << sumAbs / static_cast<double>(totalPixels) << "\n";
        std::cout << "- Max absolute difference: " << maxAbs << "\n";
    } else {
        std::cout << "- No overlapping pixels to compare.\n";
    }

    auto printMeta = [](const std::string& label, const nlohmann::json& meta) {
        std::cout << label << " metadata:\n";
        if (meta.contains("width") && meta.contains("height")) {
            std::cout << "  size: " << meta["width"] << "x" << meta["height"] << "\n";
        }
        if (meta.contains("minValue") && meta.contains("maxValue")) {
            std::cout << "  min: " << meta["minValue"] << ", max: " << meta["maxValue"] << "\n";
        }
    };
    printMeta("C++", cppInfo);
    printMeta("Reference", refInfo);

    if (cppInfo.contains("minValue") && refInfo.contains("minValue")) {
        const double diff = std::abs(static_cast<double>(cppInfo["minValue"]) - static_cast<double>(refInfo["minValue"]));
        std::cout << "- minValue delta: " << diff << "\n";
    }
    if (cppInfo.contains("maxValue") && refInfo.contains("maxValue")) {
        const double diff = std::abs(static_cast<double>(cppInfo["maxValue"]) - static_cast<double>(refInfo["maxValue"]));
        std::cout << "- maxValue delta: " << diff << "\n";
    }

    return 0;
}
