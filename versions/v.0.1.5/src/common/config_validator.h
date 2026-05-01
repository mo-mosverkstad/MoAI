#pragma once
#include "../common/config.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <iostream>

class ConfigValidator {
public:
    // Validate all algorithm config values. Returns true if valid.
    // Prints clear error messages to stderr for each problem found.
    static bool validate() {
        auto& cfg = Config::instance();
        if (!cfg.loaded()) return true; // no config file = use defaults, always valid

        bool ok = true;

        ok &= check_option(cfg, "retrieval.retriever", {"bm25", "hnsw", "hybrid"}, "hybrid");
        ok &= check_option(cfg, "query.analyzer", {"rule", "neural", "auto"}, "auto");
        ok &= check_option(cfg, "embedding.method", {"bow", "transformer", "auto"}, "auto");

        ok &= check_positive(cfg, "bm25.k1");
        ok &= check_positive(cfg, "bm25.b");
        ok &= check_positive(cfg, "bm25.top_k");
        ok &= check_positive(cfg, "retrieval.max_ranked_docs");
        ok &= check_positive(cfg, "retrieval.max_evidence");
        ok &= check_positive(cfg, "chunk.max_per_doc");

        ok &= check_range(cfg, "retrieval.bm25_weight", 0.0, 1.0);
        ok &= check_range(cfg, "retrieval.ann_weight", 0.0, 1.0);
        ok &= check_range(cfg, "compression.min_confidence", 0.0, 1.0);
        ok &= check_range(cfg, "compression.min_agreement", 0.0, 1.0);
        ok &= check_range(cfg, "scope.high_confidence_threshold", 0.0, 1.0);
        ok &= check_range(cfg, "scope.low_confidence_threshold", 0.0, 1.0);

        ok &= check_range(cfg, "confidence.coverage_weight", 0.0, 1.0);
        ok &= check_range(cfg, "confidence.volume_weight", 0.0, 1.0);
        ok &= check_range(cfg, "confidence.agreement_weight", 0.0, 1.0);
        ok &= check_range(cfg, "confidence.penalty_weight", 0.0, 1.0);

        return ok;
    }

private:
    static bool check_option(const Config& cfg, const std::string& key,
                              std::initializer_list<std::string> valid,
                              const std::string& default_val) {
        std::string val = cfg.get_string(key, default_val);
        std::unordered_set<std::string> valid_set(valid);
        if (valid_set.count(val)) return true;

        std::cerr << "[CONFIG ERROR] Unknown value for '" << key << "': '" << val << "'\n"
                  << "  Valid options:";
        for (auto& v : valid) std::cerr << " " << v;
        std::cerr << "\n";
        return false;
    }

    static bool check_positive(const Config& cfg, const std::string& key) {
        // Only validate if the key is explicitly set
        std::string raw = cfg.get_string(key, "");
        if (raw.empty()) return true;
        double val = cfg.get_double(key, 1.0);
        if (val > 0) return true;

        std::cerr << "[CONFIG ERROR] '" << key << "' must be positive, got: " << val << "\n";
        return false;
    }

    static bool check_range(const Config& cfg, const std::string& key,
                             double lo, double hi) {
        std::string raw = cfg.get_string(key, "");
        if (raw.empty()) return true;
        double val = cfg.get_double(key, (lo + hi) / 2);
        if (val >= lo && val <= hi) return true;

        std::cerr << "[CONFIG ERROR] '" << key << "' must be in ["
                  << lo << ", " << hi << "], got: " << val << "\n";
        return false;
    }
};
