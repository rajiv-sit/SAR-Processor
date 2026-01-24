#include "pta/TtlRunner.hpp"

#include <fstream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace pta {

namespace {

std::unordered_map<std::string, std::string> readKeyValueConfig(std::ifstream& input) {
    std::unordered_map<std::string, std::string> config;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '%') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (!key.empty()) {
            config[key] = value;
        }
    }
    return config;
}

}  // namespace

bool TtlRunner::runFromConfig(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }

    std::string reportPath = "pta_report.txt";
    std::string format = "json";
    bool useKeyValue = (path.size() >= 4 && path.substr(path.size() - 4) == ".prs");
    if (!useKeyValue) {
        nlohmann::json config;
        try {
            input >> config;
            reportPath = config.value("reportPath", reportPath);
            format = config.value("reportFormat", format);
        } catch (const nlohmann::json::parse_error&) {
            useKeyValue = true;
        }
    }
    if (useKeyValue) {
        input.clear();
        input.seekg(0);
        const auto kv = readKeyValueConfig(input);
        const auto reportIt = kv.find("reportPath");
        if (reportIt != kv.end()) {
            reportPath = reportIt->second;
        }
        const auto formatIt = kv.find("reportFormat");
        if (formatIt != kv.end()) {
            format = formatIt->second;
        }
    }

    std::ofstream report(reportPath);
    if (!report) {
        return false;
    }

    const std::string normalizedFormat =
        (format == "json" || format == "text") ? format : "text";

    if (normalizedFormat == "json") {
        nlohmann::json payload;
        payload["reportFormat"] = normalizedFormat;
        payload["inputConfig"] = path;
        payload["status"] = "stub";
        report << payload.dump(2) << '\n';
    } else {
        report << "PTA_REPORT\n";
        report << "inputConfig=" << path << '\n';
        report << "reportFormat=" << normalizedFormat << '\n';
        report << "status=stub\n";
    }
    return true;
}

}  // namespace pta
