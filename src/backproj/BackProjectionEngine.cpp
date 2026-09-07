#include "backproj/BackProjectionEngine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "backproj/FilterBank.hpp"
#include "backproj/ImageWriter.hpp"
#include "rpf/RpfConstants.hpp"
#include "rpf/RpfProductStreamLine.hpp"
#include "rpf/RpfWriter.hpp"

#include <nlohmann/json.hpp>

namespace backproj {

namespace {

bool profilingEnabled() {
    static bool enabled = (std::getenv("SAR_BACKPROJ_PROFILE") != nullptr);
    return enabled;
}

class ScopedProfiler {
public:
    explicit ScopedProfiler(const char* label)
        : label_(label), start_(std::chrono::steady_clock::now()), enabled_(profilingEnabled()) {}

    ~ScopedProfiler() {
        if (enabled_) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_);
            std::cerr << "[BackProjectionEngine] " << label_ << ": " << elapsed.count() << " ms\n";
        }
    }

private:
    const char* label_;
    std::chrono::steady_clock::time_point start_;
    bool enabled_;
};

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

Eigen::MatrixXf collapseColumns(const Eigen::MatrixXf& input, std::uint32_t factor) {
    if (factor <= 1 || input.size() == 0) {
        return input;
    }
    const int outCols = static_cast<int>(input.cols() / static_cast<int>(factor));
    if (outCols <= 0) {
        return input;
    }
    Eigen::MatrixXf output(input.rows(), outCols);
    for (int row = 0; row < input.rows(); ++row) {
        for (int col = 0; col < outCols; ++col) {
            float sum = 0.0f;
            const int start = col * static_cast<int>(factor);
            for (std::uint32_t k = 0; k < factor; ++k) {
                sum += input(row, start + static_cast<int>(k));
            }
            output(row, col) = sum / static_cast<float>(factor);
        }
    }
    return output;
}

Eigen::MatrixXf collapseRows(const Eigen::MatrixXf& input, std::uint32_t factor) {
    if (factor <= 1 || input.size() == 0) {
        return input;
    }
    const int outRows = static_cast<int>(input.rows() / static_cast<int>(factor));
    if (outRows <= 0) {
        return input;
    }
    Eigen::MatrixXf output(outRows, input.cols());
    for (int row = 0; row < outRows; ++row) {
        const int start = row * static_cast<int>(factor);
        for (int col = 0; col < input.cols(); ++col) {
            float sum = 0.0f;
            for (std::uint32_t k = 0; k < factor; ++k) {
                sum += input(start + static_cast<int>(k), col);
            }
            output(row, col) = sum / static_cast<float>(factor);
        }
    }
    return output;
}

void applyComplexTaper(Eigen::MatrixXcf& data, std::uint32_t rngTaper, std::uint32_t azTaper) {
    if (data.size() == 0) {
        return;
    }
    const int rows = data.rows();
    const int cols = data.cols();
    const int rng = static_cast<int>(std::min<std::uint32_t>(rngTaper, cols / 2));
    const int az = static_cast<int>(std::min<std::uint32_t>(azTaper, rows / 2));
    if (rng <= 0 && az <= 0) {
        return;
    }
    const float pi = static_cast<float>(std::numbers::pi);
    for (int i = 0; i < rng; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(rng + 1);
        const float weight = 0.5f - 0.5f * std::cos(pi * t);
        for (int row = 0; row < rows; ++row) {
            data(row, i) *= weight;
            data(row, cols - 1 - i) *= weight;
        }
    }
    for (int i = 0; i < az; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(az + 1);
        const float weight = 0.5f - 0.5f * std::cos(pi * t);
        for (int col = 0; col < cols; ++col) {
            data(i, col) *= weight;
            data(rows - 1 - i, col) *= weight;
        }
    }
}

bool loadRpfInput(const BackProjOperatorConfig& config,
                  Eigen::MatrixXf& image,
                  rpf::LatLongGrid& grid,
                  bool& hasGrid,
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

    if (linesRead != linesToRead || !image.allFinite()) {
        return false;
    }
    grid = stream.latLongGrid();
    hasGrid = !grid.lineNumber.empty();
    sourcePath = inputPath.string();
    return true;
}

