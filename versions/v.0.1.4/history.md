# v.0.1.4 Change History

For build, test, and usage instructions, see [build.md](build.md).

---

## Step 1: IRetriever + RetrieverFactory

### Goal

Extract the retrieval logic from `commands.cpp` into pluggable algorithm modules behind an `IRetriever` interface. The pipeline no longer knows which retrieval algorithm is running — config selects it.

### Before

`commands.cpp` had ~80 lines of interleaved BM25/HNSW/fusion/embedding logic:
- BM25 setup, HNSW setup, BoW vs neural embedding detection
- Per-need: BM25 search, query embedding, ANN search, score fusion, re-normalization
- BM25-only retry on validation failure

All tangled together with `#ifdef HAS_TORCH` blocks and `use_hybrid` flags.

### After

```cpp
// commands.cpp — the entire retrieval is now:
auto retriever = RetrieverFactory::create(reader, embeddir);
// ...
auto ranked_docs = retriever->search(need.keywords);
```

Config selects the algorithm:
```
retrieval.retriever = hybrid    # bm25 | hybrid
```

### Architecture

```
Config: retrieval.retriever = "hybrid"
         |
         v
RetrieverFactory::create()
         |
    +----+----+----+
    |    |         |
BM25  HNSW   HybridRetriever
              |
         +----+----+
         |         |
       BM25    HNSW+Embedding
              (BoW or Neural)
```

### IRetriever Interface

```cpp
struct IRetriever {
    virtual vector<ScoredDoc> search(const vector<string>& keywords) = 0;
    virtual string name() const = 0;
    virtual bool supports_fallback() const { return false; }
    virtual vector<ScoredDoc> fallback_search(const vector<string>& keywords);
};
```

### Files Created

| File | Description |
|------|-------------|
| `src/retrieval/i_retriever.h` | IRetriever interface with ScoredDoc struct |
| `src/retrieval/bm25_retriever.h` | BM25-only retriever |
| `src/retrieval/hnsw_retriever.h` | HNSW-only retriever with BoW/neural embedding |
| `src/retrieval/hybrid_retriever.h` | Hybrid retriever: BM25 + HNSW fusion |
| `src/retrieval/embedding_index.h` | Shared HNSW + embedding init/query logic |
| `src/retrieval/retriever_factory.h` | Config-driven factory |

What was built:

* IRetriever interface — search(), name(), supports_fallback(), fallback_search()
* BM25Retriever — lexical search only
* HNSWRetriever — semantic search only (BoW or neural embeddings)
* HybridRetriever — BM25 + HNSW fusion. All ~80 lines of retrieval logic from commands.cpp moved here.
* EmbeddingIndex — shared HNSW + embedding infrastructure used by both HNSWRetriever and HybridRetriever
* RetrieverFactory — reads retrieval.retriever from config, returns the right implementation

What was simplified:
* commands.cpp ask command lost ~80 lines of retrieval logic, replaced with 2 lines

What was fixed:
* Config parser now strips inline # comments

Config-driven switching verified:
* retrieval.retriever = hybrid → uses BM25 + HNSW fusion
* retrieval.retriever = bm25 → uses BM25 only
* retrieval.retriever = hnsw → uses HNSW only

### Files Modified

| File | What Changed |
|------|-------------|
| `src/cli/commands.cpp` | Removed ~80 lines of retrieval logic; replaced with `RetrieverFactory::create()` + `retriever->search()` |
| `src/common/config.cpp` | Fixed inline comment stripping (`key = value # comment` now works) |
| `config/default.conf` | Added `retrieval.retriever = hybrid` |

### Config

```
retrieval.retriever = hybrid    # bm25 | hnsw | hybrid
```

| Value | Behavior |
|-------|----------|
| `bm25` | Lexical search only (no embeddings needed) |
| `hnsw` | Semantic search only (requires embeddings) |
| `hybrid` | BM25 + HNSW fusion (default) |

Switching requires no rebuild.

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`

Config switching verified: `hybrid` → `bm25` → `hnsw` → `hybrid` all work without rebuild.


---

### How to Add a New Retrieval Algorithm

Three files, zero pipeline changes:

**1. Implement IRetriever** — create `src/retrieval/my_retriever.h`:

```cpp
#include "i_retriever.h"
#include "../storage/segment_reader.h"

