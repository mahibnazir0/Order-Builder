#include "doctest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

// Drives the built order_builder executable, as a user would, so argument parsing,
// report printing and exit codes are tested together. OB_CLI_PATH comes from CMake.

namespace fs = std::filesystem;

namespace {

const std::string kProduct = "tests/importer/Customer2-Product-Data.csv";
const std::string kDemand = "tests/importer/Demand-1.json";
const std::string kPlaceholder = "tests/importer/PlaceHolder-1.json";
const std::string kParams = "config/orderBuilderParams.json";

struct CliRun {
    int exitCode = -1;
    std::string output;   // stdout and stderr together
};

fs::path scratchDirectory() {
    const fs::path directory = fs::temp_directory_path() / "obCliTests";
    fs::create_directories(directory);
    return directory;
}

std::string quoted(const std::string& text) { return "\"" + text + "\""; }

CliRun runCli(const std::vector<std::string>& arguments) {
    const fs::path outputPath = scratchDirectory() / "cliOutput.txt";
    std::string command = quoted(fs::path(OB_CLI_PATH).make_preferred().string());
    for (const auto& argument : arguments) command += " " + quoted(argument);
    command += " > " + quoted(outputPath.string()) + " 2>&1";
#ifdef _WIN32
    // cmd.exe strips the outer quotes of a line that starts with one; give it a pair to strip.
    command = "\"" + command + "\"";
#endif
    CliRun run;
    const int status = std::system(command.c_str());
#ifdef _WIN32
    run.exitCode = status;
#else
    run.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    std::ifstream captured(outputPath, std::ios::binary);
    std::stringstream buffer;
    buffer << captured.rdbuf();
    run.output = buffer.str();
    captured.close();
    fs::remove(outputPath);
    return run;
}

std::vector<std::string> realDayArguments(bool withParams) {
    std::vector<std::string> arguments{"--product", kProduct, "--demand", kDemand,
                                       "--placeholder", kPlaceholder, "--day", "2026-08-17"};
    if (withParams) {
        arguments.push_back("--params");
        arguments.push_back(kParams);
    }
    return arguments;
}

std::vector<std::string> withExtra(std::vector<std::string> arguments,
                                   const std::vector<std::string>& extra) {
    arguments.insert(arguments.end(), extra.begin(), extra.end());
    return arguments;
}

const CliRun& fullRealRun() {
    static const CliRun run = runCli(realDayArguments(true));
    return run;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

bool isRule(const std::string& line) {
    return line.rfind("-----", 0) == 0 || line.rfind("=====", 0) == 0;
}

// The lines of a report section: after its title's closing rule, up to the next rule.
// A truncated table appends "(top N of M ...)" to its title line, so match the start.
std::vector<std::string> section(const std::string& output, const std::string& title) {
    const auto lines = splitLines(output);
    std::vector<std::string> body;
    std::size_t index = 0;
    while (index < lines.size() && lines[index].rfind(" " + title, 0) != 0) ++index;
    while (index < lines.size() && !isRule(lines[index])) ++index;
    for (++index; index < lines.size() && !isRule(lines[index]); ++index) body.push_back(lines[index]);
    return body;
}

// Table rows start in column one; notes and summary lines are indented. Excludes the header.
std::size_t tableRows(const std::vector<std::string>& sectionLines) {
    std::size_t rows = 0;
    for (const auto& line : sectionLines) {
        if (!line.empty() && line[0] != ' ') ++rows;
    }
    return rows == 0 ? 0 : rows - 1;
}

const std::string kM1Summary = "ORDER BUILDER - MILESTONE 1 SUMMARY";
const std::string kM2Summary = "ORDER BUILDER - MILESTONE 2 GROUPS AND STACKS";
const std::string kLaneTable = "PER-LANE SUMMARY";
const std::string kGroupTable = "PER-GROUP SUMMARY";

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// Small valid inputs, so the bad-input cases fail on the one file under test and run fast.
struct SyntheticInputs {
    fs::path directory = scratchDirectory() / "inputs";
    std::string product = (directory / "products.csv").string();
    std::string demand = (directory / "demand.json").string();
    std::string unmatchedDemand = (directory / "unmatchedDemand.json").string();
    std::string placeholder = (directory / "placeholder.json").string();

    SyntheticInputs() {
        fs::create_directories(directory);
        std::ofstream(product) << "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
                                  "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
                               << "GOOD,Good,10,10,10,5,CS,2,4,2,8,TLD\n";
        const std::string lineStart = R"({"LOCFRNO":"1","LOCTONO":"2","MATNR":")";
        const std::string lineEnd = R"(","DATFR_TA":"2026-08-17","SHIP_COND":"TL","TRANS":16.0,"UNITOFMEAS":"CS"})";
        std::ofstream(demand) << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[)"
                              << lineStart << "GOOD" << lineEnd << "]}";
        std::ofstream(unmatchedDemand) << R"({"REQUEST_ID":"t","CTL":[],"DNM":[],"STR":[)"
                                       << lineStart << "GOOD" << lineEnd << ","
                                       << lineStart << "NOT_IN_MASTER" << lineEnd << "]}";
        std::ofstream(placeholder)
            << R"({"PHOLDER":[{"LOCFRNO":"1","LOCTONO":"2","SHIP_COND":"TL","NO_OF_LOADS":1}]})";
    }
    SyntheticInputs(const SyntheticInputs&) = delete;
    SyntheticInputs& operator=(const SyntheticInputs&) = delete;
    ~SyntheticInputs() {
        std::error_code ignored;
        fs::remove_all(directory, ignored);
    }

    std::string write(const std::string& name, const std::string& content) const {
        const std::string path = (directory / name).string();
        std::ofstream(path) << content;
        return path;
    }

    std::vector<std::string> arguments(const std::string& productPath, const std::string& demandPath,
                                       const std::string& placeholderPath,
                                       const std::string& paramsPath) const {
        std::vector<std::string> result{"--product", productPath, "--demand", demandPath,
                                        "--placeholder", placeholderPath};
        if (!paramsPath.empty()) {
            result.push_back("--params");
            result.push_back(paramsPath);
        }
        return result;
    }
};

} // namespace

