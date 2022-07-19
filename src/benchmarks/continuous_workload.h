#include <chrono>
#include <deque>
#include <mutex>

#include "benchmarks/workload.h"

namespace blocking_to_async {
namespace testing {

class ContinuousWorkload : public Workload {
public:
    ~ContinuousWorkload() override = default;

    void init(const Config& config) override;

    void unitOfWork() override;
private:
    // Const after init.
    Config _config;
    std::deque<uint64_t> _data;
};

}  // namespace testing
}  // namespace blocking_to_async