Eigen::MatrixXf cropMagnitude(const Eigen::MatrixXcf& data, int targetRows, int targetCols) {
    if (data.size() == 0 || targetRows <= 0 || targetCols <= 0) {
        return {};
    }
    const int dataRows = static_cast<int>(data.rows());
    const int dataCols = static_cast<int>(data.cols());
    const int startRow = std::max(0, (dataRows - targetRows) / 2);
    const int startCol = std::max(0, (dataCols - targetCols) / 2);
    const int rows = std::min(targetRows, dataRows - startRow);
    const int cols = std::min(targetCols, dataCols - startCol);
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

struct TileLayout {
    std::uint32_t tilesX = 1;
    std::uint32_t tilesY = 1;
    std::uint32_t overlap = 0;
    std::uint32_t tileWidth = 0;
    std::uint32_t tileHeight = 0;
};

float edgeWeight(int index, int size, int overlap) {
    if (overlap <= 0) {
        return 1.0f;
    }
    const int distToStart = index;
    const int distToEnd = size - 1 - index;
    float weight = 1.0f;
    if (distToStart < overlap) {
        const float t = static_cast<float>(distToStart + 1) / static_cast<float>(overlap + 1);
        weight *= 0.5f - 0.5f * std::cos(static_cast<float>(std::numbers::pi) * t);
    }
    if (distToEnd < overlap) {
        const float t = static_cast<float>(distToEnd + 1) / static_cast<float>(overlap + 1);
        weight *= 0.5f - 0.5f * std::cos(static_cast<float>(std::numbers::pi) * t);
    }
    return weight;
}

Eigen::MatrixXf applyTileBlend(const Eigen::MatrixXf& image,
                               std::uint32_t tilesX,
                               std::uint32_t tilesY,
                               std::uint32_t overlap,
                               TileLayout& layout) {
    if (image.size() == 0 || tilesX == 0 || tilesY == 0) {
        return image;
    }
    const int rows = image.rows();
    const int cols = image.cols();
    const int tileWidth = static_cast<int>((cols + static_cast<int>(tilesX) - 1) /
                                          static_cast<int>(tilesX));
    const int tileHeight = static_cast<int>((rows + static_cast<int>(tilesY) - 1) /
                                           static_cast<int>(tilesY));
    if (tileWidth <= 0 || tileHeight <= 0) {
        return image;
    }

    layout.tilesX = tilesX;
    layout.tilesY = tilesY;
    layout.overlap = overlap;
    layout.tileWidth = static_cast<std::uint32_t>(tileWidth);
    layout.tileHeight = static_cast<std::uint32_t>(tileHeight);

    Eigen::MatrixXf accum = Eigen::MatrixXf::Zero(rows, cols);
    Eigen::MatrixXf weights = Eigen::MatrixXf::Zero(rows, cols);

    for (std::uint32_t ty = 0; ty < tilesY; ++ty) {
        for (std::uint32_t tx = 0; tx < tilesX; ++tx) {
            const int startRow = static_cast<int>(ty) * tileHeight;
            const int startCol = static_cast<int>(tx) * tileWidth;
            const int endRow = std::min(rows, startRow + tileHeight);
            const int endCol = std::min(cols, startCol + tileWidth);

            for (int row = startRow; row < endRow; ++row) {
                const int localRow = row - startRow;
                const float wy = edgeWeight(localRow, endRow - startRow, static_cast<int>(overlap));
                for (int col = startCol; col < endCol; ++col) {
                    const int localCol = col - startCol;
                    const float wx = edgeWeight(localCol, endCol - startCol, static_cast<int>(overlap));
                    const float w = wx * wy;
                    accum(row, col) += image(row, col) * w;
                    weights(row, col) += w;
                }
            }
        }
    }

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            if (weights(row, col) > 0.0f) {
                accum(row, col) /= weights(row, col);
            }
        }
    }
    return accum;
}

}  // namespace

void BackProjectionEngine::fftRows(Eigen::MatrixXcf& data) {
    if (data.size() == 0) {
        return;
    }
    const int cols = data.cols();
    const int rows = data.rows();
    fftRowInput_.resize(static_cast<std::size_t>(cols));
    fftRowOutput_.resize(static_cast<std::size_t>(cols));
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            fftRowInput_[static_cast<std::size_t>(col)] = data(row, col);
        }
        rowFft_.fwd(fftRowOutput_, fftRowInput_);
        for (int col = 0; col < cols; ++col) {
            data(row, col) = fftRowOutput_[static_cast<std::size_t>(col)];
        }
    }
}

