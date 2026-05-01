#pragma once
#include "profiler.h"
#include <chrono>
#include <string>

// RAII timer: records elapsed time to Profiler on destruction.
// Zero overhead when profiling is disabled (early return in Profiler::record).
class ScopeTimer {
public:
    explicit ScopeTimer(const std::string& name)
        : name_(name)
        , start_(std::chrono::high_resolution_clock::now())
        , active_(Profiler::instance().enabled())
    {}

    ~ScopeTimer() {
        if (!active_) return;
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start_).count();
        Profiler::instance().record(name_, ms);
    }

    // Non-copyable
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    std::string name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
    bool active_;
};
