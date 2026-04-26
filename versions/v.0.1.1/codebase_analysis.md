# MoAI v.0.1.1 — Codebase Analysis

## 1. System Overview

MoAI is a fully self-contained, offline question-answering system written in C++20. It ingests text documents, builds a binary search index, and answers natural language questions using a cognitive **InformationNeed** model — without relying on LLMs or external search libraries.

The system operates at the **information-need level**: instead of classifying questions by interrogative words (where/what/who), it models what information the user actually needs (entity + property + answer form).

---

## 2. High-Level Architecture

```
                         ┌──────────────────────┐
                         │      User Query       │
                         └──────────┬───────────┘
                                    │
                    ┌───────────────▼───────────────┐
                    │       QueryAnalyzer           │  Phase 1
                    │  (rule-based or neural)        │
                    │  Query → InformationNeed[]     │
                    └───────────────┬───────────────┘
                                    │
                    ┌───────────────▼───────────────┐
                    │     ConversationState          │  Phase 3
                    │  (resolve follow-ups)          │
                    │  File-persisted entity memory  │
                    └───────────────┬───────────────┘
                                    │
                    ┌───────────────▼───────────────┐
                    │   SelfAsk + QuestionPlanner    │  Phase 3
                    │  Expand sub-needs              │
                    │  Topological dependency sort   │
                    └───────────────┬───────────────┘
                                    │
              ┌─────────────────────▼─────────────────────┐
              │            Retrieval Layer                  │  Phase 2
              │                                            │
              │  ┌──────────┐    ┌──────────┐              │
              │  │   BM25   │    │   HNSW   │              │
              │  │ (lexical)│    │  (vector) │              │
              │  └────┬─────┘    └────┬─────┘              │
              │       └──────┬───────┘                     │
              │              │ Score Fusion (0.7/0.3)      │
              │              ▼                              │
              │     Ranked Documents                        │
              │              │                              │
              │     ┌────────▼────────┐                    │
              │     │    Chunker      │                    │
              │     │ split + classify│                    │
              │     │ + select_chunks │                    │
              │     └────────┬────────┘                    │
              │              │                              │
              │     Property-typed Evidence                 │
              └──────────────┬────────────────────────────┘
                             │
              ┌──────────────▼────────────────────────────┐
              │         Answer Layer                        │  Phase 2/3
              │                                            │
              │  ┌──────────────────┐  ┌────────────────┐  │
              │  │AnswerSynthesizer │  │AnswerValidator │  │
              │  │ 11 typed methods │  │ self-ask check │  │
              │  │ property dispatch│  │ signal words   │  │
              │  └────────┬─────────┘  └───────┬────────┘  │
              │           │                    │            │
              │  ┌────────▼────────────────────▼────────┐  │
              │  │     EvidenceNormalizer                │  │
              │  │  NormalizedClaim-based                │  │
              │  │  agreement + contradiction detection  │  │
              │  │  refined multi-factor confidence      │  │
              │  └──────────────────────────────────────┘  │
              └──────────────┬────────────────────────────┘
                             │
              ┌──────────────▼───────────────┐
              │     ConversationState         │  Phase 3
              │  update() + save() to file   │
              └──────────────┬───────────────┘
                             │
                    ┌────────▼────────┐
                    │  JSON / Text    │
                    │    Output       │
                    └─────────────────┘
```

---

## 3. Neural Learning Pipelines (Optional, requires libtorch)

