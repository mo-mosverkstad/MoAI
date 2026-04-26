# MoAI v.0.1.3 — Codebase Analysis

## 1. System Overview

MoAI is a fully self-contained, offline question-answering system written in C++20. It ingests text documents, builds a binary search index, and answers natural language questions using a cognitive **InformationNeed** model — without relying on LLMs or external search libraries.

The system operates at the **information-need level**: instead of classifying questions by interrogative words (where/what/who), it models what information the user actually needs (entity + property + answer form + answer scope).

All tuning parameters are externalized to `config/default.conf` — scoring weights, thresholds, limits, and fusion ratios can be adjusted at runtime without rebuilding.

---

## 2. High-Level Architecture

```
                         +----------------------+
                         |      User Query      |
                         |  [--brief/--detailed] |
                         +----------+-----------+
                                    |
                    +---------------v---------------+
                    |       QueryAnalyzer           |  Phase 1
                    |  (rule-based or neural)        |
                    |  Query -> InformationNeed[]    |
                    |  + AnswerScope inference       |
                    +---------------+---------------+
                                    |
                    +---------------v---------------+
                    |     ConversationState          |  Phase 3
                    |  (resolve follow-ups)          |
                    |  File-persisted entity memory  |
                    +---------------+---------------+
                                    |
                    +---------------v---------------+
                    |   SelfAsk + QuestionPlanner    |  Phase 3
                    |  Expand sub-needs              |
                    |  CLI scope override applied    |
                    |  Topological dependency sort   |
                    +---------------+---------------+
                                    |
              +---------------------v---------------------+
              |            Retrieval Layer                  |  Phase 2
              |                                            |
              |  +----------+    +----------+              |
              |  |   BM25   |    |   HNSW   |              |
              |  | (lexical)|    |  (vector) |              |
              |  +----+-----+    +----+-----+              |
              |       +------+-------+                     |
              |              | Score Fusion                 |
              |              | (configurable weights)      |
              |              v                              |
              |     Ranked Documents                        |
              |              |                              |
              |     +--------v--------+                    |
              |     |    Chunker      |                    |
              |     | split + classify|                    |
              |     | + select_chunks |                    |
              |     +--------+--------+                    |
              |              |                              |
              |     Property-typed Evidence                 |
              +---------------------+---------------------+
                                    |
              +---------------------v---------------------+
              |         Answer Layer                        |  Phase 2/3
              |                                            |
              |  +------------------+  +----------------+  |
              |  |AnswerSynthesizer |  |AnswerValidator |  |
              |  | 11 typed methods |  | self-ask check |  |
              |  | property dispatch|  | signal words   |  |
              |  | scope truncation |  |                |  |
              |  +--------+---------+  +-------+--------+  |
              |           |                    |            |
              |  +--------v--------------------v--------+  |
              |  |     EvidenceNormalizer                |  |
              |  |  NormalizedClaim-based                |  |
              |  |  agreement + contradiction detection  |  |
              |  |  refined multi-factor confidence      |  |
              |  +------------------+-------------------+  |
              |                     |                       |
              |  +------------------v-------------------+  |
              |  |     AgreementCompressor              |  |
              |  |  NONE / LIGHT / STRONG               |  |
              |  |  post-synthesis sentence reduction    |  |
              |  +------------------+-------------------+  |
              |                     |                       |
              |  Confidence-based scope adjustment          |
              |  Re-apply scope truncation                  |
              +---------------------+---------------------+
                                    |
              +---------------------v-----------+
              |     ConversationState            |  Phase 3
              |  update() + save() to file      |
              +---------------------+-----------+
                                    |
                    +---------------v---------+
                    |  JSON / Text Output     |
                    |  (+ compression metadata)|
                    +-------------------------+

All numeric parameters in the pipeline are read from config/default.conf at startup.
```

---

## 3. Runtime Configuration

All tuning parameters are externalized to `config/default.conf`, a key=value file loaded once at startup by a Config singleton. Modules access values via typed getters with baked-in defaults.

### Config Categories

