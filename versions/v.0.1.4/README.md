# MoAI v.0.1.4 (Mini version of Artificial Intelligence)

A fully self-contained, offline search engine and question-answering system written from scratch in C++20. MoAI implements every component — from binary index storage to neural inference — without relying on external search libraries.

## What It Does

MoAI ingests a directory of text documents and builds a compact binary index. It answers natural language questions using a cognitive **InformationNeed** model that identifies what the user actually needs (entity + property + answer form + scope).

### Retrieval Modes (pluggable, config-selectable)

```
retrieval.retriever = hybrid    # bm25 | hnsw | hybrid
```

- **BM25** — Classic inverted-index retrieval with boolean queries and phrase matching, ranked by Okapi BM25.
- **HNSW** — Semantic nearest-neighbor search using an HNSW vector index with BoW or neural embeddings.
- **Hybrid** (default) — Fuses BM25 lexical scores with HNSW semantic scores. Configurable weights.

### Query Analysis (pluggable, config-selectable)

```
query.analyzer = auto    # rule | neural | auto
```

- **Rule-based** — Clause splitting, semantic prototype scoring, entity extraction. All signal words and weights loaded from config files.
- **Neural** (optional, requires libtorch) — Multi-task Transformer classifier with intent, answer type, and BIO entity heads.

### Answer Pipeline

- **AnswerScope** — STRICT / NORMAL / EXPANDED controls answer length. Inferred from query wording, property defaults, and confidence.
- **Agreement-based compression** — Dynamically shortens answers when evidence strongly agrees.
- **11 typed synthesizers** — Property-dispatched answer extraction (location, definition, function, advantages, limitations, etc.).
- **Evidence agreement/contradiction** — NormalizedClaim-based analysis with domain vocabularies and opposite pairs.
- **Self-ask + question planning** — Generates internal sub-questions, topologically sorts by dependency.
- **Conversation memory** — Carries entity context across follow-up questions.

## Architecture: Pluggable Algorithm Platform

```
Config selects algorithms:
  query.analyzer = auto          retrieval.retriever = hybrid

         |                                |
         v                                v
  IQueryAnalyzer                   IRetriever
    +-- RuleBased                    +-- BM25Retriever
    +-- Neural                       +-- HNSWRetriever
                                     +-- HybridRetriever
                                     +-- (your new retriever)
         |                                |
         v                                v
  +-------------------------------------------------+
  |              Fixed Pipeline                      |
  |  Chunk -> Synthesize -> Validate -> Compress     |
  +-------------------------------------------------+
```

Adding a new retrieval algorithm: implement `IRetriever`, register in factory, set config. Zero pipeline changes.

## Configuration (No Rebuild Needed)

All tuning parameters and vocabularies are external:

| File | What It Controls |
|------|-----------------|
| `config/default.conf` | 80+ numeric parameters (weights, thresholds, limits) |
| `config/vocabularies/properties.conf` | Per-property word lists (chunk/query/validate/synth) |
| `config/vocabularies/pipeline_rules.conf` | Self-ask rules, dependencies, preferred chunks, form defaults |
| `config/vocabularies/domains.conf` | Evidence domain keywords, negations, opposites |
| `config/vocabularies/language.conf` | Stop words, non-entity words, neural training templates |

Edit any file → restart → new behavior. No recompilation.

## Build

CMake 3.16+, C++20. Zero external dependencies for the core library.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

With libtorch (enables neural encoder and neural query analyzer):

```bash
cmake .. -DUSE_TORCH=ON -DCMAKE_PREFIX_PATH=~/opt/libtorch
cmake --build .
```

## Quick Start

```bash
cd build
./mysearch ingest ../data
./mysearch build-hnsw
./mysearch ask "what is stockholm"
./mysearch ask "where is stockholm" --json
./mysearch ask "explain how TCP works" --detailed
```

## Tests

```bash
# Unit tests (76+ GoogleTest cases)
cmake .. -DBUILD_TESTS=ON && cmake --build .
ctest --output-on-failure

# Integration tests (75 QA benchmark queries)
./mysearch ingest ../data && ./mysearch build-hnsw
bash ../tests/test_qa_integration.sh
```

## Documentation

| File | Contents |
|------|----------|
| `build.md` | Build, test, and usage instructions |
| `history.md` | Step-by-step change history for this version |
| `codebase_analysis.md` | Full architecture analysis (10 sections) |
| `todo.md` | Future refactoring plans |