```
 ┌─────────────────────────────────────────────────────────────┐
 │                  Neural Learning Layer                       │
 │                  (offline training, optional)                │
 │                                                             │
 │  ┌─────────────────────────┐  ┌──────────────────────────┐  │
 │  │  Neural Sentence Encoder│  │ Neural Query Analyzer    │  │
 │  │  (train_encoder)        │  │ (train-qa)               │  │
 │  │                         │  │                          │  │
 │  │  Transformer + InfoNCE  │  │ Multi-task Transformer   │  │
 │  │  contrastive learning   │  │ intent + answer_type     │  │
 │  │  on query-doc pairs     │  │ + BIO entity tagging     │  │
 │  │                         │  │                          │  │
 │  │  Output: encoder.pt     │  │ Output: qa_model.pt      │  │
 │  │  Improves: HNSW vector  │  │ Improves: QueryAnalyzer  │  │
 │  │  retrieval (Phase 2)    │  │ property/entity (Phase 1)│  │
 │  └─────────────────────────┘  └──────────────────────────┘  │
 └─────────────────────────────────────────────────────────────┘
         │                                │
         │ encoder.pt auto-detected       │ qa_model.pt auto-detected
         ▼                                ▼
  ┌──────────────┐                ┌───────────────┐
  │ HNSW uses    │                │ QueryAnalyzer │
  │ neural embeds│                │ uses neural   │
  │ instead of   │                │ instead of    │
  │ BoW vectors  │                │ rule-based    │
  └──────────────┘                └───────────────┘
```

### 3.1 Neural Sentence Encoder Training (`train_encoder`)

**Purpose**: Improves the **retrieval** stage (Phase 2) by replacing the random BoW embedding model with a trained Transformer sentence encoder.

**Impact**:
- The HNSW vector index uses semantically meaningful embeddings instead of bag-of-words vectors
- Documents that are semantically similar to the query (even without exact keyword overlap) rank higher
- Better document ranking for paraphrased or implicit queries like "Is Stockholm close to the sea?"

**Architecture**:
```
Corpus documents
  ↓
Generate (query, document) pairs
  ↓
Transformer Encoder:
  Token embedding → Sinusoidal positional encoding
  → N Transformer encoder layers (4 heads, 2 layers)
  → Mean pooling over token outputs
  → L2 normalization
  ↓
InfoNCE contrastive loss:
  L = -log(exp(sim(q, d+) / τ) / Σ exp(sim(q, di) / τ))
  (τ = 0.07, in-batch negatives)
  ↓
AdamW optimizer, save encoder.pt
```

**Runtime**: When `encoder.pt` exists, the `ask` command automatically loads the neural encoder, builds HNSW with neural embeddings, and encodes queries using the neural encoder for ANN search.

```bash
./train_encoder --epochs 10 --dim 128
```

### 3.2 Neural Query Analyzer Training (`train-qa`)

**Purpose**: Improves the **query analysis** stage (Phase 1) by replacing the rule-based property/entity detection with a learned neural multi-task classifier.

**Architecture**:
```
Query text
  ↓
Token embedding → Sinusoidal positional encoding
  → N Transformer encoder layers (4 heads, 2 layers)
  ↓
┌─────────────────┬──────────────────┬─────────────────┐
│  Intent Head    │ Answer Type Head │  Entity Head    │
│  (5 classes)    │ (7 classes)      │  (BIO tagging)  │
│  mean-pooled    │ mean-pooled      │  per-token      │
│  → Linear(5)   │ → Linear(7)      │  → Linear(3)   │
└────────┬────────┴────────┬─────────┴────────┬────────┘
         │                 │                  │
    FACTUAL/          LOCATION/           B-entity
    EXPLANATION/      DEFINITION/         I-entity
    PROCEDURAL/       PERSON/             O (outside)
    COMPARISON/       TEMPORAL/
    GENERAL           PROCEDURE/
                      COMPARISON/
                      SUMMARY
```

**Training data**: ~27K+ samples auto-generated from the corpus using entity extraction and 22 question templates (e.g., "where is {entity}", "what is {entity}", "who invented {entity}").

**Impact**:
- Better entity extraction via learned BIO tagging (vs longest-keyword heuristic)
- Better property detection for unusual phrasings
- **Limitation**: Currently produces a single InformationNeed per query — does not support multi-need decomposition

```bash
./mysearch train-qa --epochs 30
```

### 3.3 Fallback Behavior

Both neural models are optional and auto-detected at runtime:

| Model File | Present | Absent |
|------------|---------|--------|
| `encoder.pt` | HNSW uses neural embeddings | HNSW uses BoW embeddings |
| `qa_model.pt` | Neural query analyzer (single-need) | Rule-based analyzer (multi-need) |
| Both absent | Full rule-based pipeline | |
| Both present | Neural retrieval + neural analysis | |

The system degrades gracefully — all features work without libtorch, just with lower quality.