| Category | Keys | Controls |
|----------|------|----------|
| `bm25.*` | k1, b, top_k | BM25 ranking parameters |
| `hnsw.*` | M, ef_construction, ef_search | HNSW graph parameters |
| `retrieval.*` | bm25_weight, ann_weight, max_ranked_docs, max_evidence | Hybrid fusion and limits |
| `chunk.*` | max_per_doc, primary/secondary/fallback_type_boost, keyword_boost, min_chunk_size | Chunk selection scoring |
| `scope.*` | strict/normal/expanded_max_chars, strict/normal/expanded_max_segments, high/low_confidence_threshold | Answer scope limits |
| `compression.*` | min_confidence, min_agreement, strong thresholds, light thresholds | Agreement-based compression |
| `confidence.*` | coverage/volume/agreement/penalty weights, max_contradiction_penalty, volume_divisor | Refined confidence formula |
| `synth.*` | keyword_score, entity_score, boost values, penalties, max text lengths | Synthesizer scoring |
| `proximity.*` | definition/location/advantages/history entity proximity thresholds | Entity proximity filters |
| `validator.*` | signal_ratio_fail/weak, confidence multipliers, support_coverage_threshold | Validation thresholds |

### Config Class

```cpp
class Config {
public:
    static Config& instance();
    bool load(const std::string& path);
    double get_double(const std::string& key, double default_val) const;
    int get_int(const std::string& key, int default_val) const;
    size_t get_size(const std::string& key, size_t default_val) const;
};
```

The synthesizer caches all config values in a static `SynthConfig` struct at first use to avoid per-call map lookups.

### Config Access Pattern

Each module has a small cached struct that loads its config values once at first use:

```cpp
struct ModuleConfig {
    double param1, param2;
    static const ModuleConfig& get() {
        static ModuleConfig mc = []() {
            auto& c = Config::instance();
            return ModuleConfig{ c.get_double("key1", default1), ... };
        }();
        return mc;
    }
};
```

| Module | Struct | Values Cached |
|--------|--------|---------------|
| `answer_scope.cpp` | `ScopeConfig` | 8 |
| `answer_compressor.cpp` | `CompressorConfig` | 8 |
| `answer_validator.cpp` | `ValidatorConfig` | 11 |
| `answer_synthesizer.cpp` | `SynthConfig` | 30 |
| `chunker.cpp` | `ChunkConfig` | 5 |
| `commands.cpp` | local variables | 7 |

The `Config` singleton is only accessed inside these cached structs and at startup — never in hot paths.

### Usage

```bash
# Edit config — no rebuild needed
vim config/default.conf

# Edit vocabularies — no rebuild needed
vim config/vocabularies/chunk_signals.conf
# Add: metropolitan   (to [LOCATION] section)
# Run: new word immediately active
```

### Vocabulary Files

All word lists are externalized to `config/vocabularies/`:

| File | Sections | Used By |
|------|----------|--------|
| `chunk_signals.conf` | 12 ChunkType signal lists | `chunker.cpp` (ChunkVocab) |
| `validator_signals.conf` | 9 Property signal lists | `answer_validator.cpp` (ValidatorConfig) |
| `synth_words.conf` | 14 scoring word lists | `answer_synthesizer.cpp` (SynthVocab) |
| `evidence_domains.conf` | [DOMAINS] type list + domain vocabs + negations + 14 opposite pairs | `evidence_normalizer.cpp` (EvidenceVocab) |
| `planning_rules.conf` | 7 self-ask rules + 7 dependency rules + 10 preferred chunk mappings | `self_ask.cpp` + `question_planner.cpp` + `answer_synthesizer.cpp` + `chunker.cpp` (PlanningRules) |
| `query_prototypes.conf` | 10 property prototype lists | (prepared for query_analyzer.cpp) |
| `query_templates.conf` | 22 neural training templates (prefix/suffix/intent/answer_type) | `neural_query_analyzer.cpp` via PlanningRules |
| `stop_words.conf` | Stop words + non-entity words | `query_analyzer.cpp` (QueryVocab) + `neural_query_analyzer.cpp` |

Format: `[SECTION]` headers with comma-separated words. Opposites use `word1 | word2` pipe syntax. Loaded by `VocabLoader` — if a file is missing, a warning is logged to stderr. No inline defaults are duplicated in source code; the `.conf` files are the single source of truth.

---

## 4. Neural Learning Pipelines (Optional, requires libtorch)

### 4.1 Neural Sentence Encoder Training (`train_encoder`)

