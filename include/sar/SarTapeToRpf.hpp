#pragma once

#include <cstdint>
#include <string>

namespace sar {

bool writeRpfFromSarTape(const std::string& sarTapePath,
                         const std::string& rpfPath,
                         std::uint32_t maxLines,
                         std::string& error);

}  // namespace sar
