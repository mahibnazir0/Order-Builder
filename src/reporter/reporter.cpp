#include "reporter/reporter.hpp"

#include <stdexcept>

namespace ob {

// TODO(mahib): implement.
std::vector<LaneSummary> Reporter::summarize(const std::vector<JoinResult>& /*results*/) {
    throw std::logic_error("Reporter::summarize not implemented");
}

// TODO(mahib): implement.
void Reporter::write_report(const std::vector<LaneSummary>& /*summaries*/,
                             const std::string& /*output_path*/) {
    throw std::logic_error("Reporter::write_report not implemented");
}

}  // namespace ob