Improves retrieval by replacing BoW embeddings with a trained Transformer sentence encoder. Architecture: token embedding -> sinusoidal PE -> Transformer layers (4 heads, 2 layers) -> mean pooling -> L2 norm. Trained with InfoNCE contrastive loss (t=0.07) on query-document pairs.

```bash
./train_encoder --epochs 10 --dim 128
```

### 4.2 Neural Query Analyzer Training (`train-qa`)

Improves query analysis with a multi-task Transformer classifier: intent (5 classes) + answer type (7 classes) + BIO entity tagging. ~27K+ auto-generated training samples. Limitation: single InformationNeed per query (no multi-need decomposition).

```bash
./mysearch train-qa --epochs 30
```

### 4.3 Fallback Behavior

| Model File | Present | Absent |
|------------|---------|--------|
| `encoder.pt` | HNSW uses neural embeddings | HNSW uses BoW embeddings |
| `qa_model.pt` | Neural query analyzer (single-need) | Rule-based analyzer (multi-need) |

Both auto-detected at runtime. System degrades gracefully.


---

## 5. QA Pipeline Flow (ask command)

```
1. Config::instance().load("../config/default.conf")  -- at startup

2. QueryAnalyzer.analyze(query)
   +- Split query into clauses ("X and why Y")
   +- Extract entity per clause
   +- Score properties via semantic prototypes
   +- Infer scope from query wording + property default
   +- Output: InformationNeed[] with entity, property, form, scope, keywords

3. ConversationState.apply(needs)
   +- Propagate last entity to needs with empty entity

4. SelfAsk.expand(need) for each need
   +- Generate support sub-needs (ADVANTAGES->DEF+LOC, FUNCTION->DEF, etc.)

5. CLI scope override (--brief / --detailed)
   +- Force STRICT or EXPANDED on user-facing needs

6. QuestionPlanner.build(expanded_needs)
   +- Topological sort by dependency rules

7. For each need in plan order:
   a. BM25.search(keywords, cfg:bm25.top_k)
   b. HNSW.search(query_embedding, cfg:bm25.top_k)
   c. Score fusion: cfg:retrieval.bm25_weight * BM25 + cfg:retrieval.ann_weight * ANN
   d. Chunker.chunk_document() -> typed paragraphs
   e. Chunker.select_chunks(cfg:chunk.max_per_doc) with configurable boosts
   f. SelfAsk.check_support_coverage(cfg:validator.support_coverage_threshold)
   g. AnswerSynthesizer.synthesize() with SynthConfig scoring weights
   h. EvidenceNormalizer -> agreement/contradiction analysis
   i. Refined confidence with configurable weights
   j. AgreementCompressor.compress() with configurable thresholds
   k. Scope adjustment (cfg:scope.high/low_confidence_threshold)
   l. Scope truncation (cfg:scope.*_max_chars)
   m. AnswerValidator.validate() with configurable signal ratios
   n. BM25-only retry if validation fails

8. Output JSON (with compression field) or plain text
```

---

## 6. Module Descriptions

### 6.1 Configuration (`src/common/`)

| File | Purpose |
|------|---------|
| `config.h/.cpp` | Config singleton: loads key=value file, typed getters with defaults |
| `vocab_loader.h/.cpp` | VocabLoader: parses [SECTION]/comma vocabulary files with fallback defaults |
| `rules_loader.h/.cpp` | PlanningRules: parses self-ask and dependency rules from planning_rules.conf |
| `varint.h/.cpp` | Variable-length integer encoding for compact index storage |
| `file_utils.h/.cpp` | File I/O helpers |
| `types.h` | Type aliases: DocID, TermID, Offset |

### 6.2 Storage Layer (`src/storage/`)

| File | Purpose |
|------|---------|
| `segment_writer.h/.cpp` | Writes inverted index segments: term dictionary + postings + doc texts |
| `segment_reader.h/.cpp` | Reads segments: postings lookup, doc text retrieval, term enumeration |
| `manifest.h/.cpp` | Tracks segment metadata |
| `wal.h/.cpp` | Write-ahead log for crash recovery |

### 6.3 Inverted Index (`src/inverted/`)

