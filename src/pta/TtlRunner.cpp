#include "pta/TtlRunner.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <Eigen/Core>

#include "pta/IqaAnalyzer.hpp"
#include "pta/PtaAnalyzer.hpp"
#include "pta/PtaHistogram.hpp"

namespace pta {

namespace {

struct TtlConfig {
    std::string reportPath = "pta_report.txt";
    std::string reportFormat = "json";
    std::string chipPath;
    std::vector<float> chipData;
    int chipRows = 0;
    int chipCols = 0;
    std::size_t maxPeaks = 4;
    std::size_t minSeparation = 1;
    std::size_t histogramBins = 32;
    std::size_t fftSize = 0;
    std::uint32_t magFactor = 1;
    double zpAlpha = 0.0;
    std::string powerDetection;
    std::string sideLobeMethod;
    bool analyze2D = false;
    bool useIqa = false;
    bool useIqa2D = false;
};

std::string trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::unordered_map<std::string, std::string> readKeyValueConfig(std::ifstream& input) {
    std::unordered_map<std::string, std::string> config;
    std::string line;
    while (std::getline(input, line)) {
        const auto commentPos = line.find_first_of("#%");
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (!key.empty()) {
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            config[key] = value;
        }
    }
    return config;
}

bool parseBool(const std::string& value, bool fallback) {
    const std::string lowered = trim(value);
    if (lowered == "1" || lowered == "true" || lowered == "yes") return true;
    if (lowered == "0" || lowered == "false" || lowered == "no") return false;
    return fallback;
}

int parseInt(const std::string& value, int fallback) {
    try {
        return std::stoi(trim(value));
    } catch (...) {
        return fallback;
    }
}

std::size_t parseSize(const std::string& value, std::size_t fallback) {
    try {
        return static_cast<std::size_t>(std::stoll(trim(value)));
    } catch (...) {
        return fallback;
    }
}

double parseDouble(const std::string& value, double fallback) {
    try {
        return std::stod(trim(value));
    } catch (...) {
        return fallback;
    }
}

std::vector<float> parseInlineData(const std::string& value) {
    std::vector<float> data;
    if (value.find(',') == std::string::npos) {
        std::stringstream stream(value);
        float v = 0.0f;
        while (stream >> v) {
            data.push_back(v);
        }
        return data;
    }

    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        std::stringstream sub(trim(token));
        float v = 0.0f;
        if (sub >> v) {
            data.push_back(v);
        }
    }
    return data;
}

std::vector<float> readChipFile(const std::string& path) {
    std::ifstream input(path);
    std::vector<float> data;
    if (!input) {
        return data;
    }
    float value = 0.0f;
    while (input >> value) {
        data.push_back(value);
    }
    return data;
}

TtlConfig loadConfig(std::ifstream& input, const std::string& path) {
    TtlConfig config{};
    config.reportPath = "pta_report.txt";
    config.reportFormat = "json";

    bool useKeyValue = (path.size() >= 4 && path.substr(path.size() - 4) == ".prs");
    if (!useKeyValue) {
        nlohmann::json json;
        try {
            input >> json;
            config.reportPath = json.value("reportPath", config.reportPath);
            config.reportFormat = json.value("reportFormat", config.reportFormat);
            config.chipPath = json.value("chipPath", "");
            config.maxPeaks = json.value("maxPeaks", config.maxPeaks);
            config.minSeparation = json.value("minSeparation", config.minSeparation);
            config.histogramBins = json.value("histogramBins", config.histogramBins);
            config.fftSize = json.value("fftSize", config.fftSize);
            config.magFactor = json.value("magFactor", config.magFactor);
            config.zpAlpha = json.value("zpAlpha", config.zpAlpha);
            config.powerDetection = json.value("powerDetection", "");
            config.sideLobeMethod = json.value("sideLobeMethod", "");
            config.analyze2D = json.value("analyze2D", config.analyze2D);
            config.useIqa2D = json.value("useIqa2D", config.useIqa2D);
            if (json.contains("chipData") && json["chipData"].is_array()) {
                if (!json["chipData"].empty() && json["chipData"].front().is_array()) {
                    config.chipRows = static_cast<int>(json["chipData"].size());
                    config.chipCols = static_cast<int>(json["chipData"].front().size());
                    for (const auto& row : json["chipData"]) {
                        for (const auto& entry : row) {
                            config.chipData.push_back(entry.get<float>());
                        }
                    }
                } else {
                    for (const auto& entry : json["chipData"]) {
                        config.chipData.push_back(entry.get<float>());
                    }
                }
            }
            return config;
        } catch (const nlohmann::json::parse_error&) {
            useKeyValue = true;
        }
    }

    if (useKeyValue) {
        input.clear();
        input.seekg(0);
        const auto kv = readKeyValueConfig(input);
        const auto reportIt = kv.find("reportpath");
        if (reportIt != kv.end()) {
            config.reportPath = reportIt->second;
        }
        const auto formatIt = kv.find("reportformat");
        if (formatIt != kv.end()) {
            config.reportFormat = formatIt->second;
        }
        const auto chipPathIt = kv.find("chippath");
        if (chipPathIt != kv.end()) {
            config.chipPath = chipPathIt->second;
        }
        const auto chipDataIt = kv.find("chipdata");
        if (chipDataIt != kv.end()) {
            config.chipData = parseInlineData(chipDataIt->second);
        }
        config.chipRows = parseInt(kv.count("chiprows") ? kv.at("chiprows") : "", config.chipRows);
        config.chipCols = parseInt(kv.count("chipcols") ? kv.at("chipcols") : "", config.chipCols);
        config.maxPeaks = parseSize(kv.count("maxpeaks") ? kv.at("maxpeaks") : "", config.maxPeaks);
        config.minSeparation =
            parseSize(kv.count("minseparation") ? kv.at("minseparation") : "", config.minSeparation);
        config.histogramBins =
            parseSize(kv.count("histogrambins") ? kv.at("histogrambins") : "", config.histogramBins);
        config.fftSize = parseSize(kv.count("fftsize") ? kv.at("fftsize") : "", config.fftSize);
        config.magFactor = static_cast<std::uint32_t>(
            parseSize(kv.count("magfactor") ? kv.at("magfactor") : "", config.magFactor));
        config.zpAlpha = parseDouble(kv.count("zpalpha") ? kv.at("zpalpha") : "", config.zpAlpha);
        config.powerDetection = kv.count("powerdetection") ? kv.at("powerdetection") : "";
        config.sideLobeMethod = kv.count("sidelobemethod") ? kv.at("sidelobemethod") : "";
        config.analyze2D = parseBool(kv.count("analyze2d") ? kv.at("analyze2d") : "", config.analyze2D);
        config.useIqa = parseBool(kv.count("useiqa") ? kv.at("useiqa") : "", config.useIqa);
        config.useIqa2D = parseBool(kv.count("useiqa2d") ? kv.at("useiqa2d") : "", config.useIqa2D);
    }

    return config;
}

Eigen::MatrixXf buildChipMatrix(const TtlConfig& config,
                                const std::vector<float>& data) {
    if (data.empty()) {
        return {};
    }
    if (config.chipRows > 0 && config.chipCols > 0 &&
        static_cast<std::size_t>(config.chipRows * config.chipCols) <= data.size()) {
        Eigen::MatrixXf chip(config.chipRows, config.chipCols);
        std::size_t idx = 0;
        for (int row = 0; row < config.chipRows; ++row) {
            for (int col = 0; col < config.chipCols; ++col) {
                chip(row, col) = data[idx++];
            }
        }
        return chip;
    }
    Eigen::MatrixXf chip(1, static_cast<int>(data.size()));
    for (std::size_t i = 0; i < data.size(); ++i) {
        chip(0, static_cast<int>(i)) = data[i];
    }
    return chip;
}

std::vector<float> flattenChip(const Eigen::MatrixXf& chip) {
    std::vector<float> data;
    if (chip.size() == 0) {
        return data;
    }
    if (chip.rows() == 1 || chip.cols() == 1) {
        data.reserve(static_cast<std::size_t>(chip.size()));
        for (int i = 0; i < chip.size(); ++i) {
            data.push_back(chip(i));
        }
        return data;
    }
    data.assign(static_cast<std::size_t>(chip.cols()), 0.0f);
    for (int row = 0; row < chip.rows(); ++row) {
        for (int col = 0; col < chip.cols(); ++col) {
            data[static_cast<std::size_t>(col)] += chip(row, col);
        }
    }
    return data;
}

}  // namespace