class MyRetriever : public IRetriever {
public:
    explicit MyRetriever(SegmentReader& reader) : reader_(reader) {
        // init your algorithm
    }

    std::vector<ScoredDoc> search(
        const std::vector<std::string>& keywords) override {
        // your retrieval logic — return ranked (docId, score) pairs
    }

    std::string name() const override { return "my_algo"; }

private:
    SegmentReader& reader_;
};
```

**2. Register in factory** — add one `if` branch in `src/retrieval/retriever_factory.h`:

```cpp
if (type == "my_algo")
    return std::make_unique<MyRetriever>(reader);
```

**3. Set config** — in `config/default.conf`:

```
retrieval.retriever = my_algo
```

No changes to commands.cpp, no changes to the answer pipeline, no rebuild of other modules. All 75 tests still pass.


---

## Step 2: IQueryAnalyzer + QueryAnalyzerFactory

### Goal

Formalize the query analyzer into a pluggable interface, same pattern as IRetriever.

### What Changed

- Created `IQueryAnalyzer` interface: `analyze(query) → vector<InformationNeed>`, `name()`
- `RuleBasedQueryAnalyzer` now implements `IQueryAnalyzer` directly
- Created `NeuralQueryAnalyzerAdapter` (libtorch-only) — wraps the legacy `NeuralQueryAnalyzer` output into `InformationNeed`
- Created `QueryAnalyzerFactory` — reads `query.analyzer` from config
- Removed the old `QueryAnalyzer` wrapper class (72 lines of dispatch logic)
- `commands.cpp` simplified: `auto analyzer = QueryAnalyzerFactory::create(embeddir);`

### Config

```
query.analyzer = auto    # rule | neural | auto
```

| Value | Behavior |
|-------|----------|
| `rule` | Always use rule-based analyzer |
| `neural` | Always use neural analyzer (fails if model missing) |
| `auto` | Try neural if model exists, fall back to rule (default) |

### Files Created

| File | Description |
|------|-------------|
| `src/query/i_query_analyzer.h` | IQueryAnalyzer interface |
| `src/query/query_analyzer_factory.h` | Config-driven factory with NeuralQueryAnalyzerAdapter |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/query/query_analyzer.h` | RuleBasedQueryAnalyzer implements IQueryAnalyzer; removed old QueryAnalyzer wrapper |
| `src/query/query_analyzer.cpp` | Removed 72 lines of old QueryAnalyzer dispatch logic |
| `src/cli/commands.cpp` | Uses QueryAnalyzerFactory::create() instead of manual setup |
| `config/default.conf` | Added `query.analyzer = auto`, `query.weight.*` prototype weights |

### Additional Externalizations (Step 2 continued)

- **Property prototypes**: Extracted hardcoded signal words and weights from `query_analyzer.cpp` → words from `properties.conf` QUERY_* sections, weights from `default.conf` query.weight.* keys
- **Property→Form mapping**: Extracted hardcoded if-chain from `detect_form()` → `pipeline_rules.conf [DEFAULT_FORM]` section
- **Last hardcoded word**: Moved `"capital"` special-case in synthesizer to `synth.location_capital_word` config key
- **Electricity fix**: Removed `city` from QUERY_LOCATION (was matching `electri-city` as substring)

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`


---

## Step 3: IEmbedder + EmbedderFactory

### Goal

Decouple embedding choice from retrieval. Previously `EmbeddingIndex` contained all BoW/neural detection logic with `#ifdef HAS_TORCH` blocks. Now it takes an `IEmbedder` and doesn't know or care which embedding method is used.

### IEmbedder Interface

```cpp
struct IEmbedder {
    virtual vector<float> embed(const string& text) = 0;
    virtual size_t dim() const = 0;
    virtual string name() const = 0;
};
```

### Config

```
embedding.method = auto    # bow | transformer | auto
```

| Value | Behavior |
|-------|----------|
| `bow` | BoW feedforward net (no libtorch needed) |
| `transformer` | Neural Transformer encoder (requires libtorch + encoder.pt) |
| `auto` | Try transformer if model exists, fall back to BoW (default) |

### Files Created

