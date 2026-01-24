#include "app/PipelineRunner.hpp"

#include "backproj/BackProjConfigLoader.hpp"
#include "backproj/BackProjectionEngine.hpp"
#include "pta/PtaAnalyzer.hpp"
#include "rpf/RpfProductStream.hpp"
#include "rto/RtoDataBus.hpp"

namespace app {

bool PipelineRunner::run() {
    backproj::BackProjConfigLoader loader;
    auto operatorConfig = loader.loadOperatorConfig("configs/backproj/operator.json");
    auto secondaryConfig = loader.loadSecondaryConfig("configs/backproj/secondary.json");

    backproj::BackProjectionEngine backprojEngine(operatorConfig, secondaryConfig);
    backprojEngine.run();

    rpf::RpfProductStream stream("data/rpf/sample.rpf");
    rpf::AnnotationStruct annotation{};
    rpf::LatLongGrid grid{};
    stream.nextBlock(annotation, grid, true);

    pta::PtaAnalyzer analyzer;
    pta::PtaChip chip{};
    analyzer.analyze1D(chip);

    rto::RtoDataBus bus("ipc://rto");
    rto::RtoFrame frame{};
    bus.publish(frame);

    return true;
}

}  // namespace app
