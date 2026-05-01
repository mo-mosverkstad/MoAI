# MoAI v.0.1.5 — Codebase Analysis

## 1. System Overview

MoAI is a fully self-contained, offline question-answering system written in C++20. It ingests text documents, builds a binary search index, and answers natural language questions using a cognitive **InformationNeed** model — without relying on LLMs or external search libraries.

All tuning parameters are in `config/default.conf`. All vocabularies are in `config/vocabularies/` (4 files). Retrieval algorithms are pluggable via the `IRetriever` interface — config selects which one runs. Per-query performance profiling is available via `profiling.enabled = true`.

---

## 2. High-Level Architecture

```
                         +----------------------+
                         |      User Query      |
                         |  [--brief/--detailed] |
                         +----------+-----------+
                                    |
                         PipelineBuilder::build()
                         assembles all components
                         from config at startup
                                    |
                    +---------------v---------------+
                    |   Query Analysis (pluggable)   |  Phase 1
                    |                                |
                    |   IQueryAnalyzer interface      |
                    |     +-- RuleBasedQueryAnalyzer  |
                    |     +-- NeuralQueryAnalyzer     |
                    |     +-- (your new analyzer)     |
                    |                                |
                    |   Selected by: query.analyzer   |
                    +---------------+---------------+
                                    |
                    +---------------v---------------+
                    |   ConversationState            |
                    |   SelfAsk + QuestionPlanner    |
                    |   CLI scope override           |
                    +---------------+---------------+
                                    |
              +---------------------v---------------------+
              |         Retrieval Layer (pluggable)         |  Phase 2
              |                                            |
              |  IRetriever interface                       |
              |    +-- BM25Retriever                        |
              |    +-- HNSWRetriever ----+                  |
              |    +-- HybridRetriever --+                  |
              |    +-- (your new retriever)                 |
              |                         |                  |
              |              +----------v----------+       |
              |              | Embedding (pluggable)|       |
              |              |                     |       |
              |              | IEmbedder interface  |       |
              |              |  +-- BoWEmbedder     |       |
              |              |  +-- TransformerEmb  |       |
              |              |  +-- (your new emb)  |       |
              |              |                     |       |
              |              | embedding.method     |       |
              |              +---------------------+       |
              |                                            |
              |  Selected by: retrieval.retriever = hybrid  |
              +---------------------+---------------------+
                                    |
              +---------------------v---------------------+
              |         Answer Layer                        |  Phase 3
              |                                            |
              |  Chunker -> AnswerSynthesizer               |
              |  -> EvidenceNormalizer -> AgreementCompressor|
              |  -> AnswerValidator (+ fallback retry)      |
              +---------------------+---------------------+
                                    |
                    +---------------v---------+
                    |  JSON / Text Output     |
                    +-------------------------+

All three pluggable slots are config-driven:
  query.analyzer = auto         # rule | neural | auto
  retrieval.retriever = hybrid   # bm25 | hnsw | hybrid
  embedding.method = auto        # bow | transformer | auto
```

---

## 3. Pluggable Algorithm Design

The pipeline depends only on interfaces — never on concrete implementations. Config selects which algorithm runs.

### Interfaces

| Interface | Method | Implementations |
|-----------|--------|----------------|
| `IRetriever` | `search(keywords) → ScoredDoc[]` | BM25Retriever, HNSWRetriever, HybridRetriever |
| `IQueryAnalyzer` | `analyze(query) → InformationNeed[]` | RuleBasedQueryAnalyzer, NeuralQueryAnalyzerAdapter |
| `IEmbedder` | `embed(text) → vector<float>` | BoWEmbedder, TransformerEmbedder |

### Factories

| Factory | Config Key | Options |
|---------|-----------|--------|
| `RetrieverFactory` | `retrieval.retriever` | bm25, hnsw, hybrid |
| `QueryAnalyzerFactory` | `query.analyzer` | rule, neural, auto |
| `EmbedderFactory` | `embedding.method` | bow, transformer, auto |

### How to Add a New Retrieval Algorithm

1. Create `src/retrieval/my_retriever.h` implementing `IRetriever`
2. Register in `src/retrieval/retriever_factory.h` (add one `if` branch)
3. Add config option: `retrieval.retriever = my_algo`
4. No pipeline changes. No other files touched.

Example:
```cpp
class MyRetriever : public IRetriever {
public:
    MyRetriever(SegmentReader& reader) { /* init */ }
    vector<ScoredDoc> search(const vector<string>& kw) override { /* your algo */ }
    string name() const override { return "my_algo"; }
};
```

```cpp
// In retriever_factory.h, add:
if (type == "my_algo")
    return std::make_unique<MyRetriever>(reader);
```