| File | Description |
|------|-------------|
| `src/embedding/i_embedder.h` | IEmbedder interface |
| `src/embedding/bow_embedder.h` | BoW embedder wrapping EmbeddingModel + Vocabulary + Tokenizer |
| `src/embedding/transformer_embedder.h` | Transformer embedder wrapping EncoderTrainer (libtorch) |
| `src/embedding/embedder_factory.h` | Config-driven factory with auto-detection |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/retrieval/embedding_index.h` | Rewritten: takes IEmbedder from factory, removed all BoW/neural detection logic and `#ifdef HAS_TORCH` |
| `config/default.conf` | Added `embedding.method = auto` |

### Result

`EmbeddingIndex` went from 95 lines (with `#ifdef` blocks, two model types, vocab loading) to 45 lines (just HNSW build + query forwarding). Embedding choice is now fully orthogonal to retrieval choice.

Three pluggable algorithm slots now active:

```
query.analyzer = auto        # rule | neural | auto
retrieval.retriever = hybrid  # bm25 | hnsw | hybrid
embedding.method = auto       # bow | transformer | auto
```

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`


---

## Step 4: Pipeline + PipelineBuilder

### Goal

One place assembles all components from config. `commands.cpp` becomes thin — just arg parsing and output formatting.

### Before

`commands.cpp` `ask` command: ~200 lines of interleaved setup, processing, and output.

### After

```cpp
// commands.cpp ask command — the entire processing is now:
auto pipeline = PipelineBuilder::build(reader, segdir, embeddir);
PipelineOptions opts{json, force_brief, force_detailed};
auto result = pipeline.run(query, opts);
// ... output formatting only ...
```

### Pipeline struct

```cpp
class Pipeline {
    unique_ptr<IQueryAnalyzer> analyzer_;
    unique_ptr<IRetriever> retriever_;
    Chunker, Synthesizer, Validator, Compressor, SelfAsk, Planner...

    PipelineResult run(const string& query, const PipelineOptions& opts);
};
```

### PipelineBuilder

```cpp
Pipeline PipelineBuilder::build(reader, segdir, embeddir) {
    auto analyzer = QueryAnalyzerFactory::create(embeddir);
    auto retriever = RetrieverFactory::create(reader, embeddir);
    return Pipeline(move(analyzer), move(retriever), reader, conv_path);
}
```

One place. All factories called here. Pipeline code never changes when algorithms change.

### Files Created

| File | Description |
|------|-------------|
| `src/pipeline/pipeline.h` | Pipeline struct with PipelineOptions, PipelineResult |
| `src/pipeline/pipeline.cpp` | Pipeline::run() — the entire QA processing loop |
| `src/pipeline/pipeline_builder.h` | PipelineBuilder::build() — assembles all components from config |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/cli/commands.cpp` | Ask command: 200 lines → ~15 lines of setup + output formatting. Includes simplified. |
| `CMakeLists.txt` | Added pipeline.cpp |

### Result

`commands.cpp` shrank from ~550 to ~376 lines. The ask command's processing logic (178 lines) moved entirely to `Pipeline::run()`. Adding a new algorithm now requires zero changes to commands.cpp.

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`


---

## Step 5: Config Schema Validation

### Goal

Fail fast on bad config with clear error messages, before any processing starts.

### What It Checks

| Check Type | Keys | Example Error |
|-----------|------|---------------|
| Algorithm names | `retrieval.retriever`, `query.analyzer`, `embedding.method` | `Unknown value for 'retrieval.retriever': 'colbert' — Valid options: bm25 hnsw hybrid` |
| Positive values | `bm25.k1`, `bm25.top_k`, `retrieval.max_evidence`, etc. | `'bm25.k1' must be positive, got: -1` |
| Range [0,1] | `retrieval.bm25_weight`, `compression.min_confidence`, `confidence.*_weight`, etc. | `'retrieval.bm25_weight' must be in [0, 1], got: 1.5` |

Only validates keys that are explicitly set — missing keys use baked-in defaults (always valid).

### Files Created

| File | Description |
|------|-------------|
| `src/common/config_validator.h` | ConfigValidator::validate() with check_option, check_positive, check_range |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/cli/main.cpp` | Calls ConfigValidator::validate() after config load, exits with error if invalid |

### Implementation Scenario