void BackProjectionEngine::fftCols(Eigen::MatrixXcf& data) {
    if (data.size() == 0) {
        return;
    }
    const int cols = data.cols();
    const int rows = data.rows();
    fftColInput_.resize(static_cast<std::size_t>(rows));
    fftColOutput_.resize(static_cast<std::size_t>(rows));
    for (int col = 0; col < cols; ++col) {
        for (int row = 0; row < rows; ++row) {
            fftColInput_[static_cast<std::size_t>(row)] = data(row, col);
        }
        colFft_.fwd(fftColOutput_, fftColInput_);
        for (int row = 0; row < rows; ++row) {
            data(row, col) = fftColOutput_[static_cast<std::size_t>(row)];
        }
    }
}

BackProjectionEngine::BackProjectionEngine(BackProjOperatorConfig operatorConfig,
                                           BackProjSecondaryConfig secondaryConfig)
    : operatorConfig_(std::move(operatorConfig)),
      secondaryConfig_(std::move(secondaryConfig)) {}

Eigen::MatrixXf BackProjectionEngine::generateImage() {
    lastError_.clear();
    Eigen::MatrixXf inputImage;
    std::string sourcePath;
    rpf::LatLongGrid grid{};
    bool hasGrid = false;
    ScopedProfiler loadTimer("load_rpf");
    const bool hasInput = loadRpfInput(operatorConfig_, inputImage, grid, hasGrid, sourcePath);
    lastSourcePath_ = sourcePath;
    lastLatLongGrid_ = grid;
    hasLatLongGrid_ = hasGrid;

    if (!hasInput &&
        (!operatorConfig_.inputFileName.empty() || !operatorConfig_.allowSyntheticInput)) {
        return fail(
            "A readable RPF input is required; synthetic demonstration input requires "
            "allowSyntheticInput=true and no input filename.");
    }
    if (!hasInput && (operatorConfig_.nPixX == 0 || operatorConfig_.nPixY == 0)) {
        return fail("Synthetic demonstration image dimensions must be positive.");
    }

    ScopedProfiler phaseProfiler("phase_history");
    if (hasInput) {
        if (operatorConfig_.collapseFactor > 1) {
            inputImage = collapseColumns(inputImage, operatorConfig_.collapseFactor);
        }
        const std::uint32_t azFactor = static_cast<std::uint32_t>(
            std::max(1.0, operatorConfig_.azimuthCollapseFactor));
        if (azFactor > 1) {
            inputImage = collapseRows(inputImage, azFactor);
        }
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
    applyComplexTaper(phaseHistory,
                      secondaryConfig_.quadParams.nRngTaper,
                      secondaryConfig_.quadParams.nAzmTaper);

    {
        ScopedProfiler filterTimer("filter_window");
        FilterBank filters(secondaryConfig_.rngFilterParams,
                           secondaryConfig_.azmFilterParams);
        const auto rangeWindow = filters.rangeWindow(static_cast<std::size_t>(cols));
        const auto azWindow = filters.azimuthWindow(static_cast<std::size_t>(rows));
        FilterBank::applyWindow(phaseHistory, rangeWindow, true);
        FilterBank::applyWindow(phaseHistory, azWindow, false);
    }
    {
        ScopedProfiler fftTimer("fft_rows");
        fftRows(phaseHistory);
    }
    {
        ScopedProfiler fftTimer("fft_cols");
        fftCols(phaseHistory);
    }

    ScopedProfiler cropProfiler("crop_magnitude");
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
        autofocusController_.configure(secondaryConfig_.autofocusParams);
        autofocusController_.analyzeFrame(image);
    }

    return image;
}