TEST_CASE("cli: a clean run on the real day exits 0 and prints both milestones") {
    const CliRun& run = fullRealRun();
    CHECK(run.exitCode == 0);
    CHECK(contains(run.output, kM1Summary));
    CHECK(contains(run.output, kM2Summary));
    CHECK(tableRows(section(run.output, kLaneTable)) == 371);
    CHECK(tableRows(section(run.output, kGroupTable)) == 387);
}

TEST_CASE("cli: --groups and --lanes truncate the tables and leave the summaries unchanged") {
    const CliRun& full = fullRealRun();
    const CliRun truncated = runCli(withExtra(realDayArguments(true), {"--groups", "5", "--lanes", "7"}));
    CHECK(truncated.exitCode == 0);
    CHECK(section(truncated.output, kM1Summary) == section(full.output, kM1Summary));
    CHECK(section(truncated.output, kM2Summary) == section(full.output, kM2Summary));
    CHECK(tableRows(section(truncated.output, kLaneTable)) == 7);
    CHECK(tableRows(section(truncated.output, kGroupTable)) == 5);
    CHECK(contains(truncated.output, "(top 7 of 371 by volume)"));
    CHECK(contains(truncated.output, "(top 5 of 387 by floor use)"));
}

TEST_CASE("cli: --trailer 53FT_NA gives exactly the default output") {
    const CliRun named = runCli(withExtra(realDayArguments(true), {"--trailer", "53FT_NA"}));
    CHECK(named.exitCode == 0);
    CHECK(named.output == fullRealRun().output);
}

TEST_CASE("cli: an unknown trailer exits 2 and names the code") {
    const SyntheticInputs inputs;
    const CliRun run = runCli(withExtra(
        inputs.arguments(inputs.product, inputs.demand, inputs.placeholder, kParams),
        {"--trailer", "NOSUCHTRAILER"}));
    CHECK(run.exitCode == 2);
    CHECK(contains(run.output, "[ERROR]"));
    CHECK(contains(run.output, "NOSUCHTRAILER"));
    CHECK_FALSE(contains(run.output, kM1Summary));
}

TEST_CASE("cli: --debug adds only DEBUG lines and changes no figure") {
    const CliRun debug = runCli(withExtra(realDayArguments(true), {"--debug"}));
    CHECK(debug.exitCode == 0);
    std::string withoutDebugLines;
    std::size_t debugLines = 0;
    for (const auto& line : splitLines(debug.output)) {
        if (line.rfind("[DEBUG]", 0) == 0) {
            ++debugLines;
            continue;
        }
        withoutDebugLines += line + "\n";
    }
    std::string fullNormalised;
    for (const auto& line : splitLines(fullRealRun().output)) fullNormalised += line + "\n";
    CHECK(debugLines > 0);
    CHECK(withoutDebugLines == fullNormalised);
}

TEST_CASE("cli: without --params only the Milestone 1 report is printed") {
    const CliRun run = runCli(realDayArguments(false));
    CHECK(run.exitCode == 0);
    CHECK(section(run.output, kM1Summary) == section(fullRealRun().output, kM1Summary));
    CHECK(tableRows(section(run.output, kLaneTable)) == 371);
    CHECK_FALSE(contains(run.output, "MILESTONE 2"));
    CHECK_FALSE(contains(run.output, kGroupTable));
}

TEST_CASE("cli: validation errors exit 1 and the report is still printed") {
    const SyntheticInputs inputs;
    for (const std::string& paramsPath : {std::string{}, kParams}) {
        CAPTURE(paramsPath);
        const CliRun run = runCli(inputs.arguments(inputs.product, inputs.unmatchedDemand,
                                                   inputs.placeholder, paramsPath));
        CHECK(run.exitCode == 1);
        CHECK(contains(run.output, "Validation: 1 errors"));
        CHECK(contains(run.output, kM1Summary));
        CHECK(contains(run.output, kM2Summary) == !paramsPath.empty());
    }
}

