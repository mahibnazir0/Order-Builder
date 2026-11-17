#pragma once
// A folder of the test run's own under the system temp folder, removed when the run ends. Tests that
// write files go here instead of fixed names, so two runs at once never share a file;
// create_directory fails on a name already taken, so each run keeps drawing until it owns a fresh one.

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace testTemp {

class RunTempFolder {
public:
    RunTempFolder() {
        std::random_device draw;
        do {
            path_ = std::filesystem::temp_directory_path()
                    / ("obTests_" + std::to_string(draw()) + "_" + std::to_string(draw()));
        } while (!std::filesystem::create_directory(path_));
    }
    ~RunTempFolder() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    RunTempFolder(const RunTempFolder&) = delete;
    RunTempFolder& operator=(const RunTempFolder&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// The one folder every test of this run shares.
inline const std::filesystem::path& runTempFolder() {
    static const RunTempFolder folder;
    return folder.path();
}

} // namespace testTemp