| File | Purpose |
|------|---------|
| `tokenizer.h/.cpp` | Splits text into lowercase alphanumeric tokens |
| `index_builder.h/.cpp` | Ingests directory of text files -> builds segment |
| `index_reader.h/.cpp` | Low-level postings list access |
| `bm25.h/.cpp` | Okapi BM25 ranking (k1, b configurable) |
| `query_parser.h/.cpp` | Parses boolean queries (AND/OR/NOT) and phrase queries |
| `phrase_matcher.h/.cpp` | Position-aware phrase matching |
| `search_engine.h/.cpp` | Full boolean + phrase search with BM25 scoring |

### 6.4 Vector Index (`src/hnsw/`)

| File | Purpose |
|------|---------|
| `hnsw_index.h/.cpp` | Full HNSW graph (M, efConstruction, efSearch configurable) |
| `hnsw_node.h/.cpp` | Node structure with per-layer neighbor lists |

### 6.5 Embedding (`src/embedding/`)

| File | Purpose |
|------|---------|
| `vocab.h/.cpp` | Vocabulary: term <-> integer ID mapping |
| `embedding_model.h/.cpp` | BoW feedforward net: input -> ReLU hidden -> L2-normalized output |

### 6.6 Neural Encoder (`src/encoder/`, requires libtorch)

| File | Purpose |
|------|---------|
| `sentence_encoder.h/.cpp` | Transformer encoder with sinusoidal PE, mean pooling, L2 norm |
| `encoder_trainer.h/.cpp` | InfoNCE contrastive training on query-document pairs |
| `train_main.cpp` | Training entry point |

### 6.7 Query Analysis (`src/query/`)

| File | Purpose |
|------|---------|
| `information_need.h` | Core data model: Property (11), AnswerForm (5), AnswerScope (3), InformationNeed struct |
| `query_analyzer.h/.cpp` | Rule-based: clause splitting, semantic prototype scoring, entity extraction, scope inference |
| `neural_query_analyzer.h/.cpp` | Neural: multi-task Transformer (requires libtorch) |

### 6.8 Chunking (`src/chunk/`)

| File | Purpose |
|------|---------|
| `chunker.h/.cpp` | Split documents into paragraphs, classify by ChunkType via ChunkVocab (loaded from chunk_signals.conf), keyword+type-aware selection with configurable boosts |

### 6.9 Answer Synthesis (`src/answer/`)

| File | Purpose |
|------|---------|
| `answer_scope.h/.cpp` | Scope policy functions with ScopeConfig cache |
| `answer_compressor.h/.cpp` | Agreement-based compression with CompressorConfig cache |
| `answer_synthesizer.h/.cpp` | 11 typed synthesizers with SynthConfig + SynthVocab (scoring words loaded from synth_words.conf) |
| `answer_validator.h/.cpp` | Self-ask validation with signal words loaded from validator_signals.conf via ValidatorConfig |
| `evidence_normalizer.h/.cpp` | NormalizedClaim model, domain keywords and opposites loaded from evidence_domains.conf via EvidenceVocab |
| `question_planner.h/.cpp` | Dependency-aware topological sort, rules loaded from planning_rules.conf via PlanningRules |
| `self_ask.h/.cpp` | Sub-question generation, expansion rules loaded from planning_rules.conf via PlanningRules |

### 6.10 Conversation (`src/conversation/`)

| File | Purpose |
|------|---------|
| `conversation_state.h/.cpp` | Entity/property carry-over, file-based persistence |

### 6.11 Hybrid Search (`src/hybrid/`)

| File | Purpose |
|------|---------|
| `hybrid_builder.h/.cpp` | Bootstrap: build vocab + BoW model + HNSW from segment |
| `hybrid_search.h/.cpp` | BM25 + ANN fusion search with summarization |

### 6.12 Other (`src/summarizer/`, `src/tools/`)

| File | Purpose |
|------|---------|
| `summarizer.h/.cpp` | Keyword-scored extractive summarization |
| `sandbox.h/.cpp` | Sandboxed command execution |

---

## 7. Key Algorithms and Concepts

### 7.1 BM25

```
score(Q, D) = S IDF(qi) * (tf(qi,D) * (k1+1)) / (tf(qi,D) + k1*(1-b+b*|D|/avgdl))
```

Parameters `k1` and `b` configurable via `bm25.k1` and `bm25.b`.

### 7.2 HNSW

Multi-layer navigable small world graph for approximate nearest neighbor search. Parameters `M`, `efConstruction`, `efSearch` configurable via `hnsw.*`.