```
# In default.conf:
retrieval.retriever = my_algo
```

---

## 4. Runtime Configuration

### Config File (`config/default.conf`)

80+ tunable parameters. Inline `#` comments supported.

| Category | Example Keys |
|----------|-------------|
| `profiling.*` | `enabled` (true/false), `output_file` (path) |
| `query.*` | `analyzer` (rule/neural/auto), `weight.LOCATION` through `weight.DEFINITION` (prototype weights) |
| `retrieval.*` | `retriever` (bm25/hnsw/hybrid), `bm25_weight`, `ann_weight`, `max_ranked_docs`, `max_evidence` |
| `embedding.*` | `method` (bow/transformer/auto) |
| `bm25.*` | `k1`, `b`, `top_k` |
| `hnsw.*` | `M`, `ef_construction`, `ef_search` |
| `chunk.*` | `max_per_doc`, `primary_type_boost`, `secondary_type_boost` |
| `scope.*` | `strict_max_chars`, `high_confidence_threshold` |
| `compression.*` | `min_confidence`, `normal_strong_agreement` |
| `confidence.*` | `coverage_weight`, `volume_divisor` |
| `synth.*` | `keyword_score`, `entity_subject_boost`, `max_definition_text` |
| `proximity.*` | `definition_first_pass`, `location_chunk` |
| `validator.*` | `signal_ratio_weak`, `fail_confidence_multiplier` |

### Vocabulary Files (`config/vocabularies/`)

| File | Contents |
|------|----------|
| `properties.conf` | Per-property word lists (CHUNK_, QUERY_, VALIDATE_, SYNTH_ prefixes) |
| `pipeline_rules.conf` | Self-ask rules, dependencies, preferred chunks, scope/form hints, default form per property |
| `domains.conf` | Evidence domain keywords, negations, opposites |
| `language.conf` | Stop words, non-entity words, neural training templates |

### Config Access Pattern

Each module has a cached struct loaded once at startup:

| Module | Struct | Source |
|--------|--------|--------|
| `answer_scope.cpp` | ScopeConfig | `default.conf` |
| `answer_compressor.cpp` | CompressorConfig | `default.conf` |
| `answer_validator.cpp` | ValidatorConfig | `default.conf` + `properties.conf` |
| `answer_synthesizer.cpp` | SynthConfig + SynthVocab | `default.conf` + `properties.conf` |
| `chunker.cpp` | ChunkConfig + ChunkVocab | `default.conf` + `properties.conf` |
| `evidence_normalizer.cpp` | EvidenceVocab | `domains.conf` |
| `query_analyzer.cpp` | QueryVocab | `language.conf` + `pipeline_rules.conf` + `properties.conf` + `default.conf` |

---

## 5. QA Pipeline Flow (ask command)

```
1. Config loaded at startup (main.cpp)
2. ConfigValidator::validate() — fail fast on bad config

3. PipelineBuilder::build(reader, segdir, embeddir)
   -> calls QueryAnalyzerFactory, RetrieverFactory
   -> returns assembled Pipeline

3. pipeline.run(query, opts)
   a. analyzer->analyze(query)
   b. ConversationState, SelfAsk expansion, scope override
   c. QuestionPlanner topological sort
   d. For each need: retrieve -> chunk -> synthesize -> validate -> compress
   e. Returns PipelineResult

4. commands.cpp formats output (JSON or text)
```


---

## 6. Module Descriptions

### 6.1 Pipeline (`src/pipeline/`)

| File | Purpose |
|------|---------|
| `pipeline.h` | Pipeline struct (owns all components), PipelineOptions, PipelineResult |
| `pipeline.cpp` | Pipeline::run() — QA processing loop with ScopeTimer instrumentation |
| `pipeline_builder.h` | PipelineBuilder::build() — calls all factories, assembles Pipeline |

### 6.2 Profiling (`src/profiling/`) — NEW in v.0.1.5

| File | Purpose |
|------|---------|
| `profiler.h/.cpp` | Profiler singleton: begin_query, record, end_query, JSON Lines output |
| `scope_timer.h` | RAII ScopeTimer: records elapsed ms to Profiler on destruction |

Enabled by `profiling.enabled = true`. Zero overhead when disabled. Output: one JSON object per query to `profiling.output_file`.

### 6.3 Retrieval (`src/retrieval/`)

| File | Purpose |
|------|---------|
| `i_retriever.h` | IRetriever interface: search(), name(), supports_fallback(), fallback_search() |
| `embedding_index.h` | Shared HNSW + IEmbedder infrastructure (embedding-agnostic) |
| `bm25_retriever.h` | BM25-only retriever |
| `hnsw_retriever.h` | HNSW-only retriever with BoW/neural embedding auto-detection |
| `hybrid_retriever.h` | Hybrid retriever: BM25 + HNSW fusion |
| `retriever_factory.h` | Config-driven factory: reads `retrieval.retriever` (bm25 \| hnsw \| hybrid) |

