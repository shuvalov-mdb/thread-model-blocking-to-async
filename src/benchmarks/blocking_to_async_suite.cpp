#include <chrono>
#include <iostream>
#include <ostream>

#include "benchmarks/blocking_to_async_suite.h"

namespace blocking_to_async {
namespace testing {

OptimalConcurrency Calibration::calibrate(
    const Config& config,
    MultithreadedWorkload *mtWorkload) {
    mtWorkload->scaleNonBlockingWorkloadTo(0);
    std::cerr << "Start calibration" << std::endl;
    mtWorkload->scaleNonBlockingWorkloadTo(1);

    double previousCycleQPS = 0;
    int iterationsWithoutChange = 0;
    double qpsBefore = 0.0;
    while (true) {
        mtWorkload->resetStats();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        auto statsAfter = mtWorkload->getStats();

        auto qpsAfter = statsAfter.qps();
        auto ratio = (qpsAfter - qpsBefore) / qpsAfter;
        if (std::abs(ratio) > 0.05) {
            iterationsWithoutChange = 0;
            qpsBefore = qpsAfter;
            continue;
        }
        if (++iterationsWithoutChange < 3) {
            continue;
        }

        if (qpsAfter > previousCycleQPS * 1.001 || mtWorkload->threadCount() <= 20) {
            std::cerr << "Incrementing thread count from " << mtWorkload->threadCount()
                << " with current " << statsAfter << std::endl;
            mtWorkload->scaleNonBlockingWorkloadTo(mtWorkload->threadCount() + 1);
            previousCycleQPS = qpsAfter;
            iterationsWithoutChange = 0;
            continue;
        }

        std::cerr << "Done calibrating with thread count " << (mtWorkload->threadCount() - 1)
            << " and QPS " << previousCycleQPS << " after new cycle QPS stagnated at "
            << previousCycleQPS << std::endl;

        // Stop all the jobs.
        mtWorkload->scaleNonBlockingWorkloadTo(0);

        // Result is the previous iteration.
        return { mtWorkload->threadCount() - 1, previousCycleQPS };
    }
}

}  // namespace testing
}  // namespace blocking_to_async