TEST_CASE("cli: the synthetic inputs alone run clean, so the bad-input cases isolate one file") {
    const SyntheticInputs inputs;
    const CliRun run = runCli(inputs.arguments(inputs.product, inputs.demand, inputs.placeholder, kParams));
    CHECK(run.exitCode == 0);
    CHECK_FALSE(contains(run.output, "[ERROR]"));
}

TEST_CASE("cli: a pallet taller than the trailer ceiling exits 1 and is reported") {
    const SyntheticInputs inputs;
    const std::string tallProduct = inputs.write("tallProducts.csv",
        "ID,Description,Length,Width,Height,Strength,UoM,Weight,"
        "Cases_Layer,Layers_Unit_Load,Cases_Unit_Load,Pallet_ID\n"
        "GOOD,Tall,10,10,120,5,CS,2,1,1,1,PTL\n");
    const CliRun run = runCli(inputs.arguments(tallProduct, inputs.demand, inputs.placeholder, kParams));
    CHECK(run.exitCode == 1);
    CHECK(contains(run.output, "Over-height lines         1"));
}

TEST_CASE("cli: a missing, directory, empty or malformed input exits 2 naming the problem") {
    const SyntheticInputs inputs;
    const std::string missing = (inputs.directory / "doesNotExist.json").string();
    const std::string directory = inputs.directory.string();
    const std::string emptyFile = inputs.write("empty.json", "");
    const std::string emptyCsv = inputs.write("empty.csv", "");
    const std::string malformedJson = inputs.write("malformed.json", R"({"STR":[{"LOCFRNO":)");
    const std::string malformedCsv = inputs.write("malformed.csv", "not,a,product,header\n1,2,3,4\n");

    struct BadInput {
        std::string description;
        std::string role;   // which argument receives the bad path
        std::string path;
        std::string expectedMessage;
    };
    const std::vector<BadInput> cases{
        {"missing product", "product", missing, "Cannot open product file"},
        {"directory as product", "product", directory, "Cannot open product file"},
        {"empty product", "product", emptyCsv, "Product file is empty"},
        {"malformed product header", "product", malformedCsv, "roduct"},
        {"missing demand", "demand", missing, "Cannot open demand file"},
        {"directory as demand", "demand", directory, "Cannot open demand file"},
        {"empty demand", "demand", emptyFile, "Demand file is not valid JSON"},
        {"malformed demand", "demand", malformedJson, "Demand file is not valid JSON"},
        {"missing placeholder", "placeholder", missing, "Cannot open placeholder file"},
        {"directory as placeholder", "placeholder", directory, "Cannot open placeholder file"},
        {"empty placeholder", "placeholder", emptyFile, "Placeholder file is not valid JSON"},
        {"malformed placeholder", "placeholder", malformedJson, "Placeholder file is not valid JSON"},
        {"missing params", "params", missing, "params: cannot open file"},
        {"directory as params", "params", directory, "params: cannot open file"},
        {"empty params", "params", emptyFile, "params: invalid JSON"},
        {"malformed params", "params", malformedJson, "params: invalid JSON"},
    };
    for (const auto& bad : cases) {
        CAPTURE(bad.description);
        const CliRun run = runCli(inputs.arguments(
            bad.role == "product" ? bad.path : inputs.product,
            bad.role == "demand" ? bad.path : inputs.demand,
            bad.role == "placeholder" ? bad.path : inputs.placeholder,
            bad.role == "params" ? bad.path : kParams));
        CAPTURE(run.output);
        CHECK(run.exitCode == 2);
        CHECK(contains(run.output, "[ERROR]"));
        CHECK(contains(run.output, bad.expectedMessage));
        CHECK_FALSE(contains(run.output, kM1Summary));
    }
}

TEST_CASE("cli: argument mistakes exit 2 with a message") {
    struct BadArguments {
        std::vector<std::string> arguments;
        std::string expectedMessage;
    };
    const std::vector<BadArguments> cases{
        {{}, "--product, --demand and --placeholder are all required"},
        {{"--bogus"}, "unrecognised option '--bogus'"},
        {{"--product"}, "--product needs a value"},
        {{"--groups", "abc"}, "--groups needs a number, got 'abc'"},
        {{"--lanes", "abc"}, "--lanes needs a number, got 'abc'"},
    };
    for (const auto& bad : cases) {
        CAPTURE(bad.expectedMessage);
        const CliRun run = runCli(bad.arguments);
        CHECK(run.exitCode == 2);
        CHECK(contains(run.output, bad.expectedMessage));
    }
}

TEST_CASE("cli: --help exits 0 and documents every option and exit code") {
    const CliRun run = runCli({"--help"});
    CHECK(run.exitCode == 0);
    for (const char* documented : {"--product", "--demand", "--placeholder", "--params",
                                          "--trailer", "--groups", "--day", "--lanes", "--debug",
                                          "0  success", "1  success, but validation found errors, or a pallet exceeds the trailer ceiling",
                                          "2  could not run"}) {
        CAPTURE(documented);
        CHECK(contains(run.output, documented));
    }
}
