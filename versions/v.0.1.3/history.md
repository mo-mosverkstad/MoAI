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


---

### Step 1b: Per-Module Cached Config Structs

Refined the config access pattern. Instead of scattered `Config::instance().get_double(...)` calls throughout each function, each module now has a small cached struct that loads all its config values once at first use via a static lazy-init lambda.

| Module | Cached Struct | Config Keys |
|--------|--------------|-------------|
| `answer_scope.cpp` | `ScopeConfig` | `scope.*` (8 values) |
| `answer_compressor.cpp` | `CompressorConfig` | `compression.*` (8 values) |
| `answer_validator.cpp` | `ValidatorConfig` | `validator.*` + `confidence.*` (11 values) |
| `answer_synthesizer.cpp` | `SynthConfig` | `synth.*` + `proximity.*` (30 values) |
| `chunker.cpp` | `ChunkConfig` | `chunk.*` (5 values) |

Pattern used in each module:

```cpp
struct ModuleConfig {
    double param1, param2;
    static const ModuleConfig& get() {
        static ModuleConfig mc = []() {
            auto& c = Config::instance();
            return ModuleConfig{
                c.get_double("key1", default1),
                c.get_double("key2", default2),
            };
        }();
        return mc;
    }
};
```

Also moved `answer_scope.h` inline functions to `answer_scope.cpp` (header now contains declarations only), added to CMakeLists.txt.

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`


---

## Step 2: Vocabulary Externalization

### Goal

Move all hardcoded word lists (chunk classification signals, validator signals, synthesizer scoring words, evidence domain keywords, opposite pairs) into external vocabulary files so they can be edited without touching source code or rebuilding.

### Problem

Adding or tuning vocabulary words required editing C++ source files, recompiling, and retesting. This made it impractical to ask an LLM to expand vocabularies or for a user to quickly tune word lists.

### Solution

A `VocabLoader` utility that parses `[SECTION]` / comma-separated word files. Each module has a cached `*Vocab` struct (same lazy-init pattern as Step 1) that loads its word lists from `config/vocabularies/*.conf` at first use. If a vocabulary file is missing, a warning is logged to stderr. No inline defaults are duplicated in source code — the `.conf` files are the single source of truth.

### Vocabulary Files

| File | Sections | Used By |
|------|----------|---------|
| `chunk_signals.conf` | LOCATION, LOCATION_CAPITAL_CONTEXT/REQUIRES, ADVANTAGES, LIMITATIONS, USAGE, FUNCTION, DEFINITION, PERSON, TEMPORAL, PROCEDURE, HISTORY | `chunker.cpp` (ChunkVocab) |
| `validator_signals.conf` | LOCATION, DEFINITION, FUNCTION, ADVANTAGES, LIMITATIONS, USAGE, HISTORY, TIME, COMPARISON | `answer_validator.cpp` (ValidatorConfig) |
| `synth_words.conf` | DEF_FIRST_PASS_PATTERNS/PENALTIES, DEF_FALLBACK_PATTERNS/PENALTIES, LOC_* (4 lists), ADVANTAGES/LIMITATIONS/USAGE/HISTORY/COMPARISON_BOOST, HISTORY_SIGNALS | `answer_synthesizer.cpp` (SynthVocab) |
| `evidence_domains.conf` | GEO, TECH, SCIENCE, ECON, NEGATION, OPPOSITES | `evidence_normalizer.cpp` (EvidenceVocab) |
| `query_prototypes.conf` | LOCATION, ADVANTAGES, LIMITATIONS, FUNCTION, USAGE, HISTORY, TIME, COMPARISON, DEFINITION, COMPOSITION | (prepared for future query_analyzer.cpp integration) |

### File Format

```
# Comments start with #
[SECTION_NAME]
word1, word2, word3
word4, word5

# Opposites use pipe separator
[OPPOSITES]
east | west
cheap | expensive
```

### Files Created

| File | Description |
|------|-------------|
| `src/common/vocab_loader.h/.cpp` | VocabLoader: parses [SECTION]/comma files, load_or_default merge |
| `config/vocabularies/chunk_signals.conf` | 12 sections, ~90 signal words |
| `config/vocabularies/validator_signals.conf` | 9 sections, ~80 signal words |
| `config/vocabularies/synth_words.conf` | 14 sections, ~100 scoring words |
| `config/vocabularies/evidence_domains.conf` | 6 sections, ~80 keywords + 14 opposite pairs |
| `config/vocabularies/query_prototypes.conf` | 10 sections, ~80 prototype words |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/chunk/chunker.cpp` | Added ChunkVocab cached struct; classify_chunk reads from vocab |
| `src/answer/answer_validator.cpp` | ValidatorConfig now includes signal words loaded from vocab; removed hardcoded expected_signals function |
| `src/answer/answer_synthesizer.cpp` | Added SynthVocab cached struct; all word lists in definition/location/typed synthesizers read from vocab |
| `src/answer/evidence_normalizer.cpp` | Added EvidenceVocab cached struct; domain keywords, negations, opposites loaded from vocab |
| `CMakeLists.txt` | Added vocab_loader.cpp |

### How It Works

```bash
# Add a new word to chunk LOCATION signals — no rebuild needed
echo "metropolitan" >> config/vocabularies/chunk_signals.conf
./mysearch ask "where is stockholm"  # picks up new word

# Ask an LLM to expand vocabularies
# "Add 20 more science domain keywords to config/vocabularies/evidence_domains.conf [SCIENCE] section"
```

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`

---

## Step 3: Planning Rules Externalization

### Goal

Extract self-ask expansion rules and question planner dependency rules from hardcoded switch/if-chains into an external rules file, so adding new planning logic requires only a config edit.

### Solution

A `planning_rules.conf` file with two sections parsed by a `PlanningRules` cached struct (same lazy-init pattern as Steps 1-2).

### Rules File Format

```
[SELF_ASK]
ADVANTAGES  -> DEFINITION : SHORT_FACT : definition
ADVANTAGES  -> LOCATION   : SHORT_FACT : located, region

[DEPENDENCIES]
HISTORY     -> LOCATION
ADVANTAGES  -> DEFINITION

[PREFERRED_CHUNKS]
LOCATION    -> LOCATION, GENERAL
DEFINITION  -> DEFINITION, LOCATION, GENERAL
```

Adding a new rule or changing chunk type preferences is now a one-line config edit.

### Files Created

| File | Description |
|------|-------------|
| `config/vocabularies/planning_rules.conf` | 7 self-ask rules + 7 dependency rules + 10 preferred chunk mappings |
| `src/common/rules_loader.h/.cpp` | PlanningRules cached struct, parses `->` arrow format |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/answer/self_ask.cpp` | Rewritten to iterate PlanningRules::get().self_ask instead of hardcoded switch |
| `src/answer/question_planner.cpp` | Rewritten to iterate PlanningRules::get().dependencies instead of hardcoded if-chains |
| `src/answer/answer_synthesizer.cpp` | `preferred_chunks_for()` now reads from PlanningRules::get().preferred_chunks |
| `src/chunk/chunker.cpp` | `preferred_types_for()` now reads from PlanningRules::get().preferred_chunks (removed duplicate mapping) |
| `CMakeLists.txt` | Added rules_loader.cpp |

### Additional Externalizations (Step 3 continued)

**Preferred chunk type mappings**: Extracted `preferred_chunks_for()` (answer_synthesizer.cpp) and `preferred_types_for()` (chunker.cpp) — two duplicate hardcoded switch statements — into `[PREFERRED_CHUNKS]` section of planning_rules.conf. Both now read from `PlanningRules::get().preferred_chunks`.

**Chunk classification types**: Added `[TYPES]` section to chunk_signals.conf listing the check order. `classify_chunk()` now iterates dynamically instead of hardcoded if-chain. Adding a new ChunkType is a config-only edit.

**Hybrid fusion weights**: `hybrid_search.cpp` line 96 now reads `retrieval.bm25_weight` / `retrieval.ann_weight` from config instead of hardcoded 0.7/0.3.

**Neural query templates**: Extracted 22 training templates from `neural_query_analyzer.cpp` into `config/vocabularies/query_templates.conf`. Format: `prefix | suffix | intent | answer_type`. Adding new question patterns for training is now a config edit.

**Stop words and non-entity words**: Extracted 3 duplicate stop word lists (from `query_analyzer.cpp` and 2 in `neural_query_analyzer.cpp`) plus the non-entity word list into `config/vocabularies/stop_words.conf`. All three files now read from the same single source.

**Query analyzer word lists**: Extracted clause-splitting triggers, scope inference hints (STRICT/EXPANDED), and form detection hints (EXPLANATION/SUMMARY) from hardcoded if-chains in `query_analyzer.cpp` into `config/vocabularies/query_prototypes.conf`. Adding a new scope trigger like `"detailed"` is now a config edit.

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`
