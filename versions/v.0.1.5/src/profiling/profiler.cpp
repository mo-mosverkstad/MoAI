#include "profiler.h"
#include "../common/config.h"
#include <fstream>
#include <iostream>

static double get_rss_mb() {
#ifdef __linux__
    std::ifstream f("/proc/self/statm");
    if (!f.is_open()) return 0.0;
    long pages_total, pages_rss;
    f >> pages_total >> pages_rss;
    return pages_rss * 4096.0 / (1024.0 * 1024.0); // pages -> MB
#else
    return 0.0;
#endif
}

Profiler& Profiler::instance() {
    static Profiler p;
    return p;
}

void Profiler::begin_query(const std::string& query) {
    if (!enabled_) return;
    current_ = ProfileRecord{};
    current_.query = query;
}

void Profiler::record(const std::string& component, double ms) {
    if (!enabled_) return;
    current_.timing_ms[component] = ms;
}

void Profiler::record_algorithm(const std::string& slot, const std::string& name) {
    if (!enabled_) return;
    current_.algorithms[slot] = name;
}

void Profiler::record_needs_count(int count) {
    if (!enabled_) return;
    current_.needs_count = count;
}

void Profiler::record_quality(const QualityMetrics& q) {
    if (!enabled_) return;
    current_.quality = q;
}

void Profiler::record_rss_before() {
    if (!enabled_) return;
    current_.rss_before_mb = get_rss_mb();
}

void Profiler::record_rss_after() {
    if (!enabled_) return;
    current_.rss_after_mb = get_rss_mb();
}

void Profiler::end_query() {
    if (!enabled_) return;
    records_.push_back(current_);
    write_record(current_);
}

void Profiler::write_record(const ProfileRecord& rec) {
    auto& cfg = Config::instance();
    std::string path = cfg.get_string("profiling.output_file", "../profiling.jsonl");
    std::ofstream f(path, std::ios::app);
    if (!f.is_open()) return;

    f << "{\"query\":\"";
    for (char c : rec.query) {
        if (c == '"') f << "\\\"";
        else if (c == '\\') f << "\\\\";
        else f << c;
    }
    f << "\",\"algorithms\":{";
    bool first = true;
    for (auto& [k, v] : rec.algorithms) {
        if (!first) f << ",";
        f << "\"" << k << "\":\"" << v << "\"";
        first = false;
    }
    f << "},\"timing_ms\":{";
    first = true;
    for (auto& [k, v] : rec.timing_ms) {
        if (!first) f << ",";
        f << "\"" << k << "\":" << std::fixed;
        f.precision(2);
        f << v;
        first = false;
    }
    f << "},\"needs_count\":" << rec.needs_count
      << ",\"memory_mb\":{\"rss_before\":";
    f.precision(2);
    f << rec.rss_before_mb << ",\"rss_after\":" << rec.rss_after_mb
      << "},\"quality\":{\"confidence\":" << rec.quality.avg_confidence
      << ",\"agreement\":" << rec.quality.avg_agreement
      << ",\"validated\":" << rec.quality.validated_count
      << "/" << rec.quality.total_count
      << ",\"fallback_used\":" << (rec.quality.fallback_used ? "true" : "false")
      << ",\"compression\":\"" << rec.quality.compression << "\"}}\n";
}
