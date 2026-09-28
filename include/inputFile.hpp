#pragma once

#include <filesystem>
#include <string>

namespace ob {

// std::ifstream opens a directory successfully on macOS and Linux (reads then come back
// empty) but fails on Windows. Checking for a regular file first gives every loader the
// same "cannot open" error on every platform instead of a misleading "empty file" one.
inline bool isRegularFile(const std::string& path) {
    std::error_code ignored;
    return std::filesystem::is_regular_file(path, ignored);
}

} // namespace ob
