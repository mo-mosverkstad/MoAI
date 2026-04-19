# MoAI (Mini version of Artificial Intelligence)

A fully self-contained, offline search engine and question-answering system written from scratch in C++20. MoAI implements every component — from binary index storage to neural inference — without relying on external search libraries like Lucene or Tantivy.

## What It Does

MoAI ingests a directory of text documents and builds a compact binary index. From there it supports three retrieval modes, each building on the last:

- **BM25 search** — Classic inverted-index retrieval with boolean (AND / OR / NOT) and position-aware phrase queries, ranked by Okapi BM25.
- **Hybrid BM25 + ANN search** — Fuses lexical BM25 scores with semantic nearest-neighbor scores (0.7 / 0.3 weighting) from an HNSW vector index, then produces an extractive summary.
- **Question answering** — Analyzes query intent and answer type, retrieves and chunks documents, and synthesizes a type-aware extractive answer with a confidence score. Supports follow-up questions via conversation memory.

## Core Components

- **Custom binary index** — Varint + delta-encoded postings with position storage for phrase matching.
- **BM25 ranking** — Standard Okapi BM25 (k1 = 1.2, b = 0.75) with min-heap top-K extraction.
- **HNSW vector index** — Full multilayer navigable small-world graph with configurable M, efConstruction, and efSearch.
- **Embedding models** — A lightweight BoW feedforward net (no dependencies) as the default, plus an optional Transformer sentence encoder trained with InfoNCE contrastive loss (requires libtorch).
- **Query analyzer** — Two-tier architecture: a rule-based fallback that detects intent, answer type, keywords, and entity from patterns, and an optional neural multi-task Transformer classifier (intent + answer type + BIO entity extraction) that auto-activates when a trained model is present.
- **Document chunker** — Splits documents into paragraphs and classifies each by semantic type (location, definition, person, temporal, procedure, history, general).
- **Answer synthesizer** — Dispatches to specialized extractors per answer type, with year-aware temporal extraction using stem-prefix matching and regex boosting.
- **Extractive summarizer** — Keyword-scored sentence extraction with deduplication and per-document limits.
- **Conversation memory** — Carries entity and answer type across follow-up questions.
- **Confidence scoring** — Combines keyword coverage, evidence relevance, and evidence agreement into a 0–1 score.

## Optional Neural Features (libtorch)

When built with `USE_TORCH=ON`, two additional neural components become available. Both auto-detect their model files at runtime and fall back gracefully when absent:

- **Neural sentence encoder** — Transformer encoder with sinusoidal positional encoding, mean pooling, and L2 normalization. Trained with in-batch InfoNCE contrastive loss on query–document pairs generated from the corpus.
- **Neural query analyzer** — Multi-task Transformer classifier with three heads (intent, answer type, BIO entity). Training data (~27K samples) is auto-generated from the corpus using entity extraction and 22 question templates. Trained with AdamW and a combined cross-entropy loss.

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

76+ GoogleTest cases covering varint encoding, tokenization, query parsing, segment I/O, BM25, boolean/phrase search, HNSW recall, and hybrid search.

```bash
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```