### 6.4 Configuration (`src/common/`)

| File | Purpose |
|------|---------|
| `config.h/.cpp` | Config singleton: key=value parser with inline comment support |
| `config_validator.h` | ConfigValidator: checks algorithm names, positive values, ranges at startup |
| `vocab_loader.h/.cpp` | VocabLoader: [SECTION]/comma vocabulary file parser |
| `rules_loader.h/.cpp` | PlanningRules: self-ask, dependencies, preferred chunks, default forms, query templates |
| `varint.h/.cpp` | Variable-length integer encoding |
| `file_utils.h/.cpp` | File I/O helpers |
| `types.h` | Type aliases |

### 6.5 Storage (`src/storage/`)

| File | Purpose |
|------|---------|
| `segment_writer.h/.cpp` | Writes inverted index segments |
| `segment_reader.h/.cpp` | Reads segments: postings, doc text, term enumeration |
| `manifest.h/.cpp` | Segment metadata |
| `wal.h/.cpp` | Write-ahead log |

### 6.6 Inverted Index (`src/inverted/`)

| File | Purpose |
|------|---------|
| `tokenizer.h/.cpp` | Lowercase alphanumeric tokenization |
| `index_builder.h/.cpp` | Directory ingestion -> segment build |
| `index_reader.h/.cpp` | Low-level postings access |
| `bm25.h/.cpp` | Okapi BM25 ranking (k1, b configurable) |
| `query_parser.h/.cpp` | Boolean query parsing (AND/OR/NOT) |
| `phrase_matcher.h/.cpp` | Position-aware phrase matching |
| `search_engine.h/.cpp` | Full boolean + phrase search |

### 6.7 Vector Index (`src/hnsw/`)

| File | Purpose |
|------|---------|
| `hnsw_index.h/.cpp` | HNSW graph (M, efConstruction, efSearch configurable) |
| `hnsw_node.h/.cpp` | Node with per-layer neighbor lists |

### 6.8 Embedding (`src/embedding/`)

| File | Purpose |
|------|---------|
| `vocab.h/.cpp` | Term <-> ID mapping |
| `embedding_model.h/.cpp` | BoW feedforward net |
| `i_embedder.h` | IEmbedder interface: embed(), dim(), name() |
| `bow_embedder.h` | BoW embedder wrapping EmbeddingModel + Vocabulary + Tokenizer |
| `transformer_embedder.h` | Transformer embedder wrapping EncoderTrainer (libtorch) |
| `embedder_factory.h` | Config-driven factory: reads `embedding.method` (bow \| transformer \| auto) |

### 6.9 Neural Encoder (`src/encoder/`, requires libtorch)

| File | Purpose |
|------|---------|
| `sentence_encoder.h/.cpp` | Transformer encoder with sinusoidal PE |
| `encoder_trainer.h/.cpp` | InfoNCE contrastive training |
| `train_main.cpp` | Training entry point |

### 6.10 Query Analysis (`src/query/`)

| File | Purpose |
|------|---------|
| `information_need.h` | Property (11), AnswerForm (5), AnswerScope (3), InformationNeed struct |
| `i_query_analyzer.h` | IQueryAnalyzer interface: analyze(), name() |
| `query_analyzer.h/.cpp` | RuleBasedQueryAnalyzer implementing IQueryAnalyzer |
| `query_analyzer_factory.h` | Config-driven factory with NeuralQueryAnalyzerAdapter |
| `neural_query_analyzer.h/.cpp` | Neural multi-task Transformer (libtorch) |

### 6.11 Chunking (`src/chunk/`)

| File | Purpose |
|------|---------|
| `chunker.h/.cpp` | Paragraph splitting, ChunkType classification (from properties.conf), type-aware selection |

### 6.12 Answer Synthesis (`src/answer/`)

| File | Purpose |
|------|---------|
| `answer_scope.h/.cpp` | Scope policy with ScopeConfig cache |
| `answer_compressor.h/.cpp` | Agreement-based compression with CompressorConfig cache |
| `answer_synthesizer.h/.cpp` | 11 typed synthesizers with SynthConfig + SynthVocab |
| `answer_validator.h/.cpp` | Self-ask validation with ValidatorConfig (signals from properties.conf) |
| `evidence_normalizer.h/.cpp` | NormalizedClaim, agreement/contradiction (from domains.conf) |
| `question_planner.h/.cpp` | Topological sort (from pipeline_rules.conf) |
| `self_ask.h/.cpp` | Sub-question expansion (from pipeline_rules.conf) |

