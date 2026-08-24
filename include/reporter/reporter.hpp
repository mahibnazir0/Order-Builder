#pragma once

// Reporter: turns JoinResult records into per-lane summaries and writes
// the final report output.
//
// Milestone 1: declarations only.

#include <string>
#include <vector>

#include "joiner.hpp"
#include "types.hpp"

namespace ob {

class Reporter {
public:
    // TODO(mahib): summarize join results into per-lane summaries.
    static std::vector<LaneSummary> summarize(const std::vector<JoinResult>& results);

    // TODO(mahib): write lane summaries to the given output path.
    static void write_report(const std::vector<LaneSummary>& summaries,
                              const std::string& output_path);
};

}  // namespace ob
