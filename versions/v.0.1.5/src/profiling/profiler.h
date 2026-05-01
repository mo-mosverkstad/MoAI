#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>

struct QualityMetrics {
    double avg_confidence = 0.0;
    double avg_agreement = 0.0;
    int validated_count = 0;
    int total_count = 0;
    bool fallback_used = false;
    std::string compression = "NONE";
};

struct ProfileRecord {
    std::string query;
    std::unordered_map<std::string, double> timing_ms;
    std::unordered_map<std::string, std::string> algorithms;
    int needs_count = 0;
    double rss_before_mb = 0.0;
    double rss_after_mb = 0.0;
    QualityMetrics quality;
};

class Profiler {
public:
    static Profiler& instance();

    // Enable/disable (read from config at startup)
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    // Start a new query profile
    void begin_query(const std::string& query);

    // Record a timing measurement for the current query
    void record(const std::string& component, double ms);

    // Record algorithm identity
    void record_algorithm(const std::string& slot, const std::string& name);

    // Record needs count
    void record_needs_count(int count);

    // Finalize current query and store the record
    void end_query();

    // Record quality metrics
    void record_quality(const QualityMetrics& q);

    // Record memory RSS (call before and after pipeline run)
    void record_rss_before();
    void record_rss_after();

    // Get all collected records
    const std::vector<ProfileRecord>& records() const { return records_; }

    // Get the current (in-progress) record
    ProfileRecord& current() { return current_; }

private:
    Profiler() = default;
    void write_record(const ProfileRecord& rec);

    bool enabled_ = false;
    ProfileRecord current_;
    std::vector<ProfileRecord> records_;
};
