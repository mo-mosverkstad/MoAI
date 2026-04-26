# v.0.1.3 Change History

For build, test, and usage instructions, see [build.md](build.md).

---

## Step 1: Configuration Externalization

### Goal

Abstract all hardcoded tuning parameters (scoring weights, thresholds, limits) into a single runtime configuration file so that behavior can be adjusted without changing source code or rebuilding.

### Problem

Every time a new issue was discovered (e.g., wrong definition content, bad chunk selection, truncation too aggressive), the fix required changing hardcoded numeric values scattered across multiple source files, recompiling, and retesting. This made tuning slow and error-prone.

### Solution

A `config/default.conf` key=value file loaded at startup by a Config singleton. Every module reads its parameters via `Config::instance().get_double(key, default)`. All defaults are baked into the code, so the system works identically even without a config file.

### Config File Structure

`config/default.conf` contains 80+ parameters in 10 categories:

| Category | Example Keys | Count |
|----------|-------------|-------|
| BM25 Ranking | `bm25.k1`, `bm25.b`, `bm25.top_k` | 3 |
| HNSW Vector Index | `hnsw.M`, `hnsw.ef_construction`, `hnsw.ef_search` | 3 |
| Hybrid Retrieval | `retrieval.bm25_weight`, `retrieval.ann_weight`, `retrieval.max_evidence` | 4 |
| Chunk Selection | `chunk.max_per_doc`, `chunk.primary_type_boost`, `chunk.secondary_type_boost` | 6 |
| Answer Scope | `scope.strict_max_chars`, `scope.normal_max_chars`, `scope.high_confidence_threshold` | 10 |
| Compression | `compression.min_confidence`, `compression.normal_strong_agreement` | 8 |
| Refined Confidence | `confidence.coverage_weight`, `confidence.volume_divisor` | 7 |
| Synthesizer Scoring | `synth.keyword_score`, `synth.entity_subject_boost`, `synth.max_definition_text` | 17 |
| Entity Proximity | `proximity.definition_first_pass`, `proximity.location_chunk` | 6 |
| Validator | `validator.signal_ratio_weak`, `validator.fail_confidence_multiplier` | 5 |

### Config Class Design

```cpp
// Singleton, loaded once at startup
class Config {
public:
    static Config& instance();
    bool load(const std::string& path);
    double get_double(const std::string& key, double default_val) const;
    int get_int(const std::string& key, int default_val) const;
    size_t get_size(const std::string& key, size_t default_val) const;
};
```

The synthesizer uses a cached `SynthConfig` struct to avoid repeated map lookups:

```cpp
struct SynthConfig {
    double kw_score, entity_score, entity_subject_boost, ...;
    static const SynthConfig& get();  // lazy-initialized from Config
};
```

### Files Created

| File | Description |
|------|-------------|
| `config/default.conf` | 80+ tunable parameters with comments, organized by category |
| `src/common/config.h` | Config singleton header with typed getters |
| `src/common/config.cpp` | Key=value parser with comment/whitespace handling |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/cli/main.cpp` | Load config at startup from `../config/default.conf` |
| `src/answer/answer_scope.h` | Scope limits and confidence thresholds read from Config |
| `src/answer/answer_compressor.cpp` | All 8 compression thresholds read from Config |
| `src/answer/answer_validator.cpp` | Signal ratios, confidence multipliers, penalty weights from Config |
| `src/answer/answer_synthesizer.cpp` | Added SynthConfig cache; 30+ scoring weights externalized |
| `src/chunk/chunker.cpp` | Chunk selection boosts and min size from Config |
| `src/cli/commands.cpp` | Retrieval limits, fusion weights, support threshold from Config |
| `CMakeLists.txt` | Added `config.cpp` to build |

### How It Works

```bash
# 1. Build once
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build .

# 2. Edit config (no rebuild needed)
vim ../config/default.conf
# Change: scope.strict_max_chars = 100

# 3. Run — new behavior immediately
./mysearch ask "what is stockholm"
# Answer truncated to 100 chars instead of 200
```

Config is auto-loaded from `../config/default.conf` relative to the binary. If the file is missing, all baked-in defaults are used.

### Verification

- Changed `scope.strict_max_chars` from 200 to 100 → answer shortened without rebuild
- Restored to 200 → original behavior restored
- All 75 integration tests pass with default config

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`
