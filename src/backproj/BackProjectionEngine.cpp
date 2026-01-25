#include "backproj/BackProjectionEngine.hpp"

#include <algorithm>
#include <array>
#include <complex>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

#include "backproj/FilterBank.hpp"
#include "backproj/ImageWriter.hpp"
#include "rpf/RpfConstants.hpp"
#include "rpf/RpfProductStreamLine.hpp"
#include "rpf/RpfWriter.hpp"

#include <unsupported/Eigen/FFT>

namespace backproj {

namespace {

struct SyntheticTarget {
    float amplitude = 1.0f;
    float freqRange = 0.1f;
    float freqAzimuth = 0.1f;
    float phase = 0.0f;
};

Eigen::MatrixXcf synthesizePhaseHistory(int rows, int cols) {
    Eigen::MatrixXcf data(rows, cols);
    data.setZero();
    if (rows <= 0 || cols <= 0) {
        return data;
    }

    const std::array<SyntheticTarget, 3> targets{{
        {1.0f, 0.08f, 0.11f, 0.0f},
        {0.7f, 0.21f, 0.17f, 1.2f},
        {0.5f, 0.33f, 0.05f, 2.4f},
    }};

    const float twoPi = 2.0f * static_cast<float>(std::numbers::pi);
    for (int row = 0; row < rows; ++row) {
        const float az = static_cast<float>(row) / static_cast<float>(rows);
        for (int col = 0; col < cols; ++col) {
            const float rg = static_cast<float>(col) / static_cast<float>(cols);
            std::complex<float> value(0.0f, 0.0f);
            for (const auto& target : targets) {
                const float phase =
                    twoPi * (target.freqRange * rg + target.freqAzimuth * az) + target.phase;
                value += target.amplitude *
                         std::complex<float>(std::cos(phase), std::sin(phase));
            }
            data(row, col) = value;
        }
    }

    return data;
}

void removeDc(Eigen::MatrixXcf& data) {
    if (data.size() == 0) {
        return;
    }
    std::complex<float> mean(0.0f, 0.0f);
    for (int row = 0; row < data.rows(); ++row) {
        for (int col = 0; col < data.cols(); ++col) {
            mean += data(row, col);
        }
    }
    mean /= static_cast<float>(data.size());
    data.array() -= mean;
}

void applyStcRamp(Eigen::MatrixXcf& data) {
    if (data.size() == 0) {
        return;
    }
    const int cols = data.cols();
    if (cols <= 1) {
        return;
    }
    for (int col = 0; col < cols; ++col) {
        const float scale = 0.5f + 0.5f * (static_cast<float>(col) / static_cast<float>(cols - 1));
        for (int row = 0; row < data.rows(); ++row) {
            data(row, col) *= scale;
        }
    }
}

bool loadRpfInput(const BackProjOperatorConfig& config,
                  Eigen::MatrixXf& image,
                  std::string& sourcePath) {
    if (config.inputFileName.empty()) {
        return false;
    }

    std::filesystem::path inputPath = config.inputFileName;
    if (!config.inputFilePath.empty()) {
        inputPath = std::filesystem::path(config.inputFilePath) / config.inputFileName;
    }
    if (!std::filesystem::exists(inputPath)) {
        return false;
    }

    rpf::RpfProductStreamLine stream;
    std::string error;
    if (!rpf::RpfProductStreamLine::init(inputPath.string(), 1, stream, error) ||
        stream.blocks().empty()) {
        return false;
    }

    const auto& block = stream.blocks().front();
    int startLine = block.startLine;
    if (config.firstRangeLine > 0) {
        startLine = std::max(startLine, static_cast<int>(config.firstRangeLine));
    }

    int totalLines = 0;
    for (const auto& entry : stream.blocks()) {
        totalLines += entry.numLines;
    }
    int availableLines = totalLines - (startLine - block.startLine);
    if (availableLines <= 0) {
        return false;
    }

    int linesToRead = availableLines;
    if (config.numLinesToProcess > 0) {
        linesToRead = std::min(linesToRead, static_cast<int>(config.numLinesToProcess));
    }

    image = Eigen::MatrixXf::Zero(linesToRead, block.numPixels);
    std::vector<float> lineData;
    int linesRead = 0;
    for (int i = 0; i < linesToRead; ++i) {
        const int lineNum = startLine + i;
        if (!stream.readLine(lineNum, lineData)) {
            break;
        }
        if (static_cast<int>(lineData.size()) != block.numPixels) {
            break;
        }
        for (int col = 0; col < block.numPixels; ++col) {
            image(linesRead, col) = lineData[static_cast<std::size_t>(col)];
        }
        ++linesRead;
    }

    if (linesRead == 0) {
        return false;
    }
    if (linesRead != image.rows()) {
        image.conservativeResize(linesRead, block.numPixels);
    }
    sourcePath = inputPath.string();
    return true;
}

void fftRows(Eigen::MatrixXcf& data) {
    Eigen::FFT<float> fft;
    const int cols = data.cols();
    std::vector<std::complex<float>> in(static_cast<std::size_t>(cols));
    std::vector<std::complex<float>> out(static_cast<std::size_t>(cols));
    for (int row = 0; row < data.rows(); ++row) {
        for (int col = 0; col < cols; ++col) {
            in[static_cast<std::size_t>(col)] = data(row, col);
        }
        fft.fwd(out, in);
        for (int col = 0; col < cols; ++col) {
            data(row, col) = out[static_cast<std::size_t>(col)];
        }
    }
}

void fftCols(Eigen::MatrixXcf& data) {
    Eigen::FFT<float> fft;
    const int rows = data.rows();
    std::vector<std::complex<float>> in(static_cast<std::size_t>(rows));
    std::vector<std::complex<float>> out(static_cast<std::size_t>(rows));
    for (int col = 0; col < data.cols(); ++col) {
        for (int row = 0; row < rows; ++row) {
            in[static_cast<std::size_t>(row)] = data(row, col);
        }
        fft.fwd(out, in);
        for (int row = 0; row < rows; ++row) {
            data(row, col) = out[static_cast<std::size_t>(row)];
        }
    }
}

Eigen::MatrixXf cropMagnitude(const Eigen::MatrixXcf& data, int targetRows, int targetCols) {
    if (data.size() == 0 || targetRows <= 0 || targetCols <= 0) {
        return {};
    }
    const int startRow = std::max(0, (data.rows() - targetRows) / 2);
    const int startCol = std::max(0, (data.cols() - targetCols) / 2);
    const int rows = std::min(targetRows, data.rows() - startRow);
    const int cols = std::min(targetCols, data.cols() - startCol);
    return data.block(startRow, startCol, rows, cols).cwiseAbs();
}

void applyAgc(Eigen::MatrixXf& image) {
    if (image.size() == 0) {
        return;
    }
    for (int row = 0; row < image.rows(); ++row) {
        const float mean = image.row(row).mean();
        if (mean > 0.0f) {
            image.row(row) /= mean;
        }
    }
}

void applyEdgeTaper(Eigen::MatrixXf& image, std::uint32_t pixels) {
    if (image.size() == 0 || pixels == 0) {
        return;
    }
    const int rows = image.rows();
    const int cols = image.cols();
    const int taper = static_cast<int>(std::min<std::uint32_t>(
        pixels, static_cast<std::uint32_t>(std::min(rows, cols) / 2)));
    if (taper <= 0) {
        return;
    }

    const float pi = static_cast<float>(std::numbers::pi);
    for (int i = 0; i < taper; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(taper + 1);
        const float weight = 0.5f - 0.5f * std::cos(pi * t);
        image.row(i) *= weight;
        image.row(rows - 1 - i) *= weight;
        image.col(i) *= weight;
        image.col(cols - 1 - i) *= weight;
    }
}

void buildFlatGrid(std::uint32_t rows, std::uint32_t cols, rpf::LatLongGrid& grid, std::uint16_t lines) {
    if (lines < 2) {
        lines = 2;
    }
    grid.lineNumber.assign(lines, 0);
    grid.beginGrSrRatio.assign(lines, 1.0);
    grid.midGrSrRatio.assign(lines, 1.0);
    grid.endGrSrRatio.assign(lines, 1.0);
    grid.beginLatitude.assign(lines, 0.0);
    grid.beginLongitude.assign(lines, 0.0);
    grid.midLatitude.assign(lines, 0.0);
    grid.midLongitude.assign(lines, 0.0);
    grid.endLatitude.assign(lines, 0.0);
    grid.endLongitude.assign(lines, 0.0);
    const std::uint32_t step = rows / lines;
    for (std::uint16_t i = 0; i < lines; ++i) {
        grid.lineNumber[i] = static_cast<int>(1 + i * std::max(1u, step));
    }
    (void)cols;
}

}  // namespace

BackProjectionEngine::BackProjectionEngine(BackProjOperatorConfig operatorConfig,
                                           BackProjSecondaryConfig secondaryConfig)
    : operatorConfig_(std::move(operatorConfig)),
      secondaryConfig_(std::move(secondaryConfig)) {}

Eigen::MatrixXf BackProjectionEngine::generateImage() {
    Eigen::MatrixXf inputImage;
    std::string sourcePath;
    const bool hasInput = loadRpfInput(operatorConfig_, inputImage, sourcePath);
    (void)sourcePath;

    if (!hasInput && (operatorConfig_.nPixX == 0 || operatorConfig_.nPixY == 0)) {
        return {};
    }

    const int baseRows = hasInput ? inputImage.rows() : static_cast<int>(operatorConfig_.nPixY);
    const int baseCols = hasInput ? inputImage.cols() : static_cast<int>(operatorConfig_.nPixX);
    const int targetRows =
        operatorConfig_.nPixY > 0 ? static_cast<int>(operatorConfig_.nPixY) : baseRows;
    const int targetCols =
        operatorConfig_.nPixX > 0 ? static_cast<int>(operatorConfig_.nPixX) : baseCols;

    const std::uint32_t extraRange =
        secondaryConfig_.quadParams.nExtraRngSamps + secondaryConfig_.nExtraRngSamps;
    const std::uint32_t padAz = secondaryConfig_.quadParams.nPadAzFftEachEnd;
    const double azOver = std::max(1.0, secondaryConfig_.quadParams.azOverSampFact);
    const int rows = static_cast<int>(std::lround(baseRows * azOver + padAz * 2));
    const int cols = static_cast<int>(baseCols + extraRange);

    Eigen::MatrixXcf phaseHistory;
    if (hasInput) {
        phaseHistory = Eigen::MatrixXcf::Zero(rows, cols);
        const int rowOffset = std::max(0, (rows - baseRows) / 2);
        const int colOffset = std::max(0, (cols - baseCols) / 2);
        for (int row = 0; row < baseRows; ++row) {
            for (int col = 0; col < baseCols; ++col) {
                phaseHistory(row + rowOffset, col + colOffset) =
                    std::complex<float>(inputImage(row, col), 0.0f);
            }
        }
    } else {
        phaseHistory = synthesizePhaseHistory(rows, cols);
    }
    if (secondaryConfig_.applyIqCalCorrection) {
        removeDc(phaseHistory);
    }
    if (secondaryConfig_.applyStcCorrection) {
        applyStcRamp(phaseHistory);
    }

    FilterBank filters(secondaryConfig_.rngFilterParams,
                       secondaryConfig_.azmFilterParams);
    const auto rangeWindow = filters.rangeWindow(static_cast<std::size_t>(cols));
    const auto azWindow = filters.azimuthWindow(static_cast<std::size_t>(rows));
    FilterBank::applyWindow(phaseHistory, rangeWindow, true);
    FilterBank::applyWindow(phaseHistory, azWindow, false);

    fftRows(phaseHistory);
    fftCols(phaseHistory);

    Eigen::MatrixXf image = cropMagnitude(phaseHistory, targetRows, targetCols);
    if (secondaryConfig_.applyAgcCorrection) {
        applyAgc(image);
    }
    applyEdgeTaper(image, secondaryConfig_.edgeTaperPixels);

    registrationManager_.configure(secondaryConfig_.frameRegistrationParams);
    if (operatorConfig_.applyFrameRegistration) {
        registrationManager_.registerFrame(image, true);
    }

    if (operatorConfig_.applyAutoFocus) {
        autofocusController_.analyzeFrame(image);
    }

    return image;
}

void BackProjectionEngine::run() {
    Eigen::MatrixXf image = generateImage();
    if (image.size() == 0) {
        return;
    }

    std::string outputPath = "backproj_output.tif";
    if (!operatorConfig_.rpfBaseFileName.empty()) {
        outputPath = operatorConfig_.rpfBaseFileName + "_backproj.tif";
    }
    writeTiff(outputPath, image);

    const std::string basePath =
        operatorConfig_.rpfBaseFileName.empty() ? "backproj" : operatorConfig_.rpfBaseFileName;
    const float minValue = image.minCoeff();
    const float maxValue = image.maxCoeff();
    {
        std::ofstream metaOut(basePath + "_backproj_meta.json");
        if (metaOut) {
            metaOut << "{\n"
                    << "  \"width\": " << image.cols() << ",\n"
                    << "  \"height\": " << image.rows() << ",\n"
                    << "  \"minValue\": " << minValue << ",\n"
                    << "  \"maxValue\": " << maxValue << "\n"
                    << "}\n";
        }
    }

    if (operatorConfig_.outputDebugRpf) {
        rpf::RpfWriteOptions options{};
        options.pixelType = 2;
        options.radarMode = rpf::RpfConstants::kLandspotMode;
        options.fileId = basePath;
        options.geolocationGridNumLines = 2;
        rpf::LatLongGrid grid{};
        buildFlatGrid(static_cast<std::uint32_t>(image.rows()),
                      static_cast<std::uint32_t>(image.cols()),
                      grid,
                      options.geolocationGridNumLines);
        std::string error;
        (void)rpf::writeRpfFile(basePath + "_backproj.rpf", image, options, grid, error);
    }

    if (!registrationManager_.results().empty()) {
        registrationManager_.saveJson(basePath + "_registration.json");
    }
    if (!autofocusController_.results().empty()) {
        autofocusController_.saveJson(basePath + "_autofocus.json");
    }
}

}  // namespace backproj
