#pragma once
// ============================================================================
// m5Commit.hpp - selection is not commitment (CL-10, ED-10).
//
// A delta is built only from a selected candidate whose validation matches its
// revision, profile, source snapshot and reservation version. Applying it is
// one atomic step per lane-date: check both versions, consume the goods and
// slots, record the operation. The same operation with the same payload is a
// no-op (AlreadyApplied); the same operation with a different payload is a
// conflict; a delta from a superseded snapshot is stale even under a new
// operation key, so committed cargo is never consumed twice (X-STATE-10).
// Retries after a commit plan from residualAfter, a new snapshot.
// ============================================================================

#include "m5Ledger.hpp"
#include "m5Policy.hpp"

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ob {
namespace m5 {

struct CommitDelta {
    std::string operationKey;                // stable: lane, departure and source snapshot
    SnapshotId expectedSnapshot;
    ReservationVersion expectedReservationVersion = 0;
    std::string payloadHash;                 // the selected trucks and leftovers, canonical
    std::vector<TruckPlan> trucks;
    std::vector<LeftOver> leftOver;
    std::size_t reservationsUsed = 0;
};

// Empty, with `error` set, when the plan's validation is missing, failed or stale.
std::optional<CommitDelta> buildDelta(const CandidatePlan& selected, const DemandSnapshot& snapshot,
                                      const M5PolicyProfile& profile, std::string* error);

// What one lane-date's goods and pool look like operationally.
struct OperationalState {
    SnapshotId snapshot;
    ReservationVersion reservationVersion = 0;
    std::set<std::string> consumedAtoms;  // atom ids on committed trucks
    std::size_t slotsUsed = 0;
    std::size_t slotsReserved = 0;
};

enum class ApplyStatus { Applied, AlreadyApplied, StaleSnapshot, PayloadConflict };

class CommitLedger {
public:
    ApplyStatus applyIfCurrent(const CommitDelta& delta, const DemandSnapshot& snapshot, OperationalState& state);

private:
    std::mutex mutex_;
    std::map<std::string, std::string> payloadByOperation_;
};

// The original snapshot less the committed trucks' atoms, as a new snapshot with its own identity
// and the pool reduced by the slots used: what a retry after a commit plans from.
DemandSnapshot residualAfter(const DemandSnapshot& original, const CommitDelta& delta);

const char* applyStatusName(ApplyStatus status);

} // namespace m5
} // namespace ob
