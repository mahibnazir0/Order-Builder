#include "pipeline.hpp"
#include "converter.hpp"
#include "floorPlanner.hpp"
#include "importer.hpp"
#include "logger.hpp"
#include "paramsLoader.hpp"
#include "reportFormat.hpp"
#include "segregation.hpp"
#include "stackRules.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

using namespace std;

namespace ob {

namespace {

// Fills each product with no layer size of its own from the row with the same ID and
// Pallet_ID in donors. Throws when donors carry no layer size at all: that file cannot be
// the one meant, and silently borrowing nothing would leave every footprint on the pallet.
size_t borrowLayerSizes(vector<ProductRecord>& products, const vector<ProductRecord>& donors,
                        const string& donorPath) {
    unordered_map<string, const ProductRecord*> donorByKey;
    donorByKey.reserve(donors.size());
    for (const ProductRecord& donor : donors) {
        if (donor.layerLengthIn && donor.layerWidthIn) {
            donorByKey.emplace(donor.id + '\x1f' + donor.pallet_id, &donor);
        }
    }
    if (donorByKey.empty()) {
        throw runtime_error("--layer-sizes " + donorPath
                            + ": no row has both Layer_Length and Layer_Width");
    }
    size_t borrowed = 0;
    for (ProductRecord& product : products) {
        if (product.layerLengthIn || product.layerWidthIn) continue;
        const auto found = donorByKey.find(product.id + '\x1f' + product.pallet_id);
        if (found == donorByKey.end()) continue;
        product.layerLengthIn = found->second->layerLengthIn;
        product.layerWidthIn = found->second->layerWidthIn;
        ++borrowed;
    }
    return borrowed;
}

vector<double> zeroExcluded(const vector<double>& figures, const vector<bool>& excluded) {
    vector<double> kept = figures;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (excluded[i]) kept[i] = 0.0;
    }
    return kept;
}

// M1 weighs a pallet from the item master, then the pallet table (client, 13 Oct); the M1
// totals moved with it, as docs/m1Restatement.md records. M2 must weigh a pallet exactly as
// buildUnitLoad does, from the product row's pallet
// figures or the pallet table, or binding and reporting disagree with the stacks. A line with no buildable
// unit load keeps its M1 weight: it still occupies the trailer, and that is the only
// estimate there is for it.
vector<double> configuredWeightPerLine(const vector<JoinedLine>& lines,
                                            const vector<double>& palletsForStacking,
                                            const vector<double>& m1WeightForStacking,
                                            const M2Params& params) {
    vector<double> weights = m1WeightForStacking;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (palletsForStacking[i] == 0.0) continue;
        const UnitLoad load = buildUnitLoad(lines[i], params);
        if (load.error == UnitLoadError::None) weights[i] = palletsForStacking[i] * load.weightLb;
    }
    return weights;
}

string unitLoadErrorText(UnitLoadError error, const JoinedLine& line) {
    switch (error) {
    case UnitLoadError::MissingProduct: return "product is not in the master";
    case UnitLoadError::MissingPalletSpec:
        if (line.product->pallet_id.empty()) return "the product master gives no Pallet_ID";
        return "neither the product master nor the pallet table gives the weight, height and"
               " footprint of pallet type '" + line.product->pallet_id + "'";
    case UnitLoadError::InvalidData:
        return "product height, weight, layer, case or strength data cannot form a unit load";
    case UnitLoadError::None: break;
    }
    return "unknown";
}

const char* const kOwnCriExceededReason =
    "exceeds its own CRI limit; it ships single-high and carries nothing";

string zeroPalletReason(const JoinedLine& line) {
    if (!Converter::isConvertibleUom(line.str->unitofmeas)) {
        return "unit of measure '" + line.str->unitofmeas
             + "' cannot be converted to pallets (supported: CS, PAL, DIS)";
    }
    return "quantity converts to 0 pallets";
}