---

## 4. QA Pipeline Flow (ask command)

```
1. QueryAnalyzer.analyze(query)
   ├─ Split query into clauses ("X and why Y")
   ├─ Extract entity per clause (skip non-entity words)
   ├─ Score properties via semantic prototypes
   └─ Output: InformationNeed[] with entity, property, form, keywords

2. ConversationState.apply(needs)
   └─ Propagate last entity to needs with empty entity

3. SelfAsk.expand(need) for each need
   ├─ ADVANTAGES → add DEFINITION + LOCATION sub-needs
   ├─ FUNCTION → add DEFINITION sub-need
   ├─ LIMITATIONS → add USAGE sub-need
   ├─ HISTORY → add TIME + DEFINITION sub-needs
   └─ Mark sub-needs as is_support=true

4. QuestionPlanner.build(expanded_needs)
   ├─ Explicit dependency rules (HISTORY→LOCATION, FUNCTION→DEFINITION, etc.)
   └─ Topological sort for execution order

5. For each need in plan order:
   a. BM25.search(keywords, 10) → ranked documents
   b. HNSW.search(query_embedding, 10) → ANN candidates
   c. Score fusion: 0.7 * BM25_norm + 0.3 * ANN_norm (BM25-found docs only)
   d. Chunker.chunk_document() → split into typed paragraphs
   e. Chunker.select_chunks(property, keywords) → top 5 per doc
   f. SelfAsk.check_support_coverage() → evidence quality check
   g. AnswerSynthesizer.synthesize(need, evidence) → typed answer
   h. EvidenceNormalizer → NormalizedClaim[] → agreement/contradiction analysis
   i. AnswerValidator.validate() → self-ask signal word check
   j. If validation fails + hybrid: retry with BM25-only
   k. Store answer as prior_context for next need

6. ConversationState.save() → persist to file

7. Output JSON or plain text (support needs filtered out)
```

---

## 5. Module Descriptions

### 4.1 Storage Layer (`src/storage/`)

| File | Purpose |
|------|---------|
| `segment_writer.h/.cpp` | Writes inverted index segments: term dictionary + postings + doc texts |
| `segment_reader.h/.cpp` | Reads segments: postings lookup, doc text retrieval, term enumeration |
| `manifest.h/.cpp` | Tracks segment metadata |
| `wal.h/.cpp` | Write-ahead log for crash recovery |

**Binary format**: Terms are sorted alphabetically. Postings use varint + delta encoding for compact storage. Positions are stored for phrase matching.

### 4.2 Inverted Index (`src/inverted/`)

| File | Purpose |
|------|---------|
| `tokenizer.h/.cpp` | Splits text into lowercase alphanumeric tokens |
| `index_builder.h/.cpp` | Ingests directory of text files → builds segment |
| `index_reader.h/.cpp` | Low-level postings list access |
| `bm25.h/.cpp` | Okapi BM25 ranking (k1=1.2, b=0.75) |
| `query_parser.h/.cpp` | Parses boolean queries (AND/OR/NOT) and phrase queries |
| `phrase_matcher.h/.cpp` | Position-aware phrase matching |
| `search_engine.h/.cpp` | Full boolean + phrase search with BM25 scoring |

### 4.3 Vector Index (`src/hnsw/`)

| File | Purpose |
|------|---------|
| `hnsw_index.h/.cpp` | Full HNSW (Hierarchical Navigable Small World) graph |
| `hnsw_node.h/.cpp` | Node structure with per-layer neighbor lists |

**Parameters**: M=16, efConstruction=200, efSearch=100. L2 distance metric.

### 4.4 Embedding (`src/embedding/`)

| File | Purpose |
|------|---------|
| `vocab.h/.cpp` | Vocabulary: term ↔ integer ID mapping, save/load |
| `embedding_model.h/.cpp` | BoW feedforward net: input → ReLU hidden → L2-normalized output |

### 4.5 Neural Encoder (`src/encoder/`, requires libtorch)

| File | Purpose |
|------|---------|
| `sentence_encoder.h/.cpp` | Transformer encoder with sinusoidal PE, mean pooling, L2 norm |
| `encoder_trainer.h/.cpp` | InfoNCE contrastive training on query-document pairs |
| `train_main.cpp` | Training entry point |