### 7.3 Varint + Delta Encoding

Postings lists compressed with variable-length integers and delta-encoded document IDs. Positions also delta-encoded.

### 7.4 Semantic Prototype Scoring

Property detection via weighted keyword matching against prototype vocabularies. Highest-scoring property wins.

### 7.5 Chunk Classification and Selection

Paragraphs classified into 11 ChunkTypes by keyword signals. Selection uses three-tier boost:
- Primary type: `chunk.primary_type_boost` (default 10.0)
- Secondary type: `chunk.secondary_type_boost` (default 6.0)
- Fallback type: `chunk.fallback_type_boost` (default 1.0)

### 7.6 Hybrid Score Fusion

```
fused = cfg:retrieval.bm25_weight * BM25_norm + cfg:retrieval.ann_weight * ANN_norm
```

Only BM25-found documents are fused (ANN-only discarded).

### 7.7 Evidence Agreement and Contradiction

NormalizedClaim-based analysis with 4 domain vocabularies (60+ keywords) and 14 directional opposite pairs. Contradiction penalty configurable via `confidence.contradiction_penalty_per_pair` and `confidence.max_contradiction_penalty`.

### 7.8 Refined Confidence

```
confidence = cfg:confidence.coverage_weight * coverage
           + cfg:confidence.volume_weight * volume
           + cfg:confidence.agreement_weight * agreement
           + cfg:confidence.penalty_weight * (1 - penalty)
```

### 7.9 AnswerScope

Controls answer length orthogonal to Property. Three levels with configurable char/segment limits:

| Scope | Config Key | Default |
|-------|-----------|---------|
| STRICT | `scope.strict_max_chars` | 200 |
| NORMAL | `scope.normal_max_chars` | 400 |
| EXPANDED | `scope.expanded_max_chars` | 700 |

Scope resolution: CLI flag > query wording > property default > confidence adjustment.

### 7.10 Agreement-Based Compression

Post-synthesis compression with configurable thresholds. Decision rules use `compression.*` config keys. STRICT never compressed. STRONG = first sentence only. LIGHT = first 2 sentences.

### 7.11 Definition Synthesis

Two-pass approach with configurable scoring:
- First pass: score DEFINITION+LOCATION chunks using `synth.definition_first_pass_*` weights
- Fallback: segment scoring using `synth.keyword_score`, `synth.entity_subject_boost`, `synth.entity_is_pattern_boost`
- Entity proximity controlled by `proximity.definition_first_pass` and `proximity.definition_fallback_subject`

### 7.12 Self-Ask Expansion

| Need Property | Generated Sub-Needs |
|---------------|-------------------|
| ADVANTAGES | DEFINITION + LOCATION |
| FUNCTION | DEFINITION |
| LIMITATIONS | USAGE |
| HISTORY | TIME + DEFINITION |
| COMPARISON | DEFINITION |

### 7.13 Question Planning

Topological sort with explicit dependency rules. Prior answers passed as context to dependent needs.

### 7.14 InfoNCE Contrastive Loss (Neural)

Optional neural encoder trained with in-batch InfoNCE loss (t=0.07).

### 7.15 Neural Query Analyzer (Neural)

Optional multi-task Transformer: intent (5) + answer type (7) + BIO entity (3 tags).

---

## 8. Data Flow: Ingestion

```
Text files (data/)
  |
IndexBuilder.ingest_directory()
  |
Tokenizer.tokenize() -> lowercase tokens
  |
SegmentWriter.add_document() -> collect postings + positions
  |
SegmentWriter.finalize() -> write binary files:
  +- terms.bin, postings.bin, docs.bin, rawdocs.bin
```


---

## 9. Data Flow: Hybrid Index Build

### BoW Path (default)

```
SegmentReader -> Vocabulary.build_from_terms() -> EmbeddingModel.init_random()
  -> For each doc: Tokenize -> BoW -> embed -> HNSWIndex.add_point()
  -> Save: vocab.txt + model.bin
```

### Neural Path (after train_encoder)

```
SegmentReader -> Vocabulary.load() -> EncoderTrainer.load(encoder.pt)
  -> For each doc: Full text -> Transformer encode -> L2-norm -> HNSWIndex.add_point()
```

---

## 10. File Structure

