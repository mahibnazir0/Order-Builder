#include "m5Budget.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

using namespace std;

namespace ob {
namespace m5 {

FunctionClock::FunctionClock(function<double()> now) : now_(std::move(now)) {}

double FunctionClock::nowSeconds() const {
    if (now_) return now_();
    return chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count();
}

LoadTimeSummary summarizeLoadTimes(const vector<double>& secondsPerLoad) {
    LoadTimeSummary summary;
    summary.loads = secondsPerLoad.size();
    for (const double seconds : secondsPerLoad) {
        summary.totalSeconds += seconds;
        summary.maximumSeconds = summary.maximumSeconds ? max(*summary.maximumSeconds, seconds) : seconds;
    }
    if (summary.loads > 0) summary.averageSeconds = summary.totalSeconds / static_cast<double>(summary.loads);
    return summary;
}

Budget::Budget(BudgetDeclaration declaration, const Clock& clock)
    : declaration_(std::move(declaration)), clock_(clock), start_(clock.nowSeconds()) {
    ledger_.allowanceSeconds = declaration_.allowanceSeconds;
    ledger_.contract = declaration_.contract;
}

double Budget::elapsedSeconds() const { return clock_.nowSeconds() - start_; }

bool Budget::exhausted() const {
    if (declaration_.workAllowance) return workUsed_ >= *declaration_.workAllowance;
    return elapsedSeconds() >= declaration_.allowanceSeconds;
}

double Budget::remainingSeconds() const { return max(0.0, declaration_.allowanceSeconds - elapsedSeconds()); }

void Budget::chargeWork(const string& phase, long long units) {
    workUsed_ += units;
    ledger_.workByPhase[phase] += units;
}

void Budget::closePhase() {
    if (phase_.empty()) return;
    ledger_.secondsByPhase[phase_] += clock_.nowSeconds() - phaseStart_;
    phase_.clear();
}

void Budget::startPhase(const string& phase) {
    closePhase();
    phase_ = phase;
    phaseStart_ = clock_.nowSeconds();
}

void Budget::finish() { closePhase(); }

BudgetLedger Budget::ledger() const {
    BudgetLedger ledger = ledger_;
    if (!phase_.empty()) ledger.secondsByPhase[phase_] += clock_.nowSeconds() - phaseStart_;
    ledger.elapsedSeconds = elapsedSeconds();
    ledger.exhausted = exhausted();
    return ledger;
}

double Budget::sliceEnd(double share) const {
    const double now = clock_.nowSeconds();
    const double end = start_ + declaration_.allowanceSeconds;
    return min(end, now + max(0.0, end - now) * share);
}

bool Budget::pastSlice(double sliceEnd) const { return exhausted() || clock_.nowSeconds() >= sliceEnd; }

} // namespace m5
} // namespace ob