### 4.6 Query Analysis (`src/query/`)

| File | Purpose |
|------|---------|
| `information_need.h` | Core data model: Property (11 values), AnswerForm (5 values), InformationNeed struct |
| `query_analyzer.h/.cpp` | Rule-based: clause splitting, semantic prototype scoring, entity extraction |
| `neural_query_analyzer.h/.cpp` | Neural: multi-task Transformer (intent + answer type + BIO entity), requires libtorch |

### 4.7 Chunking (`src/chunk/`)

| File | Purpose |
|------|---------|
| `chunker.h/.cpp` | Split documents into paragraphs, classify by ChunkType (11 types), keyword+type-aware selection |

### 4.8 Answer Synthesis (`src/answer/`)

| File | Purpose |
|------|---------|
| `answer_synthesizer.h/.cpp` | 11 typed synthesizers dispatched by Property, typed-chunk-first pattern |
| `answer_validator.h/.cpp` | Self-ask validation (signal words), evidence analysis, refined confidence |
| `evidence_normalizer.h/.cpp` | NormalizedClaim model, agreement scoring, contradiction detection |
| `question_planner.h/.cpp` | Dependency-aware topological sort of InformationNeeds |
| `self_ask.h/.cpp` | Internal sub-question generation and coverage checking |

### 4.9 Conversation (`src/conversation/`)

| File | Purpose |
|------|---------|
| `conversation_state.h/.cpp` | Entity/property carry-over, file-based persistence across processes |

### 4.10 Hybrid Search (`src/hybrid/`)

| File | Purpose |
|------|---------|
| `hybrid_builder.h/.cpp` | Bootstrap: build vocab + BoW model + HNSW from segment |
| `hybrid_search.h/.cpp` | BM25 + ANN fusion search with summarization |

### 4.11 Other (`src/summarizer/`, `src/tools/`, `src/common/`)

| File | Purpose |
|------|---------|
| `summarizer.h/.cpp` | Keyword-scored extractive summarization (used by `hybrid` command) |
| `sandbox.h/.cpp` | Sandboxed command execution |
| `varint.h/.cpp` | Variable-length integer encoding for compact index storage |
| `file_utils.h/.cpp` | File I/O helpers |
| `types.h` | Type aliases: DocID, TermID, Offset |

---

## 6. Key Algorithms and Concepts

### 5.1 BM25 (Best Matching 25)

The core lexical ranking algorithm. For a query Q and document D:

```
score(Q, D) = Σ IDF(qi) * (tf(qi, D) * (k1 + 1)) / (tf(qi, D) + k1 * (1 - b + b * |D| / avgdl))
```

Where:
- `IDF(qi) = log((N - df(qi) + 0.5) / (df(qi) + 0.5))` — inverse document frequency
- `tf(qi, D)` — term frequency of query term qi in document D
- `|D|` — document length, `avgdl` — average document length
- `k1 = 1.2` — term frequency saturation parameter
- `b = 0.75` — document length normalization parameter

Top-K extraction uses a min-heap for efficient ranking.

### 5.2 HNSW (Hierarchical Navigable Small World)

Approximate nearest neighbor search using a multi-layer graph:

- **Insertion**: New point is assigned a random level (exponential distribution). Connected to M nearest neighbors at each layer using greedy search from the entry point down.
- **Search**: Start at the top layer's entry point. Greedy search narrows to a single candidate at each upper layer. At layer 0, beam search with efSearch candidates produces the final result.
- **Parameters**: M=16 (max connections per node), M0=32 (layer 0), efConstruction=200, efSearch=100.
- **Distance**: L2 (Euclidean).

### 5.3 Varint + Delta Encoding

Postings lists are compressed using:
- **Varint**: Each integer uses 1-5 bytes. The high bit of each byte indicates continuation.
- **Delta encoding**: Document IDs are stored as differences from the previous ID, reducing values and improving varint compression.
- **Position storage**: Within each posting, positions are also delta-encoded.

### 5.4 Semantic Prototype Scoring