Adding a new algorithm: implement interface → register in factory → set config name. Zero pipeline changes.

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`

Bad config tested:
- `retrieval.retriever = colbert` → `[CONFIG ERROR] Unknown value... Valid options: bm25 hnsw hybrid`
- `retrieval.bm25_weight = 1.5` → `[CONFIG ERROR] must be in [0, 1], got: 1.5`


---

## Step 6: Configuration Matrix Testing

### Goal

Test all algorithm combinations automatically to verify the pluggable platform works correctly across configurations.

### What Was Built

`tests/test_config_matrix.sh` — a wrapper that runs `test_qa_integration.sh` with different config combinations and produces a summary report.

### Usage

```bash
# Run default matrix (5 combinations)
bash ../tests/test_config_matrix.sh

# Run specific combinations (analyzer:retriever:embedding)
bash ../tests/test_config_matrix.sh rule:bm25:auto rule:hybrid:bow

# Run all sensible combinations
bash ../tests/test_config_matrix.sh \
    rule:bm25:auto rule:hybrid:auto rule:hybrid:bow \
    rule:hnsw:auto auto:hybrid:auto
```

### Default Matrix Results

| Combination | Passed | Failed | Total |
|-------------|--------|--------|-------|
| rule:bm25:auto | 74 | 1 | 75 |
| rule:hybrid:auto | 75 | 0 | 75 |
| rule:hybrid:bow | 75 | 0 | 75 |
| rule:hnsw:auto | 30 | 45 | 75 |
| auto:hybrid:auto | 75 | 0 | 75 |

Notes:
- `bm25` fails 1 compression test — see analysis below
- `hnsw` fails 45 tests — see analysis below
- All hybrid configurations pass 75/75

### Why `rule:bm25:auto` scores 74/75

The one failing test is "Compression field present in JSON" which queries "what are the drawbacks of NoSQL" and expects `compression = STRONG`.

| | Hybrid | BM25-only |
|---|--------|----------|
| Confidence | 0.88 | 0.82 |
| Scope | STRICT (adjusted from NORMAL) | NORMAL (not adjusted) |
| Compression | STRONG | NONE |

The STRONG compression rule requires: `NORMAL scope + confidence ≥ 0.85 + agreement ≥ 0.7`.

With **hybrid**, the ANN score fusion boosts confidence to 0.88 (above 0.85 threshold). The confidence-based scope adjustment then compresses NORMAL → STRICT, but compression was already decided before scope adjustment — so it sees NORMAL + 0.88 confidence and triggers STRONG.

With **BM25-only**, confidence is 0.82 (below 0.85 threshold). The scope stays NORMAL, but the compression rule's confidence threshold is not met → NONE.

The difference is small (0.88 vs 0.82) and comes from the ANN fusion adding a semantic relevance signal that slightly boosts the refined confidence score. This is not a bug — it correctly shows that hybrid retrieval produces higher-confidence evidence than BM25 alone.

### Why `rule:hnsw:auto` scores 30/75

HNSW-only retrieval uses vector similarity instead of keyword matching. With `embedding.method = auto` (and no libtorch), this resolves to **BoW embeddings** — random projections of bag-of-words vectors that don't capture real semantic meaning.

The result: HNSW-only with BoW embeddings is essentially **random retrieval**. It retrieves documents that happen to be close in a meaningless vector space, not documents that actually match the query.

- The 30 tests that pass are queries where the correct document happens to be nearby in the random vector space, or where the answer is common enough that any retrieved document works.
- The 45 failures are queries that depend on keyword matching (e.g., "where is stockholm" needs documents containing "stockholm").

**Why hybrid works but HNSW-only doesn't:**
- **Hybrid**: BM25 finds the right documents by keyword (0.7 weight), HNSW adds a small semantic boost (0.3 weight) → BM25 dominates → good results
- **HNSW-only**: no keyword matching at all → wrong documents → wrong answers

**This would improve with a trained neural encoder** (`embedding.method = transformer`). With real semantic embeddings, HNSW vectors would capture meaning, and `hnsw`-only retrieval would become viable. This is a key motivation for training the neural encoder.

**Takeaway**: The matrix test correctly reveals that `hnsw + bow` is not a useful configuration, while `hnsw + transformer` (with a trained encoder) would be. The test serves as a quality gate for new algorithm combinations.

### Files Created

| File | Description |
|------|-------------|
| `tests/test_config_matrix.sh` | Config matrix test runner with summary report |
