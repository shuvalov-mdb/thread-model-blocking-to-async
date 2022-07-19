#include <chrono>
#include <functional>

#include "benchmarks/continuous_workload.h"

namespace blocking_to_async {
namespace testing {

class Calibration {
public:
    // Returns the optimal thread count.
    OptimalConcurrency calibrate(
        const Config& config,
        MultithreadedWorkload *mtWorkload);

private:
};


}  // namespace testing
}  // namespace blocking_to_async

