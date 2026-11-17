#pragma once
// ============================================================================
// m5Report.hpp - the M5 report for one lane-date, in the preview's 3A-3E
// layout with the v7 0.7 content: run header and settings (3A), the lane and
// its trucks with fullness labels and buckets (3B), method evidence kept per
// method (3C), the threshold benchmark section (3D: production runs hold no
// benchmark rows) and the summary (3E).
//
// Production totals come from the selected plan only, never from A and B
// added together (INV-22). Bucket averages weight each truck once (X-REPORT-
// 10): a truck FULL for two reasons is one FULL truck. Averages with nothing
// to average are N/A, never a division by zero (X-RUN-12). Plain ASCII only.
// ============================================================================

#include "m5Orchestrator.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

struct M5ReportContext {
    std::string scope = "PRODUCTION";  // or SYNTHETIC
    int routingThreshold = 0;          // T: reserved trucks above it go to M5
    std::string runIdentity;           // the extract or fixture planned
};

struct TruckRow {
    std::size_t truck = 0;
    std::string group;
    std::size_t stacks = 0;
    double unitLoads = 0.0;
    double canGoUnitLoads = 0.0;
    double weightLb = 0.0;
    double weightPercent = 0.0;
    double rearInches = 0.0;
    std::string fullness;
    std::string reasons;
    std::string fit;
    std::string bucket;
};

struct BucketAverage {
    std::size_t trucks = 0;
    std::optional<double> averageRearInches;  // N/A for an empty bucket
};

struct M5ReportData {
    std::string lane;
    std::string departure;
    std::string scope;
    std::string runIdentity;
    std::string profileKey;
    std::string sourceVersion;
    std::string minimumLoad;
    std::string fullnessRule;
    std::string routing;
    std::string pastDueOrder;
    std::string canGoWindow;
    std::string methodPolicy;
    double secondsPerLoad = 0.0;
    double allowanceSeconds = 0.0;
    long long reservedTrucks = 0;
    std::vector<std::string> groups;
    std::vector<TruckRow> trucks;
    std::map<std::string, double> unitLoadsLeftByCause;
    long long unusedReservations = 0;
    std::string penalty;
    std::optional<double> penaltyAmount;  // unused x rate; empty while the rate is unknown (CL-06)
    std::vector<MethodRecord> methods;
    EscalationRecord escalation;
    std::string proof;
    std::string proofScope;
    std::string countEvidence;
    std::string comparator;
    FloorSearchRecord floorSearch;
    std::string outcome;
    std::string stop;
    double elapsedSeconds = 0.0;
    std::map<std::string, double> secondsByPhase;
    std::size_t loadedTrucks = 0;
    std::optional<double> secondsPerLoadedTruck;
    double unitLoadsIn = 0.0;
    double unitLoadsShipped = 0.0;
    double canGoUnitLoadsShipped = 0.0;
    std::map<std::string, BucketAverage> rearInchesByBucket;
    std::size_t fullTrucks = 0;
    std::size_t fullByWeight = 0;
    std::size_t fullByRear = 0;
    std::size_t fullByFloor = 0;
    std::vector<std::string> findings;
    std::vector<std::string> openItems;
};

// `profile` is null when the M5 settings could not be read.
M5ReportData buildReportData(const M5LaneOutcome& outcome, const M5LaneDateInput& input, const M5PolicyProfile* profile,
                             const M5ReportContext& context);

// Truck-weighted average rear inches per bucket; every bucket listed, N/A when empty.
std::map<std::string, BucketAverage> rearInchesByBucket(const std::vector<FullnessLabel>& labels);

std::string renderAscii(const M5ReportData& data);

// X-E2E-08: a comparison of unused reservations holds only when unused = reserved - built on one
// basis. Empty when it holds; otherwise names the figure it should be.
std::optional<std::string> unusedComparisonMismatch(long long reserved, long long built, long long statedUnused);

} // namespace m5
} // namespace ob