Eigen::MatrixXf BackProjectionEngine::runWithOutputs() {
    Eigen::MatrixXf image = generateImage();
    if (image.size() == 0) {
        return image;
    }

    TileLayout tileLayout{};
    if ((operatorConfig_.numTilesX > 1 || operatorConfig_.numTilesY > 1 ||
         secondaryConfig_.tileOverlap > 0) &&
        operatorConfig_.numTilesX > 0 && operatorConfig_.numTilesY > 0) {
        image = applyTileBlend(image,
                               operatorConfig_.numTilesX,
                               operatorConfig_.numTilesY,
                               secondaryConfig_.tileOverlap,
                               tileLayout);
    }

    const std::string basePath =
        operatorConfig_.rpfBaseFileName.empty() ? "backproj" : operatorConfig_.rpfBaseFileName;
    if (!writeTiff(basePath + "_backproj.tif", image)) {
        return fail("Failed to write TIFF: " + basePath);
    }
    if (!operatorConfig_.fastMode) {
        if (!writeNormalizedTiff(basePath + "_backproj_norm.tif", image,
                                 secondaryConfig_.imageScaling)) {
            return fail("Failed to write normalized TIFF: " + basePath);
        }
    }
    if (!writeRawFloat(basePath + "_backproj.raw", image)) {
        return fail("Failed to write raw image: " + basePath);
    }
    const float minValue = image.minCoeff();
    const float maxValue = image.maxCoeff();
    const float range = (maxValue > minValue) ? (maxValue - minValue) : 1.0f;
    const double scale = 65535.0 / static_cast<double>(range);
    const double offset = -static_cast<double>(minValue) * scale;
    {
        nlohmann::json payload;
        payload["width"] = image.cols();
        payload["height"] = image.rows();
        payload["processingAlgorithm"] = "legacy_magnitude_2d_fft_demo";
        payload["syntheticInput"] = lastSourcePath_.empty();
        payload["minValue"] = minValue;
        payload["maxValue"] = maxValue;
        payload["scale"] = scale;
        payload["offset"] = offset;
        payload["outputProjection"] = operatorConfig_.outputProjection;
        payload["pixelSpacing"] = operatorConfig_.pixelSpacing;
        payload["imageOffsetSpec"] = operatorConfig_.imageOffsetSpec;
        payload["sourcePath"] = [&]() {
            std::string path = lastSourcePath_;
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
        }();
        payload["tileLayout"] = {
            {"tilesX", tileLayout.tilesX},
            {"tilesY", tileLayout.tilesY},
            {"overlap", tileLayout.overlap},
            {"tileWidth", tileLayout.tileWidth},
            {"tileHeight", tileLayout.tileHeight}
        };

        std::ofstream metaOut(basePath + "_backproj_meta.json");
        metaOut << payload.dump(2);
        metaOut.close();
        if (!metaOut) {
            return fail("Failed to write metadata: " + basePath);
        }
    }

    if (operatorConfig_.outputDebugRpf) {
        rpf::RpfWriteOptions options{};
        options.pixelType = 2;
        options.radarMode = rpf::RpfConstants::kLandspotMode;
        options.fileId = basePath;
        options.geolocationGridNumLines = 2;
        rpf::LatLongGrid grid{};
        if (hasLatLongGrid_) {
            grid = lastLatLongGrid_;
        } else {
            buildFlatGrid(static_cast<std::uint32_t>(image.rows()),
                          static_cast<std::uint32_t>(image.cols()),
                          grid,
                          options.geolocationGridNumLines);
        }
        std::string error;
        if (!rpf::writeRpfFile(basePath + "_backproj.rpf", image, options, grid, error)) {
            return fail("Failed to write RPF: " + error);
        }
    }

    if (!operatorConfig_.fastMode) {
        if (!registrationManager_.results().empty()) {
            if (!registrationManager_.saveJson(basePath + "_registration.json")) {
                return fail("Failed to write registration report: " + basePath);
            }
        }
        if (!autofocusController_.results().empty()) {
            if (!autofocusController_.saveJson(basePath + "_autofocus.json")) {
                return fail("Failed to write autofocus report: " + basePath);
            }
        }
    }

    return image;
}

void BackProjectionEngine::run() {
    (void)runWithOutputs();
}

Eigen::MatrixXf BackProjectionEngine::fail(const std::string& error) {
    lastError_ = error;
    std::cerr << "[BackProjectionEngine] " << error << '\n';
    return {};
}

}  // namespace backproj
