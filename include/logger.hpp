#pragma once

// Minimal thread-safe logging singleton.
//
// Levels: DEBUG < INFO < WARN < ERROR. DEBUG output is suppressed unless
// Logger::instance().set_debug(true) has been called.

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>

namespace ob {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void set_debug(bool enabled) {
        debug_enabled_.store(enabled, std::memory_order_relaxed);
    }

    void log(LogLevel level, const std::string& message) {
        if (level == LogLevel::kDebug
            && !debug_enabled_.load(std::memory_order_relaxed)) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostream& out = (level == LogLevel::kError) ? std::cerr : std::cout;
        out << "[" << level_name(level) << "] " << message << std::endl;
    }

private:
    Logger() = default;

    static const char* level_name(LogLevel level) {
        switch (level) {
            case LogLevel::kDebug:
                return "DEBUG";
            case LogLevel::kInfo:
                return "INFO";
            case LogLevel::kWarn:
                return "WARN";
            case LogLevel::kError:
                return "ERROR";
        }
        return "UNKNOWN";
    }

    std::mutex mutex_;
    std::atomic<bool> debug_enabled_{false};
};

}  // namespace ob

#define LOG_DEBUG(msg) ::ob::Logger::instance().log(::ob::LogLevel::kDebug, (msg))
#define LOG_INFO(msg) ::ob::Logger::instance().log(::ob::LogLevel::kInfo, (msg))
#define LOG_WARN(msg) ::ob::Logger::instance().log(::ob::LogLevel::kWarn, (msg))
#define LOG_ERROR(msg) ::ob::Logger::instance().log(::ob::LogLevel::kError, (msg))