// Every joined line must end up in a stack or on this list with a reason; the list is built
// from which lines the stacks actually hold, so a line dropped anywhere upstream (validator,
// segregation, conversion, stacking) cannot pass unreported.
vector<ReportedLine> unstackedLines(const PipelineResult& result, const TrailerSpec& trailer) {
    const auto& lines = result.join.lines;
    vector<string> reasons(lines.size());
    for (const auto& issue : result.validation.issues) {
        if (issue.line_index < 0 || static_cast<size_t>(issue.line_index) >= lines.size()) continue;
        string& reason = reasons[static_cast<size_t>(issue.line_index)];
        if (reason.empty() && Validator::excludesLine(issue)) {
            reason = "rejected by validation (" + issue.rule + "): " + issue.message;
        }
    }
    const StackingResult& stacking = result.stacking;
    for (const auto& excludedLine : stacking.excludedLines) {
        reasons[excludedLine.lineIndex] = unitLoadErrorText(excludedLine.error, lines[excludedLine.lineIndex]);
    }
    for (const size_t lineIndex : stacking.overHeightLines) {
        reasons[lineIndex] = "one pallet is taller than the trailer's "
                           + fixed(trailer.stackHeightCeilingIn, 0) + " in ceiling";
    }
    for (const size_t lineIndex : stacking.invalidQuantityLines) {
        reasons[lineIndex] = "pallet quantity is negative or not a finite number";
    }
    for (const size_t lineIndex : stacking.zeroQuantityLines) {
        reasons[lineIndex] = zeroPalletReason(lines[lineIndex]);
    }

    const vector<bool> stacked = stackedLineFlags(stacking, lines.size());
    vector<ReportedLine> unstacked;
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        if (stacked[lineIndex]) continue;
        string reason = reasons[lineIndex].empty() ? "is in no stack" : std::move(reasons[lineIndex]);
        unstacked.push_back({lineIndex, lines[lineIndex].str->matnr, std::move(reason)});
    }
    return unstacked;
}

void runMilestone2(const PipelineInputs& inputs, PipelineResult& result) {
    if (inputs.trailerCode.empty()) {
        result.trailer = largestTrailer(result.params.trailers, result.params.sourcePath);
        result.trailerChoice = TrailerChoice::Largest;
    } else {
        result.trailer =
            selectTrailer(result.params.trailers, result.params.sourcePath, inputs.trailerCode);
        result.trailerChoice = TrailerChoice::Named;
    }
    const TrailerSpec& trailer = result.trailer;

    result.missingPalletIds = missingPalletIds(result.join.lines, result.params);
    // Masters before 29 Sep carry no Pallet_* columns, so without a table every line using them
    // would go unstacked with one warning each. Stop with one message instead.
    if (inputs.palletPath.empty() && !result.missingPalletIds.empty()) {
        string palletIds;
        for (const auto& palletId : result.missingPalletIds) {
            palletIds += (palletIds.empty() ? "" : ", ") + palletId;
        }
        throw runtime_error("the product master gives no weight, height and footprint for pallet"
                            " type(s) " + palletIds + " and no pallet table was given;"
                            " pass --pallets <csv>");
    }
    for (const auto& palletId : result.missingPalletIds) {
        LOG_WARN("Demand uses pallet type '" + palletId + "' but neither the product master nor"
                 " the pallet table gives its weight, height and footprint");
    }

    const vector<bool> excluded =
        Validator::excludedLineFlags(result.validation, result.join.lines.size());
    result.palletsForStacking = zeroExcluded(result.pallets_per_line, excluded);
    result.weightForStacking = configuredWeightPerLine(
        result.join.lines, result.palletsForStacking,
        zeroExcluded(result.weight_per_line, excluded), result.params);

    result.segregation = segregate(result.join.lines, result.demand.dnm,
                                   result.params.doNotMixReading, excluded);
    result.binding = assessBinding(result.segregation, result.palletsForStacking,
                                   result.weightForStacking, trailer);
    result.stacking = buildStacks(result.segregation, result.join.lines, result.palletsForStacking,
                                  result.binding, result.params, trailer);
    result.stackReport = StackReporter::build(result.segregation, result.binding,
                                              result.stacking, result.params);
    result.stackReport.unstackedLines = unstackedLines(result, trailer);
    // Over-own-CRI lines are found only once unit loads are built, but they are warnings like
    // any other, so they join the validation tally and its WARNINGS section as well.
    const vector<size_t>& ownCriExceededLines = result.stacking.ownCriExceededLines;
    result.stackReport.ownCriExceeded.reserve(ownCriExceededLines.size());
    for (const size_t lineIndex : ownCriExceededLines) {
        const string& matnr = result.join.lines[lineIndex].str->matnr;
        result.stackReport.ownCriExceeded.push_back({lineIndex, matnr, kOwnCriExceededReason});
        ValidationIssue issue;
        issue.rule = "exceeds_own_cri";
        issue.message = string("Unit load ") + kOwnCriExceededReason;
        issue.matnr = matnr;
        issue.line_index = static_cast<int>(lineIndex);
        result.validation.issues.push_back(std::move(issue));
    }
    result.validation.exceedsOwnCri = static_cast<int>(ownCriExceededLines.size());
    result.validation.warnings += result.validation.exceedsOwnCri;
    // One summary line, not one per demand line: a single product can span hundreds of lines.
    if (!ownCriExceededLines.empty()) {
        LOG_WARN(to_string(ownCriExceededLines.size())
                 + " demand line(s) exceed their own CRI limit and ship single-high;"
                   " the stack report lists them");
    }
    for (const auto& line : result.stackReport.unstackedLines) {
        LOG_WARN("Demand line " + to_string(line.lineIndex) + " (MATNR " + line.matnr
                 + ") was not stacked: " + line.reason);
    }
    result.stackReport.ambiguousPalletLines = static_cast<size_t>(result.validation.ambiguous_pallet);
    result.ranMilestone2 = true;
}

