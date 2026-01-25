#include "rpf/RpfFileUtils.hpp"

#include <algorithm>
#include <filesystem>

namespace rpf {

std::string getBaseFileName(const std::string& fileName) {
    const std::filesystem::path path(fileName);
    const std::string stem = path.stem().string();
    const auto underscore = stem.find_last_of('_');
    if (underscore == std::string::npos) {
        return (path.parent_path() / stem).string();
    }
    const std::string base = stem.substr(0, underscore + 1);
    return (path.parent_path() / base).string();
}

int getFileCounter(const std::string& fileName) {
    const std::filesystem::path path(fileName);
    const std::string stem = path.stem().string();
    const auto underscore = stem.find_last_of('_');
    if (underscore == std::string::npos || underscore + 1 >= stem.size()) {
        return -1;
    }
    const std::string counter = stem.substr(underscore + 1);
    int value = -1;
    try {
        value = std::stoi(counter);
    } catch (...) {
        value = -1;
    }
    return value;
}

std::vector<std::string> findFiles(const std::string& baseFileName) {
    const std::filesystem::path base(baseFileName);
    const auto dir = base.parent_path().empty() ? std::filesystem::current_path() : base.parent_path();
    const std::string prefix = base.filename().string();

    std::vector<std::pair<int, std::string>> matches;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto path = entry.path();
        if (path.extension() != ".rpf") {
            continue;
        }
        const std::string stem = path.stem().string();
        if (stem.rfind(prefix, 0) != 0) {
            continue;
        }
        const int counter = getFileCounter(path.string());
        if (counter < 0) {
            continue;
        }
        matches.emplace_back(counter, path.string());
    }

    std::sort(matches.begin(), matches.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    std::vector<std::string> files;
    int expected = matches.empty() ? 0 : matches.front().first;
    for (const auto& match : matches) {
        if (!files.empty() && match.first != expected) {
            break;
        }
        files.push_back(match.second);
        ++expected;
    }
    return files;
}

}  // namespace rpf