Property detection uses weighted keyword matching against prototype vocabularies:

```cpp
struct PropertyPrototype {
    Property property;
    std::vector<std::string> signals;  // e.g., {"located", "coast", "capital"}
    double weight;                      // e.g., 3.0
};
```

A query clause is scored against all prototypes. The highest-scoring property wins. Multiple properties can activate simultaneously (scores preserved in `property_score`).

### 5.5 Chunk Classification

At ingestion time, each document paragraph is classified into one of 11 ChunkTypes using keyword signals:

| ChunkType | Example Signals |
|-----------|----------------|
| LOCATION | located, coast, situated, eastern, western |
| DEFINITION | is a, refers to, defined as |
| ADVANTAGES | advantage, benefit, widely used, important for |
| LIMITATIONS | limitation, drawback, not suitable, vendor lock |
| FUNCTION | ensures, mechanism, handshake, flow control |
| USAGE | used for, beginner, recommend, learning path |
| HISTORY | history, founded, first described, first practical |
| TEMPORAL | century, 1947, invented, year |
| PERSON | born, inventor, alan turing |
| PROCEDURE | step, how to, algorithm, method |
| GENERAL | (fallback) |

### 5.6 Hybrid Score Fusion

BM25 and ANN scores are fused per document:

```
fused_score = 0.7 * (bm25_score / max_bm25) + 0.3 * ((cosine_sim + 1) / 2)
```

Only documents found by BM25 are considered (ANN-only results are discarded to prevent irrelevant documents from entering the evidence pool).

### 5.7 Evidence Agreement & Contradiction

Evidence chunks are normalized to `NormalizedClaim` with controlled keyword extraction from 4 domain vocabularies (60+ keywords).

**Agreement**: Two claims agree if same entity + same property + substantial keyword overlap + no negation mismatch. Score = keyword overlap ratio (Jaccard-like).

**Contradiction**: Two claims contradict if same entity + same property + different documents + (negation conflict OR directional opposites). 14 opposite pairs defined (east/west, cheap/expensive, etc.).

### 5.8 Refined Confidence

```
confidence = 0.3 * coverage      (keyword match ratio in evidence)
           + 0.2 * volume        (min(1.0, evidence_count / 3.0))
           + 0.3 * agreement     (average agreement score across claim pairs)
           + 0.2 * (1 - penalty) (contradiction penalty, max 0.4)
```

### 5.9 Self-Ask Expansion

Before answering, the system generates internal sub-questions:

| Need Property | Generated Sub-Needs |
|---------------|-------------------|
| ADVANTAGES | DEFINITION + LOCATION |
| COMPARISON | DEFINITION |
| LIMITATIONS | USAGE |
| HISTORY | TIME + DEFINITION |
| FUNCTION | DEFINITION |

Sub-needs are marked `is_support=true`, added to the plan, answered internally, but hidden from output.

### 5.10 Question Planning (Topological Sort)

Explicit dependency rules determine execution order:

```
HISTORY    depends on LOCATION
ADVANTAGES depends on LOCATION + DEFINITION
FUNCTION   depends on DEFINITION
LIMITATIONS depends on DEFINITION
COMPARISON depends on DEFINITION
USAGE      depends on DEFINITION
```

Needs are topologically sorted so prerequisites are answered first. Prior answers are passed as `prior_context` to dependent needs.

### 5.11 InfoNCE Contrastive Loss (Neural Encoder)

The optional neural sentence encoder is trained with in-batch InfoNCE loss:

```
L = -log(exp(sim(q, d+) / τ) / Σ exp(sim(q, di) / τ))
```

Where `sim` is cosine similarity, `d+` is the positive document, and `τ = 0.07` is the temperature. This learns embeddings where semantically similar query-document pairs are close in vector space.

### 5.12 Neural Query Analyzer (Multi-Task)

The optional neural query analyzer uses a Transformer encoder with three classification heads:
- **Intent head**: 5-class (FACTUAL, EXPLANATION, PROCEDURAL, COMPARISON, GENERAL)
- **Answer type head**: 7-class (LOCATION, DEFINITION, PERSON, TEMPORAL, PROCEDURE, COMPARISON, SUMMARY)
- **Entity head**: BIO tagging (3 tags per token: O, B-entity, I-entity)