// A demand extract read as the placeholder file has no PHOLDER block and loads as zero trucks
// requested, so passing one file to both flags would quietly empty the requested loads.
void requireDistinctDemandAndPlaceholder(const PipelineInputs& inputs) {
    error_code ignored;
    if (filesystem::equivalent(inputs.demand_path, inputs.placeholder_path, ignored)) {
        throw runtime_error("--demand and --placeholder name the same file: "
                            + inputs.placeholder_path);
    }
}

// Non-empty demand of which no line ships on the rule's dates is far more often a mistyped date
// than a day with nothing to ship, so it never reads as a complete run.
bool selectsNothing(const PipelineResult& result) {
    return !result.demand.str.empty() && result.floorRun.selectedLines() == 0;
}

void runFloor(const DemandSelector& selector, PipelineResult& result) {
    result.floorRun = planFloorRun(selector, result.demand, result.segregation, result.join.lines,
        result.placeholders.placeholders,
        Validator::excludedPlaceholderFlags(result.validation,
                                            result.placeholders.placeholders.size()),
        result.params, result.trailer);
    const ShipDateCoverage& coverage = result.floorRun.coverage;
    const string ruleText = describeDemandSelector(selector);
    if (selectsNothing(result)) {
        LOG_WARN("none of the " + to_string(result.demand.str.size()) + " demand line(s) ships"
                 " on the dates of " + ruleText + "; check the dates");
    }
    if (!coverage.undatedLines.empty()) {
        LOG_WARN(to_string(coverage.undatedLines.size())
                 + " demand line(s) have no real ship date (DATFR_TA) and count toward no floor");
    }
    if (result.floorRun.linesLeftOut() > 0) {
        LOG_WARN(to_string(result.floorRun.linesLeftOut())
                 + " demand line(s) shipping on the run's dates were left out of the floor"
                   " (listed in section B)");
    }
    result.ranFloor = true;
}

// The inputs and params copied into one folder with a manifest naming each file's role, so the
// solve can be run again from what was recorded.
void recordInputs(const PipelineInputs& inputs) {
    const filesystem::path directory(inputs.recordDirectory);
    error_code failure;
    filesystem::create_directories(directory, failure);
    if (failure) throw runtime_error("cannot create record folder " + inputs.recordDirectory + ": " + failure.message());
    ofstream manifest(directory / "record.txt");
    const vector<pair<string, string>> files{
        {"product", inputs.product_path}, {"demand", inputs.demand_path},
        {"placeholder", inputs.placeholder_path}, {"params", inputs.paramsPath},
        {"pallets", inputs.palletPath}, {"layerSizes", inputs.layerSizePath}};
    for (const auto& [role, path] : files) {
        if (path.empty()) continue;
        const string copied = role + "-" + filesystem::path(path).filename().string();
        filesystem::copy_file(path, directory / copied, filesystem::copy_options::overwrite_existing, failure);
        if (failure) throw runtime_error("cannot record " + path + ": " + failure.message());
        manifest << role << " " << copied << "\n";
    }
    if (!manifest) throw runtime_error("cannot write " + (directory / "record.txt").string());
}