### 6.13 Other

| File | Purpose |
|------|---------|
| `src/conversation/conversation_state.h/.cpp` | Entity/property carry-over, file persistence |
| `src/hybrid/hybrid_builder.h/.cpp` | Bootstrap: vocab + BoW model + HNSW from segment |
| `src/hybrid/hybrid_search.h/.cpp` | Legacy hybrid command (kept for backward compatibility) |
| `src/summarizer/summarizer.h/.cpp` | Extractive summarization (stop words from language.conf) |
| `src/tools/sandbox.h/.cpp` | Sandboxed command execution |

---

## 7. File Structure

```
v.0.1.4/
+-- config/
|   +-- default.conf               # 80+ tunable parameters
|   +-- vocabularies/
|       +-- properties.conf         # per-property words (chunk/query/validate/synth)
|       +-- pipeline_rules.conf     # self-ask, dependencies, preferred chunks, hints
|       +-- domains.conf            # evidence domain keywords, negations, opposites
|       +-- language.conf           # stop words, non-entity words, training templates
+-- data/                           # 21 text documents across 7 topics
+-- src/
|   +-- answer/                     # synthesis, validation, planning, compression, scope
|   +-- chunk/                      # document chunking and classification
|   +-- cli/                        # command-line interface
|   +-- common/                     # config, vocab_loader, rules_loader, varint
|   +-- conversation/               # cross-process conversation memory
|   +-- embedding/                  # BoW embedding model, vocabulary
|   +-- encoder/                    # neural Transformer encoder (optional, libtorch)
|   +-- hnsw/                       # HNSW vector index
|   +-- hybrid/                     # legacy hybrid command
|   +-- inverted/                   # tokenizer, BM25, boolean search, phrase matching
|   +-- query/                      # InformationNeed model, query analyzers
|   +-- profiling/                   # Profiler, ScopeTimer (opt-in performance data)
|   +-- pipeline/                    # Pipeline struct, PipelineBuilder, run() logic
|   +-- retrieval/                  # IRetriever interface, BM25/HNSW/Hybrid retrievers, EmbeddingIndex, factory
|   +-- storage/                    # binary segment reader/writer, WAL, manifest
|   +-- summarizer/                 # extractive summarization
|   +-- tools/                      # sandboxed command execution
+-- tests/
|   +-- test_*.cpp                  # 76+ GoogleTest unit tests
|   +-- test_qa_integration.sh      # 75 QA integration tests
+-- CMakeLists.txt
+-- build.md
+-- history.md
+-- codebase_analysis.md
+-- todo.md
```

---

## 8. Build Configurations

| Configuration | Description |
|---------------|-------------|
| Default (no torch) | Core library + BoW embeddings + rule-based analyzer |
| USE_TORCH=ON | + Neural sentence encoder + neural query analyzer |
| BUILD_TESTS=ON | + GoogleTest unit tests |

---

## 9. Test Coverage

| Test Type | Count | What It Tests |
|-----------|-------|---------------|
| GoogleTest unit tests | 76+ | Varint, tokenization, query parsing, segment I/O, BM25, boolean/phrase search, HNSW recall, hybrid search |
| QA integration tests | 75 | Property detection, answer content, validation, multi-need, definition quality, compression, edge cases |

---

## 10. Adding a New Retrieval Algorithm

This is the primary extension point in v.0.1.4. Three files, zero pipeline changes:

### Step 1: Implement IRetriever

```cpp
// src/retrieval/my_retriever.h
#pragma once
#include "i_retriever.h"
#include "../storage/segment_reader.h"

class MyRetriever : public IRetriever {
public:
    explicit MyRetriever(SegmentReader& reader) : reader_(reader) {
        // Initialize your algorithm here
    }

    std::vector<ScoredDoc> search(
        const std::vector<std::string>& keywords) override
    {
        // Your retrieval logic here
        // Return ranked (docId, score) pairs
    }

    std::string name() const override { return "my_algo"; }

    // Optional: if your algorithm has a simpler fallback mode
    // bool supports_fallback() const override { return true; }
    // std::vector<ScoredDoc> fallback_search(...) override { ... }

private:
    SegmentReader& reader_;
};
```

### Step 2: Register in Factory

```cpp
// In src/retrieval/retriever_factory.h, add one branch:
if (type == "my_algo")
    return std::make_unique<MyRetriever>(reader);
```

### Step 3: Set Config

```
# In config/default.conf:
retrieval.retriever = my_algo
```

### That's it.

- No changes to `commands.cpp`
- No changes to the answer pipeline
- No changes to any other module
- All 75 integration tests still pass (they test answer quality, not retrieval internals)
