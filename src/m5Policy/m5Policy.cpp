#include "m5Policy.hpp"
#include "paramsFields.hpp"

#include <cmath>
#include <stdexcept>

using namespace std;

namespace ob {
namespace m5 {

using json = nlohmann::json;

namespace {

constexpr long double kCubicInchesPerCubicFoot = 1728.0L;

// A field the block may leave out, read with `read` when present.
template <typename Read>
void readOptional(const json& block, const char* key, Read read) {
    const auto found = block.find(key);
    if (found != block.end()) read(*found, string("m5.") + key);
}

int readWholeCount(const json& value, const string& key) { return readInteger(value, key, 1); }

}  // namespace

string M5PolicyProfile::key() const {
    return profileId + "#" + to_string(profileRevision) + "#" + configHash;
}

AcceptanceDecision evaluateAcceptance(double weightLb, double cubeCubicIn, const NumericAcceptance& rule) {
    AcceptanceDecision decision;
    decision.byWeight = weightLb > rule.weightLb;
    // Cubic inches against y cubic feet without dividing, so 3,000 cu ft is exactly 5,184,000 cu in.
    decision.byCube = static_cast<long double>(cubeCubicIn) > static_cast<long double>(rule.cubeCuFt) * kCubicInchesPerCubicFoot;
    decision.accepted = decision.byWeight || decision.byCube;
    return decision;
}

bool m5Enabled(const json& root) {
    if (!root.is_object()) return false;
    const auto found = root.find("m5");
    if (found == root.end()) return false;
    if (!found->is_object()) throw runtime_error("params: m5 must be an object");
    const auto enabled = found->find("enabled");
    if (enabled == found->end()) throw runtime_error("params: m5.enabled is missing");
    if (!enabled->is_boolean()) throw runtime_error("params: m5.enabled must be true or false");
    return enabled->get<bool>();
}

M5PolicyProfile loadProfile(const json& block) {
    if (!block.is_object()) throw runtime_error("params: m5 must be an object");
    const string name = "m5";
    M5PolicyProfile profile;
    profile.configHash = digestOf(block.dump());
    profile.profileId = readString(requiredField(block, "profileId", name), name + ".profileId");
    if (profile.profileId.empty()) throw runtime_error("params: m5.profileId must not be blank");
    profile.profileRevision =
        static_cast<uint32_t>(readInteger(requiredField(block, "profileRevision", name), name + ".profileRevision", 1));

    const json& routing = requiredField(block, "routing", name);
    const string countMode = readString(requiredField(routing, "countMode", "m5.routing"), "m5.routing.countMode");
    if (countMode != "reservedTrucks") {
        throw runtime_error("params: m5.routing.countMode '" + countMode
                            + "' is not supported: M5 routes on reserved trucks only (reservedTrucks)");
    }
    const string scope = readString(requiredField(routing, "scope", "m5.routing"), "m5.routing.scope");
    if (scope != "perLaneShipDate") {
        throw runtime_error("params: m5.routing.scope '" + scope
                            + "' is not supported: reservations are counted per lane and ship date (perLaneShipDate)");
    }

    profile.secondsPerLoad = readQuantity(requiredField(block, "secondsPerLoad", name), name + ".secondsPerLoad");
    string allowanceError;
    if (!laneAllowanceSeconds(profile.secondsPerLoad, 1, &allowanceError)) {
        throw runtime_error("params: m5.secondsPerLoad " + allowanceError);
    }

    const json& fullness = requiredField(block, "fullness", name);
    if (!fullness.is_object()) throw runtime_error("params: m5.fullness must be an object");
    profile.fullness.weightFullPercent =
        readQuantity(requiredField(fullness, "weightFullPercent", "m5.fullness"), "m5.fullness.weightFullPercent");
    if (profile.fullness.weightFullPercent > 100.0) {
        throw runtime_error("params: m5.fullness.weightFullPercent must be at most 100");
    }
    const auto rear = fullness.find("rearInchLimit");
    if (rear != fullness.end()) {
        profile.fullness.rearInchLimit = readQuantity(*rear, "m5.fullness.rearInchLimit");
        if (profile.fullness.rearInchLimit > kMaxRearInchLimit) {
            throw runtime_error("params: m5.fullness.rearInchLimit must be at most " + to_string(kMaxRearInchLimit));
        }
        profile.fullness.rearInchLimitDefaulted = false;
    }

    const string method = readString(requiredField(block, "methodPolicy", name), name + ".methodPolicy");
    if (method == "SequentialAThenB") {
        profile.method = MethodPolicy::SequentialAThenB;
    } else if (method == "PatternOnly") {
        profile.method = MethodPolicy::PatternOnly;
    } else if (method == "BlocksOnly") {
        profile.method = MethodPolicy::BlocksOnly;
    } else {
        throw runtime_error("params: m5.methodPolicy must be SequentialAThenB, PatternOnly or BlocksOnly");
    }
    profile.methodAShare = readQuantity(requiredField(block, "methodAShare", name), name + ".methodAShare");
    if (profile.methodAShare > 1.0) throw runtime_error("params: m5.methodAShare must be at most 1");
    profile.blockSize = readWholeCount(requiredField(block, "blockSize", name), name + ".blockSize");
    profile.scarcityWindow = readWholeCount(requiredField(block, "scarcityWindow", name), name + ".scarcityWindow");
    profile.gapTrucks = readWholeCount(requiredField(block, "gapTrucks", name), name + ".gapTrucks");

    const json& canGo = requiredField(block, "canGoFill", name);
    if (!canGo.is_boolean()) throw runtime_error("params: m5.canGoFill must be true or false");
    profile.canGoFill = canGo.get<bool>();

    const json& repair = requiredField(block, "repair", name);
    profile.repair.maxRounds = readWholeCount(requiredField(repair, "maxRounds", "m5.repair"), "m5.repair.maxRounds");
    profile.repair.maxMovesPerTruck =
        readWholeCount(requiredField(repair, "maxMovesPerTruck", "m5.repair"), "m5.repair.maxMovesPerTruck");

    // null is a written "unknown"; a number is the rate. A missing key is an error like any other.
    const json& penalty = requiredField(block, "penaltyPerUnusedTruck", name);
    if (!penalty.is_null()) {
        profile.penaltyPerUnusedTruck = readQuantity(penalty, name + ".penaltyPerUnusedTruck", ZeroAllowed::Yes);
    }

    readOptional(block, "acceptance", [&](const json& value, const string& key) {
        if (!value.is_object()) throw runtime_error("params: " + key + " must be an object when present");
        NumericAcceptance rule;
        rule.weightLb = readQuantity(requiredField(value, "weightLb", key), key + ".weightLb", ZeroAllowed::Yes);
        rule.cubeCuFt = readQuantity(requiredField(value, "cubeCuFt", key), key + ".cubeCuFt", ZeroAllowed::Yes);
        profile.acceptance = rule;
    });
    return profile;
}

optional<double> laneAllowanceSeconds(double secondsPerLoad, long long reservedTrucks, string* error) {
    auto fail = [&](const string& why) -> optional<double> {
        if (error) *error = why;
        return nullopt;
    };
    if (!isfinite(secondsPerLoad) || secondsPerLoad <= 0.0) return fail("must be a finite number of seconds above zero");
    if (reservedTrucks < 0) return fail("cannot apply to a negative reservation count");
    const long double allowance = static_cast<long double>(secondsPerLoad) * static_cast<long double>(reservedTrucks);
    if (allowance > kMaxAllowanceSeconds) {
        return fail("times " + to_string(reservedTrucks) + " reserved trucks is past the largest allowance M5 declares");
    }
    return static_cast<double>(allowance);
}

const char* methodPolicyName(MethodPolicy policy) {
    switch (policy) {
    case MethodPolicy::SequentialAThenB: return "SequentialAThenB";
    case MethodPolicy::PatternOnly: return "PatternOnly";
    case MethodPolicy::BlocksOnly: return "BlocksOnly";
    }
    return "unknown";
}

} // namespace m5
} // namespace ob
