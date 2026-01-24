#include <iostream>
#include <string>

#include "sar/SarTapeIngestPipeline.hpp"

namespace {

void printUsage() {
    std::cout << "Usage: sartape_ingest_cli <input> <output_prefix> [--complex]\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    const std::string inputPath = argv[1];
    const std::string outputPrefix = argv[2];
    const bool outputComplex = (argc > 3 && std::string(argv[3]) == "--complex");

    sar::IngestOptions options{};
    options.errorPolicy = sar::ErrorPolicy::kFatal;
    options.outputComplexIq = outputComplex;

    sar::SarTapeIngestPipeline pipeline(inputPath, outputPrefix, options);
    const auto lines = pipeline.run();
    if (lines == 0) {
        std::cerr << "SarTape ingest produced no data.\n";
        return 1;
    }

    std::cout << "SarTape ingest wrote " << lines << " lines to " << outputPrefix << ".*\n";
    return 0;
}
