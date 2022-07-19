#include <chrono>
#include <iostream>
#include <ostream>

#include "benchmarks/blocking_to_async_suite.h"

namespace blocking_to_async {
namespace testing {

OptimalConcurrency Calibration::calibrate(
    const Config& config,
    MultithreadedWorkload *mtWorkload) {
    mtWorkload->scaleTo(0);
    std::cerr << "Start calibration" << std::endl;
    mtWorkload->scaleTo(1);

    double previousCycleQPS = 0;
    int iterationsWithoutChange = 0;
    while (true) {
        auto statsBefore = mtWorkload->getStats();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto statsAfter = mtWorkload->getStats();

        if (statsBefore.iterations == 0) {
            continue;
        }

        auto qpsBefore = statsBefore.iterations * 1000.0 * 1000 / statsBefore.duration.count();
        auto qpsAfter = statsAfter.iterations * 1000.0 * 1000 / statsAfter.duration.count();
        auto ratio = (qpsBefore - qpsAfter) / qpsAfter;
        if (std::abs(ratio) > 0.05) {
            iterationsWithoutChange = 0;
            continue;
        }
        if (++iterationsWithoutChange < 10) {
            continue;
        }

        if (qpsAfter > previousCycleQPS * 1.001) {
            std::cerr << "Incrementing thread count to " << (mtWorkload->threadCount() + 1)
                << " with current QPS " << qpsAfter << std::endl;
            mtWorkload->scaleTo(mtWorkload->threadCount() + 1);
            previousCycleQPS = qpsAfter;
            iterationsWithoutChange = 0;
            continue;
        }

        std::cerr << "Done calibrating with thread count " << (mtWorkload->threadCount() - 1)
            << " and QPS " << previousCycleQPS << " after new cycle QPS stagnated at "
            << previousCycleQPS << std::endl;

        // Result is the previous iteration.
        return { mtWorkload->threadCount() - 1, previousCycleQPS };
    }
}

}  // namespace testing
}  // namespace blocking_to_async
