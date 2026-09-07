#include "visualizer/ScientificScan.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <utility>

namespace sar::visualizer {
namespace {

constexpr std::uint64_t kMaxScanPixels = 64'000'000;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string text(const nlohmann::json& value) {
    const auto result = value.get<std::string>();
    require(!result.empty() && result.size() <= 4096 && result.find('\0') == std::string::npos,
            "Scan text must contain 1..4096 characters without null bytes");
    return result;
}

std::filesystem::path sibling(const std::filesystem::path& scanPath, const nlohmann::json& value) {
    const auto name = text(value);
    require(name != "." && name != ".." && name.find_first_of("/\\:") == std::string::npos,
            "Stage manifest must be a sibling basename without traversal");
    const auto path = scanPath.parent_path() / name;
    require(std::filesystem::is_regular_file(path), "Stage manifest is missing or not a file");
    require(std::filesystem::canonical(path).parent_path() ==
                std::filesystem::canonical(scanPath).parent_path(),
            "Stage manifest resolves outside the scan directory");
    return path;
}

}  // namespace

ScientificScan loadScientificScan(const std::filesystem::path& manifestPath,
                                  std::uint64_t pixelBudget) {
    try {
        require(pixelBudget > 0 && pixelBudget <= kMaxScanPixels,
                "Scientific scan pixel budget must be in 1..64 million");
        require(std::filesystem::is_regular_file(manifestPath),
                "Scan manifest is missing or not a file");
        require(std::filesystem::file_size(manifestPath) <= 2'000'000,
                "Scientific scan manifest exceeds 2 MB");
        std::ifstream stream(manifestPath);
        require(stream.is_open(), "Cannot open scientific scan manifest");
        const auto metadata = nlohmann::json::parse(stream);
        require(metadata.at("schema") == "sar-scientific-scan-v1",
                "Unsupported scientific scan schema");
        ScientificScan scan;
        scan.label = text(metadata.at("label"));
        scan.source = text(metadata.at("source"));
        scan.defaultStage = text(metadata.at("default_stage"));
        const auto& stages = metadata.at("stages");
        require(stages.is_array() && !stages.empty() && stages.size() <= 16,
                "Scientific scan requires 1..16 stages");
        std::set<std::string> ids;
        bool defaultFound = false;
        for (const auto& record : stages) {
            ScientificScanStage stage;
            stage.id = text(record.at("id"));
            stage.label = text(record.at("label"));
            require(ids.insert(stage.id).second, "Scientific scan stage IDs must be unique");
            const auto status = text(record.at("status"));
            if (status == "available") {
                stage.status = ScientificStageStatus::Available;
                stage.manifestPath = sibling(manifestPath, record.at("manifest"));
            } else {
                require(status == "unavailable", "Unsupported scientific stage status");
                require(!record.contains("manifest"), "Unavailable stage must omit its manifest");
                stage.reason = text(record.at("reason"));
            }
            if (stage.id == scan.defaultStage) {
                require(stage.status == ScientificStageStatus::Available,
                        "Default scientific stage must be available");
                scan.defaultStageIndex = scan.stages.size();
                defaultFound = true;
            }
            scan.stages.push_back(std::move(stage));
        }
        require(defaultFound, "Default scientific stage was not found");
        // Complete structural validation before allocating any pixel payload.
        for (auto& stage : scan.stages) {
            if (stage.status == ScientificStageStatus::Available) {
                stage.frame =
                    loadScientificFrame(stage.manifestPath, pixelBudget - scan.totalPixels);
                scan.totalPixels += stage.frame->pixels.size();
            }
        }
        return scan;
    } catch (const std::exception& error) {
        throw std::runtime_error(manifestPath.filename().string() + ": " + error.what());
    }
}

}  // namespace sar::visualizer
