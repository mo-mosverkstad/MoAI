# MoAI (Mini version of Artificial Intelligence)

A fully self-contained, offline search engine and question-answering system written from scratch in C++20. MoAI implements every component — from binary index storage to neural inference — without relying on external search libraries like Lucene or Tantivy.

## What It Does

MoAI ingests a directory of text documents and builds a compact binary index. It answers natural language questions by modeling what the user actually needs (entity + property + answer form), not just matching keywords.

- **Lexical search** — Inverted-index retrieval with boolean (AND / OR / NOT) and phrase queries, ranked by Okapi BM25.
- **Semantic search** — Nearest-neighbor retrieval using an HNSW vector index with BoW or neural embeddings.
- **Hybrid search** — Fuses lexical and semantic scores with configurable weights.
- **Question answering** — Decomposes queries into InformationNeeds, retrieves and chunks documents, and synthesizes typed extractive answers with confidence scoring, agreement-based compression, and conversation memory.

## Architecture

MoAI is a **pluggable algorithm platform**. The QA pipeline is fixed; algorithms are interchangeable via interfaces and selected by configuration:

- **IRetriever** — BM25, HNSW, Hybrid, or your own retrieval algorithm
- **IQueryAnalyzer** — Rule-based or neural (multi-task Transformer), or your own analyzer
- **IEmbedder** — BoW feedforward net, Transformer encoder, or your own embedder

A **PipelineBuilder** assembles all components from config at startup. All tuning parameters, vocabularies, and pipeline rules are externalized to configuration files — no rebuild needed to adjust behavior.

## Core Components

- **Custom binary index** — Varint + delta-encoded postings with position storage for phrase matching.
- **BM25 ranking** — Okapi BM25 with configurable k1 and b parameters.
- **HNSW vector index** — Full multilayer navigable small-world graph.
- **Embedding models** — BoW feedforward net (default) or Transformer sentence encoder (optional, libtorch).
- **Query analyzer** — Rule-based semantic prototype scoring, or neural multi-task Transformer classifier.
- **Document chunker** — Splits documents into paragraphs, classifies by semantic type (11 types).
- **Answer synthesizer** — 11 typed extractors dispatched by property, with scope-aware truncation.
- **Agreement-based compression** — Dynamically reduces answer length when evidence strongly agrees.
- **Evidence normalizer** — Domain-aware agreement and contradiction detection.
- **Self-ask + question planner** — Internal sub-question generation with dependency-aware topological sort.
- **Conversation memory** — Carries entity context across follow-up questions.
- **Confidence scoring** — Multi-factor score combining coverage, volume, agreement, and contradiction penalty.
- **Config validation** — Fail fast on bad configuration with clear error messages.

## Build

CMake 3.16+, C++20. The core library has zero external dependencies.

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

## Tests

76+ GoogleTest unit tests, 75 QA integration tests, and configuration matrix tests.

```bash
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest --output-on-failure

# QA integration tests
./moai ingest ../data && ./moai build-hnsw
bash ../tests/test_qa_integration.sh

# Configuration matrix tests (multiple algorithm combinations)
bash ../tests/test_config_matrix.sh
```

## Versions

Each version is self-contained in `versions/v.X.Y.Z/` with its own source, config, tests, and documentation.

| Version | Focus |
|---------|-------|
| v.0.1.1 | InformationNeed model, hybrid retrieval, self-ask reasoning |
| v.0.1.2 | AnswerScope, agreement-based compression, definition synthesis |
| v.0.1.3 | Configuration and vocabulary externalization |
| v.0.1.4 | Pluggable algorithm platform (IRetriever, IQueryAnalyzer, IEmbedder) |
| v.0.1.5 | Performance profiling (per-query timing, RSS, quality metrics, benchmark runner) |
| v.0.1.6 | CLI unification — single `moai` binary replaces `mysearch` + `train_encoder` |

See each version's `README.md`, `codebase_analysis.md`, and `history.md` for details.
