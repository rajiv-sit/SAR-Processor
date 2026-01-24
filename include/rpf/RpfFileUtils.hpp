#pragma once

#include <string>
#include <vector>

namespace rpf {

std::string getBaseFileName(const std::string& fileName);
int getFileCounter(const std::string& fileName);
std::vector<std::string> findFiles(const std::string& baseFileName);

}  // namespace rpf
