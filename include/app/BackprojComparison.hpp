#pragma once

#include <iosfwd>

namespace app {

// Returns 0 for a match, 1 for invalid input, and 2 for values outside tolerance.
int runBackprojComparison(int argc, const char* const* argv, std::ostream& output,
                          std::ostream& errors);

}  // namespace app
