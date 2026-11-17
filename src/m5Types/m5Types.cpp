#include "m5Types.hpp"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <tuple>

using namespace std;

namespace ob {
namespace m5 {

bool LaneKey::operator<(const LaneKey& other) const {
    return tie(origin, destination, shipCondition) < tie(other.origin, other.destination, other.shipCondition);
}

bool LaneKey::operator==(const LaneKey& other) const {
    return origin == other.origin && destination == other.destination && shipCondition == other.shipCondition;
}

string LaneKey::text() const { return origin + " -> " + destination + " " + shipCondition; }

string TrailerCapability::signature() const {
    // Hex floats print every bit, so two capabilities share a signature only when equal.
    ostringstream out;
    out << hexfloat << lengthIn << 'x' << widthIn << 'x' << heightIn << '/' << maxWeightLb << "/units" << maxUnitLoads;
    return out.str();
}

bool ValidationReport::allPassed() const {
    auto passes = [](SectionStatus status) { return status == SectionStatus::Pass || status == SectionStatus::NotApplicable; };
    return mandatoryChecksComplete && passes(physical) && passes(conservation) && passes(eligibility) && passes(groups)
           && passes(reservations) && passes(acceptance) && passes(metadata);
}

string digestOf(const string& text) {
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    ostringstream out;
    out << hex << setw(16) << setfill('0') << hash;
    return out.str();
}

const char* leftOverCauseName(LeftOverCause cause) {
    switch (cause) {
    case LeftOverCause::NotEligible: return "not eligible on this date";
    case LeftOverCause::Unfittable: return "fits no reserved trailer";
    case LeftOverCause::OverReservation: return "over the reservation";
    case LeftOverCause::AcceptanceWithheld: return "withheld by minimum-load policy";
    case LeftOverCause::NoResult: return "no checked plan";
    case LeftOverCause::ConfigurationIncomplete: return "configuration incomplete";
    case LeftOverCause::InvalidInput: return "input invalid";
    }
    return "unknown";
}

const char* methodName(MethodId method) {
    switch (method) {
    case MethodId::PatternRepeat: return "A (repeated patterns)";
    case MethodId::RollingBlocks: return "B (rolling blocks)";
    case MethodId::Stub: return "stub";
    }
    return "unknown";
}

const char* proofStatusName(ProofStatus status) {
    switch (status) {
    case ProofStatus::NotAttempted: return "not attempted";
    case ProofStatus::Unproven: return "unproven";
    case ProofStatus::ProvenOptimal: return "proven optimal";
    case ProofStatus::ProvenInfeasible: return "proven infeasible";
    }
    return "unknown";
}

const char* terminationName(Termination termination) {
    switch (termination) {
    case Termination::Completed: return "completed";
    case Termination::IterationCap: return "iteration cap";
    case Termination::Budget: return "budget";
    case Termination::Cancelled: return "cancelled";
    case Termination::Error: return "error";
    }
    return "unknown";
}

const char* sectionStatusName(SectionStatus status) {
    switch (status) {
    case SectionStatus::Pass: return "pass";
    case SectionStatus::Fail: return "fail";
    case SectionStatus::NotApplicable: return "n/a";
    case SectionStatus::Incomplete: return "incomplete";
    }
    return "unknown";
}

} // namespace m5
} // namespace ob
