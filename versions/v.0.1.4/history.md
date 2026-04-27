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
