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
    |              (BoW or Neural)
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
| `src/retrieval/retriever_factory.h` | Config-driven factory |

What was built:

* IRetriever interface — search(), name(), supports_bm25_retry(), search_bm25_only()
* BM25Retriever — lexical search only
* HNSWRetriever — semantic search only (BoW or neural embeddings)
* HybridRetriever — BM25 + HNSW fusion. All ~80 lines of retrieval logic from commands.cpp moved here.
* RetrieverFactory — reads retrieval.retriever from config, returns the right implementation

What was simplified:
* commands.cpp ask command lost ~80 lines of retrieval logic, replaced with 2 lines: auto retriever = RetrieverFactory::create(reader, embeddir); and auto ranked_docs = retriever->search(need.keywords);

What was fixed:
* Config parser now strips inline # comments (was breaking retrieval.retriever = hybrid # comment)

Config-driven switching verified:
* retrieval.retriever = hybrid → uses BM25 + HNSW fusion
* retrieval.retriever = bm25 → uses BM25 only

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

Config switching verified: `hybrid` → `bm25` → `hybrid` all work without rebuild.


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