bool TtlRunner::runFromConfig(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }

    const TtlConfig config = loadConfig(input, path);

    std::ofstream report(config.reportPath);
    if (!report) {
        return false;
    }

    const std::string normalizedFormat =
        (config.reportFormat == "json" || config.reportFormat == "text")
            ? config.reportFormat
            : "text";

    std::vector<float> chipData = config.chipData;
    if (chipData.empty() && !config.chipPath.empty()) {
        chipData = readChipFile(config.chipPath);
    }

    PtaAnalyzer analyzer;
    PtaAnalyzer::PtaAnalysisResult analysis{};
    std::pair<PtaStats, PtaStats> stats2d{};
    PtaHistogram hist{};
    IqaAnalyzer iqaAnalyzer;
    IqaPeakResult iqaResult{};
    IqaPeak2DResult iqa2dResult{};
    std::string status = "no_input";

    if (!chipData.empty()) {
        PtaChip chip{};
        chip.magFactor = config.magFactor;
        chip.zpAlpha = config.zpAlpha;
        chip.powerDetection = config.powerDetection;
        chip.sideLobeMethod = config.sideLobeMethod;
        chip.chipIn = buildChipMatrix(config, chipData);
        analysis = analyzer.analyze1DWithZoom(chip, config.fftSize);
        analysis.peaks = analyzer.findPeaks1D(chip, config.maxPeaks, config.minSeparation);
        if (config.analyze2D) {
            stats2d = analyzer.analyze2D(chip);
        }
        hist = generateHistogram(analysis.zoomPower,
                                 std::max<std::size_t>(1, config.histogramBins));
        if (config.useIqa || !config.powerDetection.empty() || !config.sideLobeMethod.empty()) {
            IqaPeakOptions iqaOptions{};
            iqaOptions.maxPeaks = config.maxPeaks;
            iqaOptions.minSeparation = config.minSeparation;
            iqaOptions.fftSize = config.fftSize;
            iqaOptions.magFactor = config.magFactor;
            iqaOptions.zpAlpha = config.zpAlpha;
            iqaOptions.powerDetection = config.powerDetection;
            iqaOptions.sideLobeMethod = config.sideLobeMethod;
            const auto profile = flattenChip(chip.chipIn);
            iqaResult = iqaAnalyzer.process1DPeaks(profile, iqaOptions);
            if (config.useIqa2D) {
                iqa2dResult = iqaAnalyzer.process2DPeaks(chip.chipIn, iqaOptions);
            }
        }
        status = "ok";
    }

    if (normalizedFormat == "json") {
        nlohmann::json payload;
        payload["reportFormat"] = normalizedFormat;
        payload["inputConfig"] = path;
        payload["status"] = status;
        payload["maxPeaks"] = config.maxPeaks;
        payload["minSeparation"] = config.minSeparation;
        payload["histogramBins"] = config.histogramBins;
        payload["stats"] = {
            {"irw", analysis.stats.irw},
            {"mslr", analysis.stats.mslr},
            {"islr", analysis.stats.islr},
            {"pos", analysis.stats.pos},
            {"maxPower", analysis.stats.maxPower}
        };
        if (config.analyze2D) {
            payload["stats2d"] = {
                {"x", {{"irw", stats2d.first.irw},
                       {"mslr", stats2d.first.mslr},
                       {"islr", stats2d.first.islr},
                       {"pos", stats2d.first.pos},
                       {"maxPower", stats2d.first.maxPower}}},
                {"y", {{"irw", stats2d.second.irw},
                       {"mslr", stats2d.second.mslr},
                       {"islr", stats2d.second.islr},
                       {"pos", stats2d.second.pos},
                       {"maxPower", stats2d.second.maxPower}}}
            };
        }
        payload["peaks"] = nlohmann::json::array();
        for (const auto& peak : analysis.peaks) {
            payload["peaks"].push_back({{"index", peak.index}, {"power", peak.power}});
        }
        if (!iqaResult.zoomPower.empty() || !iqaResult.peaks.empty()) {
            payload["iqa"] = {
                {"powerDetection", config.powerDetection},
                {"sideLobeMethod", config.sideLobeMethod},
                {"stats", {{"irw", iqaResult.stats.irw},
                           {"mslr", iqaResult.stats.mslr},
                           {"islr", iqaResult.stats.islr},
                           {"pos", iqaResult.stats.pos},
                           {"maxPower", iqaResult.stats.maxPower}}},
                {"peaks", nlohmann::json::array()}
            };
            for (const auto& peak : iqaResult.peaks) {
                payload["iqa"]["peaks"].push_back({{"index", peak.index}, {"power", peak.power}});
            }
        }
        if (config.useIqa2D) {
            payload["iqa2d"] = {
                {"x", {{"stats", {{"irw", iqa2dResult.x.stats.irw},
                                  {"mslr", iqa2dResult.x.stats.mslr},
                                  {"islr", iqa2dResult.x.stats.islr},
                                  {"pos", iqa2dResult.x.stats.pos},
                                  {"maxPower", iqa2dResult.x.stats.maxPower}}},
                       {"peaks", nlohmann::json::array()}}},
                {"y", {{"stats", {{"irw", iqa2dResult.y.stats.irw},
                                  {"mslr", iqa2dResult.y.stats.mslr},
                                  {"islr", iqa2dResult.y.stats.islr},
                                  {"pos", iqa2dResult.y.stats.pos},
                                  {"maxPower", iqa2dResult.y.stats.maxPower}}},
                       {"peaks", nlohmann::json::array()}}}
            };
            for (const auto& peak : iqa2dResult.x.peaks) {
                payload["iqa2d"]["x"]["peaks"].push_back({{"index", peak.index}, {"power", peak.power}});
            }
            for (const auto& peak : iqa2dResult.y.peaks) {
                payload["iqa2d"]["y"]["peaks"].push_back({{"index", peak.index}, {"power", peak.power}});
            }
        }
        payload["histogram"] = {
            {"min", hist.minValue},
            {"max", hist.maxValue},
            {"counts", hist.counts}
        };
        report << payload.dump(2) << '\n';
    } else {
        report << "PTA_REPORT\n";
        report << "inputConfig=" << path << '\n';
        report << "reportFormat=" << normalizedFormat << '\n';
        report << "status=" << status << '\n';
        report << "maxPeaks=" << config.maxPeaks << '\n';
        report << "minSeparation=" << config.minSeparation << '\n';
        report << "histogramBins=" << config.histogramBins << '\n';
        report << "irw=" << analysis.stats.irw << '\n';
        report << "mslr=" << analysis.stats.mslr << '\n';
        report << "islr=" << analysis.stats.islr << '\n';
        report << "pos=" << analysis.stats.pos << '\n';
        report << "maxPower=" << analysis.stats.maxPower << '\n';
        report << "peakCount=" << analysis.peaks.size() << '\n';
        if (!iqaResult.zoomPower.empty() || !iqaResult.peaks.empty()) {
            report << "iqa.powerDetection=" << config.powerDetection << '\n';
            report << "iqa.sideLobeMethod=" << config.sideLobeMethod << '\n';
            report << "iqa.irw=" << iqaResult.stats.irw << '\n';
            report << "iqa.mslr=" << iqaResult.stats.mslr << '\n';
            report << "iqa.islr=" << iqaResult.stats.islr << '\n';
            report << "iqa.pos=" << iqaResult.stats.pos << '\n';
            report << "iqa.maxPower=" << iqaResult.stats.maxPower << '\n';
            report << "iqa.peakCount=" << iqaResult.peaks.size() << '\n';
        }
        if (config.useIqa2D) {
            report << "iqa2d.x.irw=" << iqa2dResult.x.stats.irw << '\n';
            report << "iqa2d.x.mslr=" << iqa2dResult.x.stats.mslr << '\n';
            report << "iqa2d.x.islr=" << iqa2dResult.x.stats.islr << '\n';
            report << "iqa2d.x.pos=" << iqa2dResult.x.stats.pos << '\n';
            report << "iqa2d.x.maxPower=" << iqa2dResult.x.stats.maxPower << '\n';
            report << "iqa2d.x.peakCount=" << iqa2dResult.x.peaks.size() << '\n';
            report << "iqa2d.y.irw=" << iqa2dResult.y.stats.irw << '\n';
            report << "iqa2d.y.mslr=" << iqa2dResult.y.stats.mslr << '\n';
            report << "iqa2d.y.islr=" << iqa2dResult.y.stats.islr << '\n';
            report << "iqa2d.y.pos=" << iqa2dResult.y.stats.pos << '\n';
            report << "iqa2d.y.maxPower=" << iqa2dResult.y.stats.maxPower << '\n';
            report << "iqa2d.y.peakCount=" << iqa2dResult.y.peaks.size() << '\n';
        }
    }
    return true;
}

}  // namespace pta