// A grain coarser than an item would round it into grains it does not fill, so every lane's
// method would throw and fall back. Checked once against the real items instead.
void requireGrainsFitItems(const SelectionParams& params, const SelectionItems& items) {
    double lightest = 0.0, smallest = 0.0;
    for (const SelectionItem& item : items.items) {
        const double area = item.footprintLongIn * item.footprintShortIn;
        if (item.weightLb > 0.0 && (lightest == 0.0 || item.weightLb < lightest)) lightest = item.weightLb;
        if (area > 0.0 && (smallest == 0.0 || area < smallest)) smallest = area;
    }
    const SelectorSettings& selector = params.selector;
    const vector<pair<string, pair<double, double>>> grains{
        {"knapsack", {selector.knapsack.weightGrainLb, selector.knapsack.areaGrainSqIn}},
        {"patternPacker", {selector.patternPacker.weightGrainLb, selector.patternPacker.areaGrainSqIn}},
        {"binPacker", {selector.binPacker.weightGrainLb, selector.binPacker.areaGrainSqIn}}};
    for (const auto& [method, grain] : grains) {
        if (lightest > 0.0 && grain.first > lightest) {
            throw runtime_error("params: selection." + method + ".weightGrainLb is coarser than the lightest stack ("
                                + to_string(lightest) + " lb)");
        }
        if (smallest > 0.0 && grain.second > smallest) {
            throw runtime_error("params: selection." + method + ".areaGrainSqIn is coarser than the smallest stack ("
                                + to_string(smallest) + " sq in)");
        }
    }
}

// Lanes with demand under more than one ship condition on the same two sites. The client's
// specification would top up spare truckload space from them; this customer has none, so they
// are planned apart and named rather than merged.
vector<string> comboLanes(const SegregationResult& segregation) {
    map<pair<string, string>, set<string>> conditions;
    for (const SegregatedGroup& group : segregation.groups) {
        conditions[{group.key.locationFrom, group.key.locationTo}].insert(group.key.shipCondition);
    }
    vector<string> warnings;
    for (const auto& [sites, shipConditions] : conditions) {
        if (shipConditions.size() < 2) continue;
        string listed;
        for (const string& condition : shipConditions) listed += (listed.empty() ? "" : " and ") + condition;
        warnings.push_back("combo lane " + sites.first + " -> " + sites.second + " has demand under " + listed
                           + "; planned apart, not merged");
    }
    return warnings;
}

void runSelection(PipelineResult& result) {
    const string planDate = planDateFromRequestId(result.demand.request_id);
    result.reservationBook = buildReservationBook(result.placeholders.placeholders, result.demand.ctl, planDate,
                                                  result.selectionParams.reservations);
    result.selectionItems = buildSelectionItems(result, result.reservationBook.window);
    requireGrainsFitItems(result.selectionParams, result.selectionItems);
    result.laneClassification = classifyLanes(result, result.reservationBook, result.selectionItems,
                                              result.selectionParams.routing);
    result.selectionPlan = selectLoads(result, result.reservationBook, result.selectionItems,
                                       result.laneClassification, result.selectionParams.selector);
    result.selectionViolations = checkInvariants(result, result.reservationBook, result.selectionItems,
                                                 result.selectionPlan, result.selectionParams.maxUnitLoadsPerTruck);
    result.selectionWarnings = comboLanes(result.segregation);
    for (const InvariantViolation& violation : result.selectionViolations) {
        LOG_ERROR("M4 " + violation.invariant + ": " + violation.detail);
    }
    LOG_INFO("M4: " + to_string(result.selectionPlan.trucks.size()) + " trucks against "
             + to_string(result.reservationBook.reservedTrucks) + " reserved, "
             + to_string(result.selectionPlan.penaltyExposureTrucks) + " unused");
    result.ranSelection = true;
}

} // namespace

RunOutcome runOutcome(const PipelineResult& result) {
    if (result.validation.errors > 0) return RunOutcome::ValidationErrors;
    if (result.ranMilestone2 && !result.stackReport.isComplete()) return RunOutcome::LinesNotStacked;
    if (!result.ranFloor) return RunOutcome::FloorNotComputed;
    if (!result.floorRun.coverage.undatedLines.empty() || result.floorRun.linesLeftOut() > 0) {
        return RunOutcome::FloorIncomplete;
    }
    if (selectsNothing(result)) return RunOutcome::NothingSelected;
    if (result.ranSelection) {
        const auto& routes = result.selectionPlan.routes;
        const bool unsolved = any_of(routes.begin(), routes.end(),
                                     [](const RouteOutcome& route) { return route.status == RouteStatus::Unsolved; });
        if (!result.selectionViolations.empty() || unsolved) return RunOutcome::SelectionIncomplete;
    }
    return RunOutcome::Complete;
}

