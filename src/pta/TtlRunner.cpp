#include "pta/TtlRunner.hpp"

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace pta {

bool TtlRunner::runFromConfig(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }

    nlohmann::json config;
    input >> config;
    const std::string reportPath = config.value("reportPath", "pta_report.txt");

    std::ofstream report(reportPath);
    if (!report) {
        return false;
    }

    report << "PTA report stub\n";
    report << "inputConfig=" << path << '\n';
    return true;
}

}  // namespace pta