```
v.0.1.3/
+-- config/
|   +-- default.conf           # 80+ tunable parameters
|   +-- vocabularies/          # externalized word lists
|       +-- chunk_signals.conf
|       +-- validator_signals.conf
|       +-- synth_words.conf
|       +-- evidence_domains.conf
|       +-- planning_rules.conf
|       +-- query_prototypes.conf
|       +-- query_templates.conf
|       +-- stop_words.conf
+-- data/                      # 21 text documents across 7 topics
|   +-- biology/               # animals, plants
|   +-- computer_science/      # algorithms, databases, networking, security, python, blockchain
|   +-- economics/             # inflation
|   +-- engineering/           # solar energy, electric vehicles
|   +-- geography/             # japan, mars, climate change
|   +-- health/                # antibiotics
|   +-- misc/                  # AI overview, history of computing
|   +-- sweden/                # stockholm, gothenburg, malmo
+-- src/
|   +-- answer/                # synthesis, validation, planning, self-ask, compression, scope
|   +-- chunk/                 # document chunking and classification
|   +-- cli/                   # command-line interface (main, commands)
|   +-- common/                # config, types, varint, file utils
|   +-- conversation/          # cross-process conversation memory
|   +-- embedding/             # BoW embedding model, vocabulary
|   +-- encoder/               # neural Transformer encoder (optional, libtorch)
|   +-- hnsw/                  # HNSW vector index
|   +-- hybrid/                # BM25+HNSW fusion search
|   +-- inverted/              # tokenizer, BM25, boolean search, phrase matching
|   +-- query/                 # InformationNeed model, query analyzer (rule + neural)
|   +-- storage/               # binary segment reader/writer, WAL, manifest
|   +-- summarizer/            # extractive summarization
|   +-- tools/                 # sandboxed command execution
+-- tests/
|   +-- test_*.cpp             # 76+ GoogleTest unit tests
|   +-- test_qa_integration.sh # 75 QA integration tests
+-- CMakeLists.txt
+-- build.md
+-- history.md
+-- codebase_analysis.md
```

---

## 11. Build Configurations

| Configuration | Description |
|---------------|-------------|
| Default (no torch) | Core library + BoW embeddings + rule-based analyzer |
| USE_TORCH=ON | + Neural sentence encoder + neural query analyzer |
| BUILD_TESTS=ON | + GoogleTest unit tests |

Zero external dependencies for the core library. Only libtorch is optional.

---

## 12. Test Coverage

| Test Type | Count | What It Tests |
|-----------|-------|---------------|
| GoogleTest unit tests | 76+ | Varint encoding, tokenization, query parsing, segment I/O, BM25, boolean/phrase search, HNSW recall, hybrid search |
| QA integration tests | 75 | Property detection, answer content, validation status, multi-need decomposition, definition quality, compression verification, edge cases across 7 topic areas |

### Integration Test Sections (75 tests)

| Section | Tests | What It Covers |
|---------|-------|----------------|
| Sweden | 4 | Location, implicit location, multi-need |
| Databases | 4 | Definition, limitations, usage, advantages |
| Networking | 3 | Function, explanation, temporal |
| Physics | 5 | Definition, function, history, advantages, limitations |
| Solar Energy | 4 | Definition, function, advantages, limitations |
| Japan | 3 | Location, advantages, limitations |
| Python | 4 | Definition, advantages, limitations, usage |
| Climate Change | 3 | Definition, function, history |
| Multi-need and Cross-topic | 3 | TCP def+function, algorithm scalability |
| Validation | 3 | NoSQL, Stockholm, Japan validation status |
| Antibiotics | 6 | Definition, function, history, advantages, limitations, usage |
| Blockchain | 5 | Definition, function, advantages, limitations, history |
| Inflation | 4 | Definition, function, limitations, history |
| Mars | 5 | Location, composition, advantages, limitations, history |
| Electric Vehicles | 6 | Definition, function, advantages, limitations, comparison, history |
| Definition Quality | 5 | Stockholm "capital,sweden", database, Python, blockchain, EV definitions |
| Compression | 3 | STRICT->NONE, DEFINITION STRICT->NONE, NORMAL high-confidence->STRONG |
| Edge Cases | 5 | Polite prefix, no interrogative, implicit advantages/definition, short query |
