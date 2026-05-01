# MoAI v.0.1.6 (Mini version of Artificial Intelligence)

A fully self-contained, offline search engine and question-answering system written from scratch in C++20. MoAI implements every component — from binary index storage to neural inference — without relying on external search libraries.

## What It Does

MoAI ingests a directory of text documents and builds a compact binary index. It answers natural language questions using a cognitive **InformationNeed** model that identifies what the user actually needs (entity + property + answer form + scope).

## Architecture: Pluggable Algorithm Platform

Three algorithm slots are pluggable via interfaces and selected by configuration:

```
PipelineBuilder assembles all components from config at startup:

  query.analyzer = auto         retrieval.retriever = hybrid       embedding.method = auto
         |                                |                                |
         v                                v                                v
  IQueryAnalyzer                   IRetriever                       IEmbedder
    +-- RuleBased                    +-- BM25Retriever                +-- BoWEmbedder
    +-- Neural                       +-- HNSWRetriever ---uses--->    +-- TransformerEmbedder
    +-- (your new analyzer)          +-- HybridRetriever --uses--->   +-- (your new embedder)
                                     +-- (your new retriever)
         |                                |
         v                                v
  +-------------------------------------------------+
  |              Fixed Pipeline                      |
  |  Chunk -> Synthesize -> Validate -> Compress     |
  +-------------------------------------------------+
```

Adding a new algorithm: implement the interface, register in factory, set config. Zero pipeline changes.

### Retrieval (pluggable)

| Config Value | Algorithm |
|-------------|-----------|
| `bm25` | Lexical search only (Okapi BM25) |
| `hnsw` | Semantic nearest-neighbor (HNSW vector index) |
| `hybrid` | BM25 + HNSW fusion (default) |

### Query Analysis (pluggable)

| Config Value | Algorithm |
|-------------|-----------|
| `rule` | Rule-based clause splitting + semantic prototype scoring |
| `neural` | Multi-task Transformer (intent + answer type + BIO entity) |
| `auto` | Try neural if model exists, fall back to rule (default) |

### Embedding (pluggable)

| Config Value | Algorithm |
|-------------|-----------|
| `bow` | BoW feedforward net (no libtorch needed) |
| `transformer` | Neural Transformer encoder (requires libtorch) |
| `auto` | Try transformer if model exists, fall back to BoW (default) |

### Answer Pipeline (fixed)

- **AnswerScope** — STRICT / NORMAL / EXPANDED controls answer length
- **Agreement-based compression** — Shortens answers when evidence strongly agrees
- **11 typed synthesizers** — Property-dispatched answer extraction
- **Evidence agreement/contradiction** — Domain-aware analysis
- **Self-ask + question planning** — Sub-question generation with topological sort
- **Conversation memory** — Entity context across follow-up questions
- **Config validation** — Fail fast on bad config with clear error messages

## Configuration (No Rebuild Needed)

| File | What It Controls |
|------|-----------------|
| `config/default.conf` | Algorithm selection + 80+ tuning parameters |
| `config/vocabularies/properties.conf` | Per-property word lists (chunk/query/validate/synth) |
| `config/vocabularies/pipeline_rules.conf` | Self-ask rules, dependencies, preferred chunks, form defaults |
| `config/vocabularies/domains.conf` | Evidence domain keywords, negations, opposites |
| `config/vocabularies/language.conf` | Stop words, non-entity words, neural training templates |

## Build

CMake 3.16+, C++20. Zero external dependencies for the core library.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

With libtorch:

```bash
cmake .. -DUSE_TORCH=ON -DCMAKE_PREFIX_PATH=~/opt/libtorch
cmake --build .
```

## Quick Start

```bash
cd build
./moai ingest ../data
./moai build-hnsw
./moai ask "what is stockholm"
./moai ask "where is stockholm" --json
./moai ask "explain how TCP works" --detailed
```

## Tests

```bash
# Unit tests (76+ GoogleTest cases)
cmake .. -DBUILD_TESTS=ON && cmake --build .
ctest --output-on-failure

# Integration tests (75 QA benchmark queries)
./moai ingest ../data && ./moai build-hnsw
bash ../tests/test_qa_integration.sh

# Configuration matrix tests (5 combos × 75 = 375 test runs)
bash ../tests/test_config_matrix.sh
```

## Documentation

| File | Contents |
|------|----------|
| `build.md` | Build, test, and usage instructions |
| `history.md` | Step-by-step change history |
| `codebase_analysis.md` | Full architecture analysis (10 sections) |
| `todo.md` | Design rationale and future plans |