PipelineResult Pipeline::run(const PipelineInputs& inputs) {
    if (inputs.demandSelector && inputs.paramsPath.empty()) {
        throw runtime_error("a demand rule needs a params file: the floor is planned against "
                            "its trailer");
    }
    if (inputs.runSelection && inputs.paramsPath.empty()) {
        throw runtime_error("selection needs a params file: its settings are the params file's 'selection' block");
    }
    requireDistinctDemandAndPlaceholder(inputs);
    if (!inputs.recordDirectory.empty()) recordInputs(inputs);
    PipelineResult result;

    // ── 1. Read the three input files ───────────────────────────────────────
    LOG_DEBUG("Reading product master: " + inputs.product_path);
    result.products = ProductImporter::load(inputs.product_path);

    if (!inputs.layerSizePath.empty()) {
        result.layerSizesBorrowed = borrowLayerSizes(result.products.products,
            ProductImporter::load(inputs.layerSizePath).products, inputs.layerSizePath);
        LOG_INFO("Borrowed layer sizes for " + to_string(result.layerSizesBorrowed)
                 + " product rows from " + inputs.layerSizePath);
    }

    LOG_DEBUG("Reading demand: " + inputs.demand_path);
    result.demand = Importer::load_demand(inputs.demand_path);

    LOG_DEBUG("Reading placeholders: " + inputs.placeholder_path);
    result.placeholders = PlaceholderImporter::load(inputs.placeholder_path);

    // Read up front so a bad params file or pallet table stops the run before any work.
    if (!inputs.paramsPath.empty()) {
        result.params = loadParams(inputs.paramsPath);
        if (!inputs.palletPath.empty()) {
            result.params.pallets = ProductImporter::loadPalletTable(inputs.palletPath);
        }
        if (inputs.runSelection) result.selectionParams = loadSelectionParams(inputs.paramsPath);
    }

    // ── 2. Join demand to the product master ────────────────────────────────
    result.index = Joiner::build_index(result.products.products);
    result.join  = Joiner::join(result.demand.str, result.index);

    // ── 3. Convert quantities to pallets and weight ─────────────────────────
    const vector<PalletSpec>& m1Pallets =
        result.params.pallets.empty() ? confirmedPalletSpecs() : result.params.pallets;
    result.pallets_per_line.reserve(result.join.lines.size());
    result.weight_per_line.reserve(result.join.lines.size());
    for (const auto& jl : result.join.lines) {
        // Unmatched lines still need an entry: the arrays must stay exactly
        // parallel to join.lines, which the Validator and Reporter rely on.
        if (!jl.matched || jl.product == nullptr || jl.str == nullptr) {
            result.pallets_per_line.push_back(0.0);
            result.weight_per_line.push_back(0.0);
            continue;
        }

        const double pallets = Converter::to_pallets(jl.str->trans,
                                                     jl.str->unitofmeas,
                                                     *jl.product);
        result.pallets_per_line.push_back(pallets);
        result.weight_per_line.push_back(
            Converter::to_weight_lb(pallets, *jl.product, m1Pallets));
    }

    // ── 4. Validate ─────────────────────────────────────────────────────────
    ValidationConfig validationConfig = inputs.validation;
    validationConfig.productMasterPath = inputs.product_path;
    validationConfig.pallets = m1Pallets;
    result.validation = Validator::validate(result.join,
                                            result.pallets_per_line,
                                            validationConfig);
    Validator::validate_placeholders(result.placeholders.placeholders, result.validation,
                                     validationConfig);
    Validator::validate_do_not_mix(result.demand.dnm, result.demand.str, result.validation);

    // ── 5. Summarise ────────────────────────────────────────────────────────
    result.summary = Reporter::build(result.join,
                                     result.placeholders.placeholders,
                                     result.pallets_per_line,
                                     result.weight_per_line,
                                     inputs.planning_day,
                                     result.validation);

    // ── 6. Milestone 2: segregate, pass 1, pass 2, stack report ─────────────
    if (!inputs.paramsPath.empty()) runMilestone2(inputs, result);

    // ── 7. Milestone 3: a floor per ship date ───────────────────────────────
    if (inputs.demandSelector) runFloor(*inputs.demandSelector, result);

    // ── 8. M4: select each reserved truck's load ────────────────────────────
    if (inputs.runSelection) runSelection(result);

    // Logged only now so the count includes the warnings Milestone 2 adds and matches the report.
    LOG_INFO("Validation: " + to_string(result.validation.errors) + " errors, "
             + to_string(result.validation.warnings) + " warnings across "
             + to_string(result.join.lines.size()) + " demand lines");

    return result;
}

} // namespace ob
