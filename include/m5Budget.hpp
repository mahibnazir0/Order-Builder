#pragma once
// ============================================================================
// m5Budget.hpp - one lane-date's time allowance (CL-05, ED-14).
//
// The allowance is declared before the search and never changes: in
// production secondsPerLoad x the lane-date's reserved trucks, on a monotonic
// clock; in tests a fixed allowance on a fake clock, or a deterministic work
// count (FX-CLOCK). Methods, blocks, repair, validation and selection all draw
// on it. A slice never adds time: its end is the earlier of the parent's end
// and its own cap, so no method or block receives a fresh allowance
// (X-BLK-07, X-RUN-04, X-RUN-13). Lane-dates never pool allowances (U-PF-08).
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

class Clock {
public:
    virtual ~Clock() = default;
    virtual double nowSeconds() const = 0;  // monotonic
};

// A clock from a function, such as SelectorSettings::clock; empty uses std::chrono::steady_clock.
class FunctionClock final : public Clock {
public:
    explicit FunctionClock(std::function<double()> now);
    double nowSeconds() const override;

private:
    std::function<double()> now_;
};

struct BudgetDeclaration {
    double allowanceSeconds = 0.0;
    std::optional<long long> workAllowance;  // deterministic stop (tests, benchmarks)
    std::string contract;                    // "secondsPerLoad x reserved trucks", "FX-CLOCK", ...
};

// Elapsed per phase. Phases may overlap in wall time only when run in parallel, which M5 never
// does inside a lane-date, so their sum is the lane-date's elapsed time.
struct BudgetLedger {
    std::map<std::string, double> secondsByPhase;
    std::map<std::string, long long> workByPhase;
    double elapsedSeconds = 0.0;
    double allowanceSeconds = 0.0;
    bool exhausted = false;
    std::string contract;
};

// X-RUN-11: per-load times summarised without letting one stand for another. The average is
// total over loads; the maximum is the slowest load. Both are N/A with no loads.
struct LoadTimeSummary {
    double totalSeconds = 0.0;
    std::size_t loads = 0;
    std::optional<double> averageSeconds;
    std::optional<double> maximumSeconds;
};
LoadTimeSummary summarizeLoadTimes(const std::vector<double>& secondsPerLoad);

class Budget {
public:
    Budget(BudgetDeclaration declaration, const Clock& clock);
    Budget(const Budget&) = delete;
    Budget& operator=(const Budget&) = delete;

    bool exhausted() const;
    double remainingSeconds() const;
    double elapsedSeconds() const;
    void chargeWork(const std::string& phase, long long units);
    // Starts a phase; the time until the next startPhase or finish is charged to it.
    void startPhase(const std::string& phase);
    void finish();
    BudgetLedger ledger() const;
    const BudgetDeclaration& declaration() const { return declaration_; }

    // A share of what is left, as a deadline no later than the budget's own.
    double sliceEnd(double share) const;
    bool pastSlice(double sliceEnd) const;

private:
    void closePhase();

    BudgetDeclaration declaration_;
    const Clock& clock_;
    double start_ = 0.0;
    long long workUsed_ = 0;
    std::string phase_;
    double phaseStart_ = 0.0;
    BudgetLedger ledger_;
};

} // namespace m5
} // namespace ob