Training data (~27K+ samples) is auto-generated from the corpus using entity extraction and 22 question templates.

---

## 7. Data Flow: Ingestion

```
Text files (data/)
  ↓
IndexBuilder.ingest_directory()
  ↓
Tokenizer.tokenize() → lowercase tokens
  ↓
SegmentWriter.add_document() → collect postings + positions
  ↓
SegmentWriter.finalize() → write binary files:
  ├─ terms.bin   (sorted term dictionary)
  ├─ postings.bin (varint+delta encoded posting lists)
  ├─ docs.bin    (document lengths)
  └─ rawdocs.bin (raw document texts)
```

---

## 8. Data Flow: Hybrid Index Build

### 8a. BoW Path (default, no libtorch)

```
SegmentReader (existing segment)
  ↓
Vocabulary.build_from_terms() → term↔ID mapping
  ↓
EmbeddingModel.init_random() → BoW feedforward net
  ↓
For each document:
  Tokenize → BoW vector → EmbeddingModel.embed() → dense vector
  ↓
HNSWIndex.add_point() → build navigable small world graph
  ↓
Save: vocab.txt + model.bin
```

### 8b. Neural Path (after train_encoder)

```
SegmentReader (existing segment)
  ↓
Vocabulary.load(vocab.txt)
  ↓
EncoderTrainer.load(encoder.pt) → trained Transformer
  ↓
For each document:
  Full text → Tokenize → Transformer encode → L2-normalized dense vector
  ↓
HNSWIndex.add_point() → build navigable small world graph
  (semantically meaningful vectors, not random BoW)
```

The neural path produces higher-quality embeddings where semantically similar documents cluster together, even without shared keywords.

---

## 9. File Structure

```
v.0.1.1/
├── data/                    # 21 text documents across 7 topics
│   ├── biology/             # animals, plants
│   ├── computer_science/    # algorithms, databases, networking, security, python, blockchain
│   ├── economics/           # inflation
│   ├── engineering/         # solar energy, electric vehicles
│   ├── geography/           # japan, mars, climate change
│   ├── health/              # antibiotics
│   ├── misc/                # AI overview, history of computing
│   └── sweden/              # stockholm, gothenburg, malmo
├── src/
│   ├── answer/              # synthesis, validation, planning, self-ask, evidence normalization
│   ├── chunk/               # document chunking and classification
│   ├── cli/                 # command-line interface (main, commands)
│   ├── common/              # types, varint, file utils
│   ├── conversation/        # cross-process conversation memory
│   ├── embedding/           # BoW embedding model, vocabulary
│   ├── encoder/             # neural Transformer encoder (optional, libtorch)
│   ├── hnsw/                # HNSW vector index
│   ├── hybrid/              # BM25+HNSW fusion search
│   ├── inverted/            # tokenizer, BM25, boolean search, phrase matching
│   ├── query/               # InformationNeed model, query analyzer (rule + neural)
│   ├── storage/             # binary segment reader/writer, WAL, manifest
│   ├── summarizer/          # extractive summarization
│   └── tools/               # sandboxed command execution
├── tests/
│   ├── test_*.cpp           # 76+ GoogleTest unit tests
│   └── test_qa_integration.sh  # 67 QA integration tests
├── CMakeLists.txt
├── build.md
└── history.md
```

---

## 10. Build Configurations

| Configuration | Description |
|---------------|-------------|
| Default (no torch) | Core library + BoW embeddings + rule-based analyzer |
| USE_TORCH=ON | + Neural sentence encoder + neural query analyzer |
| BUILD_TESTS=ON | + GoogleTest unit tests |

Zero external dependencies for the core library. Only libtorch is optional.

---

## 11. Test Coverage

| Test Type | Count | What It Tests |
|-----------|-------|---------------|
| GoogleTest unit tests | 76+ | Varint encoding, tokenization, query parsing, segment I/O, BM25, boolean/phrase search, HNSW recall, hybrid search |
| QA integration tests | 67 | Property detection, answer content, validation status, multi-need decomposition, edge cases across 7 topic areas |
