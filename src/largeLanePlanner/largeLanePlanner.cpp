#include "largeLanePlanner.hpp"
#include "pipeline.hpp"

using namespace std;

namespace ob {

map<LaneDateKey, vector<size_t>> routesByLaneDate(const PipelineResult& run, const LaneClassification& classification) {
    map<LaneDateKey, vector<size_t>> routes;
    for (size_t routeIndex = 0; routeIndex < classification.routes.size(); ++routeIndex) {
        const LaneRoute& route = classification.routes[routeIndex];
        const GroupKey& key = run.segregation.groups[route.groupIndex].key;
        routes[{key.locationFrom, key.locationTo, key.shipCondition, route.shipDate}].push_back(routeIndex);
    }
    return routes;
}

LargeLaneRequest largeLaneRequestOf(const PipelineResult& run, const LaneClassification& classification,
                                    const vector<size_t>& routeIndices) {
    const LaneRoute& first = classification.routes[routeIndices.front()];
    const GroupKey& lane = run.segregation.groups[first.groupIndex].key;
    LargeLaneRequest request;
    request.origin = lane.locationFrom;
    request.destination = lane.locationTo;
    request.shipCondition = lane.shipCondition;
    request.shipDate = first.shipDate;
    request.reservedTrucks = first.reservedTrucks;
    request.pastDueOrder = first.pastDueOrder;
    for (const size_t routeIndex : routeIndices) {
        const LaneRoute& route = classification.routes[routeIndex];
        request.routeIndices.push_back(routeIndex);
        request.groupIndices.push_back(route.groupIndex);
        request.itemIndicesByRoute.push_back(route.itemIndices);
        request.methodBeforeEscalation.push_back(route.methodBeforeEscalation);
    }
    return request;
}

vector<LargeLaneRequest> collectEscalatedLaneDates(const PipelineResult& run, const LaneClassification& classification) {
    vector<LargeLaneRequest> requests;
    for (const auto& [laneDate, routeIndices] : routesByLaneDate(run, classification)) {
        if (classification.routes[routeIndices.front()].method == SolveMethod::M5) {
            requests.push_back(largeLaneRequestOf(run, classification, routeIndices));
        }
    }
    return requests;
}

} // namespace ob
