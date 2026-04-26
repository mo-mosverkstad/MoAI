# MoAI v.0.1.2 — Codebase Analysis

## 1. System Overview

MoAI is a fully self-contained, offline question-answering system written in C++20. It ingests text documents, builds a binary search index, and answers natural language questions using a cognitive **InformationNeed** model — without relying on LLMs or external search libraries.

The system operates at the **information-need level**: instead of classifying questions by interrogative words (where/what/who), it models what information the user actually needs (entity + property + answer form + answer scope).

**New in v.0.1.2**: AnswerScope (STRICT/NORMAL/EXPANDED) controls *how much* information is produced, orthogonal to Property. Agreement-based compression dynamically reduces answer length when evidence strongly agrees. Improved definition synthesis with entity-subject scoring.

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
              |              | Score Fusion (0.7/0.3)      |
              |              v                              |
              |     Ranked Documents                        |
              |              |                              |
              |     +--------v--------+                    |
              |     |    Chunker      |                    |
              |     | split + classify|                    |
              |     | + select_chunks |                    |
              |     | (8 per doc)     |                    |
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
              |  |     AgreementCompressor     NEW      |  |
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
```

---

## 3. Neural Learning Pipelines (Optional, requires libtorch)

```
 +-------------------------------------------------------------+
 |                  Neural Learning Layer                       |
 |                  (offline training, optional)                |
 |                                                             |
 |  +-------------------------+  +--------------------------+  |
 |  |  Neural Sentence Encoder|  | Neural Query Analyzer    |  |
 |  |  (train_encoder)        |  | (train-qa)               |  |
 |  |                         |  |                          |  |
 |  |  Transformer + InfoNCE  |  | Multi-task Transformer   |  |
 |  |  contrastive learning   |  | intent + answer_type     |  |
 |  |  on query-doc pairs     |  | + BIO entity tagging     |  |
 |  |                         |  |                          |  |
 |  |  Output: encoder.pt     |  | Output: qa_model.pt      |  |
 |  |  Improves: HNSW vector  |  | Improves: QueryAnalyzer  |  |
 |  |  retrieval (Phase 2)    |  | property/entity (Phase 1)|  |
 |  +-------------------------+  +--------------------------+  |
 +-------------------------------------------------------------+
         |                                |
         | encoder.pt auto-detected       | qa_model.pt auto-detected
         v                                v
  +--------------+                +---------------+
  | HNSW uses    |                | QueryAnalyzer |
  | neural embeds|                | uses neural   |
  | instead of   |                | instead of    |
  | BoW vectors  |                | rule-based    |
  +--------------+                +---------------+
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
  |
Generate (query, document) pairs
  |
Transformer Encoder:
  Token embedding -> Sinusoidal positional encoding
  -> N Transformer encoder layers (4 heads, 2 layers)
  -> Mean pooling over token outputs
  -> L2 normalization
  |
InfoNCE contrastive loss:
  L = -log(exp(sim(q, d+) / t) / S exp(sim(q, di) / t))
  (t = 0.07, in-batch negatives)
  |
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
  |
Token embedding -> Sinusoidal positional encoding
  -> N Transformer encoder layers (4 heads, 2 layers)
  |
+-----------------+------------------+-----------------+
|  Intent Head    | Answer Type Head |  Entity Head    |
|  (5 classes)    | (7 classes)      |  (BIO tagging)  |
|  mean-pooled    | mean-pooled      |  per-token      |
|  -> Linear(5)   | -> Linear(7)    |  -> Linear(3)   |
+--------+--------+--------+---------+--------+--------+
         |                 |                  |
    FACTUAL/          LOCATION/           B-entity
    EXPLANATION/      DEFINITION/         I-entity
    PROCEDURAL/       PERSON/             O (outside)
    COMPARISON/       TEMPORAL/
    GENERAL           PROCEDURE/
                      COMPARISON/
                      SUMMARY
```

**Training data**: ~27K+ samples auto-generated from the corpus using entity extraction and 22 question templates.

**Impact**:
- Better entity extraction via learned BIO tagging (vs longest-keyword heuristic)
- Better property detection for unusual phrasings
- **Limitation**: Currently produces a single InformationNeed per query -- does not support multi-need decomposition

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

The system degrades gracefully -- all features work without libtorch, just with lower quality.

---

## 4. QA Pipeline Flow (ask command)

```
1. QueryAnalyzer.analyze(query)
   +- Split query into clauses ("X and why Y")
   +- Extract entity per clause (skip non-entity words)
   +- Score properties via semantic prototypes
   +- Detect form (SHORT_FACT, EXPLANATION, LIST, etc.)
   +- Infer scope from query wording ("brief"->STRICT, "explain"->EXPANDED)
   +- Apply property default scope via default_scope_for_property()
   +- Output: InformationNeed[] with entity, property, form, scope, keywords

2. ConversationState.apply(needs)
   +- Propagate last entity to needs with empty entity

3. SelfAsk.expand(need) for each need
   +- ADVANTAGES -> add DEFINITION + LOCATION sub-needs
   +- FUNCTION -> add DEFINITION sub-need
   +- LIMITATIONS -> add USAGE sub-need
   +- HISTORY -> add TIME + DEFINITION sub-needs
   +- Mark sub-needs as is_support=true

4. CLI scope override (--brief / --detailed)
   +- Force STRICT or EXPANDED on all user-facing needs

5. QuestionPlanner.build(expanded_needs)
   +- Explicit dependency rules (HISTORY->LOCATION, FUNCTION->DEFINITION, etc.)
   +- Topological sort for execution order

6. For each need in plan order:
   a. BM25.search(keywords, 10) -> ranked documents
   b. HNSW.search(query_embedding, 10) -> ANN candidates
   c. Score fusion: 0.7 * BM25_norm + 0.3 * ANN_norm (BM25-found docs only)
   d. Chunker.chunk_document() -> split into typed paragraphs
   e. Chunker.select_chunks(property, keywords, 8) -> top 8 per doc
      (with secondary type boost: LOCATION +6 for DEFINITION property)
   f. SelfAsk.check_support_coverage() -> evidence quality check
   g. AnswerSynthesizer.synthesize(need, evidence) -> typed answer + scope truncation
   h. EvidenceNormalizer -> NormalizedClaim[] -> agreement/contradiction analysis
   i. Refined confidence = 0.3*coverage + 0.2*volume + 0.3*agreement + 0.2*(1-penalty)
   j. AgreementCompressor.compress(answer, ctx) -> sentence reduction   << NEW
   k. Confidence-based scope adjustment (NORMAL->STRICT if >0.85)
   l. Re-apply scope truncation after adjustment
   m. AnswerValidator.validate() -> self-ask signal word check
   n. If validation fails + hybrid: retry with BM25-only
   o. Store answer as prior_context for next need

7. ConversationState.save() -> persist to file

8. Output JSON or plain text (support needs filtered out)
   (JSON includes compression field)
```


---

## 5. Module Descriptions

### 5.1 Storage Layer (`src/storage/`)

| File | Purpose |
|------|---------|
| `segment_writer.h/.cpp` | Writes inverted index segments: term dictionary + postings + doc texts |
| `segment_reader.h/.cpp` | Reads segments: postings lookup, doc text retrieval, term enumeration |
| `manifest.h/.cpp` | Tracks segment metadata |
| `wal.h/.cpp` | Write-ahead log for crash recovery |

**Binary format**: Terms are sorted alphabetically. Postings use varint + delta encoding for compact storage. Positions are stored for phrase matching.

### 5.2 Inverted Index (`src/inverted/`)

| File | Purpose |
|------|---------|
| `tokenizer.h/.cpp` | Splits text into lowercase alphanumeric tokens |
| `index_builder.h/.cpp` | Ingests directory of text files -> builds segment |
| `index_reader.h/.cpp` | Low-level postings list access |
| `bm25.h/.cpp` | Okapi BM25 ranking (k1=1.2, b=0.75) |
| `query_parser.h/.cpp` | Parses boolean queries (AND/OR/NOT) and phrase queries |
| `phrase_matcher.h/.cpp` | Position-aware phrase matching |
| `search_engine.h/.cpp` | Full boolean + phrase search with BM25 scoring |

### 5.3 Vector Index (`src/hnsw/`)

| File | Purpose |
|------|---------|
| `hnsw_index.h/.cpp` | Full HNSW (Hierarchical Navigable Small World) graph |
| `hnsw_node.h/.cpp` | Node structure with per-layer neighbor lists |

**Parameters**: M=16, efConstruction=200, efSearch=100. L2 distance metric.

### 5.4 Embedding (`src/embedding/`)

| File | Purpose |
|------|---------|
| `vocab.h/.cpp` | Vocabulary: term <-> integer ID mapping, save/load |
| `embedding_model.h/.cpp` | BoW feedforward net: input -> ReLU hidden -> L2-normalized output |

### 5.5 Neural Encoder (`src/encoder/`, requires libtorch)

| File | Purpose |
|------|---------|
| `sentence_encoder.h/.cpp` | Transformer encoder with sinusoidal PE, mean pooling, L2 norm |
| `encoder_trainer.h/.cpp` | InfoNCE contrastive training on query-document pairs |
| `train_main.cpp` | Training entry point |

### 5.6 Query Analysis (`src/query/`)

| File | Purpose |
|------|---------|
| `information_need.h` | Core data model: Property (11 values), AnswerForm (5 values), AnswerScope (3 values), InformationNeed struct |
| `query_analyzer.h/.cpp` | Rule-based: clause splitting, semantic prototype scoring, entity extraction, scope inference from query wording + property defaults |
| `neural_query_analyzer.h/.cpp` | Neural: multi-task Transformer (intent + answer type + BIO entity), requires libtorch |

### 5.7 Chunking (`src/chunk/`)

| File | Purpose |
|------|---------|
| `chunker.h/.cpp` | Split documents into paragraphs, classify by ChunkType (11 types), keyword+type-aware selection with primary (+10) and secondary (+6) type boosts |

### 5.8 Answer Synthesis (`src/answer/`)

| File | Purpose |
|------|---------|
| `answer_scope.h` | `default_scope_for_property()`, `adjust_scope_by_confidence()`, `max_answer_chars()`, `max_answer_segments()` |
| `answer_compressor.h/.cpp` | Agreement-based compression: CompressionLevel (NONE/LIGHT/STRONG), policy rules, sentence-level reduction |
| `answer_synthesizer.h/.cpp` | 11 typed synthesizers dispatched by Property, typed-chunk-first pattern, entity-subject scoring for definitions, scope-aware truncation |
| `answer_validator.h/.cpp` | Self-ask validation (signal words), evidence analysis, refined confidence |
| `evidence_normalizer.h/.cpp` | NormalizedClaim model, 4 domain keyword vocabularies (60+ keywords), 14 directional opposite pairs, agreement scoring, contradiction detection |
| `question_planner.h/.cpp` | Dependency-aware topological sort of InformationNeeds |
| `self_ask.h/.cpp` | Internal sub-question generation and coverage checking |

### 5.9 Conversation (`src/conversation/`)

| File | Purpose |
|------|---------|
| `conversation_state.h/.cpp` | Entity/property carry-over, file-based persistence across processes |

### 5.10 Hybrid Search (`src/hybrid/`)

| File | Purpose |
|------|---------|
| `hybrid_builder.h/.cpp` | Bootstrap: build vocab + BoW model + HNSW from segment |
| `hybrid_search.h/.cpp` | BM25 + ANN fusion search with summarization |

### 5.11 Other (`src/summarizer/`, `src/tools/`, `src/common/`)

| File | Purpose |
|------|---------|
| `summarizer.h/.cpp` | Keyword-scored extractive summarization (used by `hybrid` command) |
| `sandbox.h/.cpp` | Sandboxed command execution |
| `varint.h/.cpp` | Variable-length integer encoding for compact index storage |
| `file_utils.h/.cpp` | File I/O helpers |
| `types.h` | Type aliases: DocID, TermID, Offset |

---

## 6. Key Algorithms and Concepts

### 6.1 BM25 (Best Matching 25)

The core lexical ranking algorithm. For a query Q and document D:

```
score(Q, D) = S IDF(qi) * (tf(qi, D) * (k1 + 1)) / (tf(qi, D) + k1 * (1 - b + b * |D| / avgdl))
```

Where:
- `IDF(qi) = log((N - df(qi) + 0.5) / (df(qi) + 0.5))` -- inverse document frequency
- `tf(qi, D)` -- term frequency of query term qi in document D
- `|D|` -- document length, `avgdl` -- average document length
- `k1 = 1.2` -- term frequency saturation parameter
- `b = 0.75` -- document length normalization parameter

Top-K extraction uses a min-heap for efficient ranking.

### 6.2 HNSW (Hierarchical Navigable Small World)

Approximate nearest neighbor search using a multi-layer graph:

- **Insertion**: New point is assigned a random level (exponential distribution). Connected to M nearest neighbors at each layer using greedy search from the entry point down.
- **Search**: Start at the top layer's entry point. Greedy search narrows to a single candidate at each upper layer. At layer 0, beam search with efSearch candidates produces the final result.
- **Parameters**: M=16 (max connections per node), M0=32 (layer 0), efConstruction=200, efSearch=100.
- **Distance**: L2 (Euclidean).

### 6.3 Varint + Delta Encoding

Postings lists are compressed using:
- **Varint**: Each integer uses 1-5 bytes. The high bit of each byte indicates continuation.
- **Delta encoding**: Document IDs are stored as differences from the previous ID, reducing values and improving varint compression.
- **Position storage**: Within each posting, positions are also delta-encoded.

### 6.4 Semantic Prototype Scoring

Property detection uses weighted keyword matching against prototype vocabularies:

```cpp
struct PropertyPrototype {
    Property property;
    std::vector<std::string> signals;  // e.g., {"located", "coast", "capital"}
    double weight;                      // e.g., 3.0
};
```

A query clause is scored against all prototypes. The highest-scoring property wins. Multiple properties can activate simultaneously (scores preserved in `property_score`).

### 6.5 Chunk Classification

At ingestion time, each document paragraph is classified into one of 11 ChunkTypes using keyword signals:

| ChunkType | Example Signals |
|-----------|----------------|
| LOCATION | located, coast, situated, eastern, western, capital+city |
| DEFINITION | is a, is an, refers to, defined as, collection of |
| ADVANTAGES | advantage, benefit, widely used, important for |
| LIMITATIONS | limitation, drawback, not suitable, vendor lock |
| FUNCTION | ensures, mechanism, handshake, flow control |
| USAGE | used for, beginner, recommend, learning path |
| HISTORY | history, founded, first described, first practical |
| TEMPORAL | century, 1947, invented, year |
| PERSON | born, inventor, alan turing |
| PROCEDURE | step, how to, algorithm, method |
| GENERAL | (fallback) |

**Chunk selection** uses a three-tier type boost system:
- **Primary type** (+10): exact match (e.g., LOCATION chunk for LOCATION property)
- **Secondary type** (+6): strong fallback (e.g., LOCATION chunk for DEFINITION property -- geographic entities' definitions often live in LOCATION chunks)
- **Fallback types** (+1): other preferred types

### 6.6 Hybrid Score Fusion

BM25 and ANN scores are fused per document:

```
fused_score = 0.7 * (bm25_score / max_bm25) + 0.3 * ((cosine_sim + 1) / 2)
```

Only documents found by BM25 are considered (ANN-only results are discarded to prevent irrelevant documents from entering the evidence pool).

### 6.7 Evidence Agreement and Contradiction

Evidence chunks are normalized to `NormalizedClaim` with controlled keyword extraction from 4 domain vocabularies (60+ keywords).

**Agreement**: Two claims agree if same entity + same property + substantial keyword overlap + no negation mismatch. Score = keyword overlap ratio (Jaccard-like).

**Contradiction**: Two claims contradict if same entity + same property + different documents + (negation conflict OR directional opposites). 14 opposite pairs defined (east/west, cheap/expensive, etc.).

### 6.8 Refined Confidence

```
confidence = 0.3 * coverage      (keyword match ratio in evidence)
           + 0.2 * volume        (min(1.0, evidence_count / 3.0))
           + 0.3 * agreement     (average agreement score across claim pairs)
           + 0.2 * (1 - penalty) (contradiction penalty, max 0.4)
```

### 6.9 AnswerScope (NEW in v.0.1.2)

AnswerScope controls *how much* information is produced, orthogonal to Property:

```cpp
enum class AnswerScope { STRICT, NORMAL, EXPANDED };
```

| Scope | Max Chars | Max Segments | Trigger |
|-------|-----------|-------------|---------|
| STRICT | 200 | 2 | "brief", "short", "just", or SHORT_FACT form |
| NORMAL | 400 | 4 | Default |
| EXPANDED | 700 | 8 | "explain", "in detail", "describe", "comprehensive" |

**Scope resolution priority** (highest to lowest):
1. CLI flag (`--brief` / `--detailed`) -- always wins
2. Query wording ("brief", "explain in detail")
3. Property-based default via `default_scope_for_property()`
4. Confidence-based adjustment (post-synthesis)

**Property -> Default Scope**:

| Property | Default Scope | Rationale |
|----------|--------------|-----------|
| LOCATION, DEFINITION, TIME | STRICT | Facts should be concise |
| FUNCTION, USAGE, ADVANTAGES, LIMITATIONS | NORMAL | Needs some context |
| HISTORY, COMPARISON | EXPANDED | Requires narrative |

**Confidence-based adjustment**:
- confidence > 0.85 + NORMAL -> compress to STRICT
- confidence < 0.5 + STRICT -> expand to NORMAL

### 6.10 Agreement-Based Compression (NEW in v.0.1.2)

Post-synthesis compression that reduces answer length when evidence strongly agrees:

```cpp
enum class CompressionLevel { NONE, LIGHT, STRONG };
```

**Decision rules** (first match wins):

| Condition | Compression |
|-----------|-------------|
| confidence < 0.6 | NONE |
| agreement < 0.6 | NONE |
| STRICT scope | NONE |
| evidence >= 4 + confidence >= 0.9 | STRONG |
| NORMAL + confidence >= 0.85 + agreement >= 0.7 | STRONG |
| EXPANDED + confidence >= 0.85 + agreement >= 0.7 | LIGHT |
| otherwise | NONE |

**Compression behavior**:
- **NONE**: Keep answer as-is
- **LIGHT**: Keep first 2 sentences (drop trailing explanations)
- **STRONG**: Keep first sentence only (canonical fact)

Pipeline position: after confidence computation, before scope truncation.

### 6.11 Definition Synthesis (Improved in v.0.1.2)

The definition synthesizer uses a two-pass approach:

**First pass**: Score DEFINITION + LOCATION typed chunks by definitional quality:
- "entity is " pattern at start (+10)
- "is a", "is an", "is the", "capital", "known for", "refers to" (+3 each)
- Penalize: "expensive", "tech hub", "important for", "ranking" (-4 each)
- Entity must appear in first 30 chars

**Fallback**: Score individual segments across all evidence:
- Entity as subject in first 30 chars (+6)
- "entity is/are" pattern (+8)
- Definitional patterns (+5 each)
- Penalize non-definitional content (-3 each)

This ensures "what is stockholm" returns "Stockholm is Sweden's capital and largest city..." rather than quality-of-life or tech-hub content.

### 6.12 Self-Ask Expansion

Before answering, the system generates internal sub-questions:

| Need Property | Generated Sub-Needs |
|---------------|-------------------|
| ADVANTAGES | DEFINITION + LOCATION |
| COMPARISON | DEFINITION |
| LIMITATIONS | USAGE |
| HISTORY | TIME + DEFINITION |
| FUNCTION | DEFINITION |

Sub-needs are marked `is_support=true`, added to the plan, answered internally, but hidden from output.

### 6.13 Question Planning (Topological Sort)

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

### 6.14 InfoNCE Contrastive Loss (Neural Encoder)

The optional neural sentence encoder is trained with in-batch InfoNCE loss:

```
L = -log(exp(sim(q, d+) / t) / S exp(sim(q, di) / t))
```

Where `sim` is cosine similarity, `d+` is the positive document, and `t = 0.07` is the temperature. This learns embeddings where semantically similar query-document pairs are close in vector space.

### 6.15 Neural Query Analyzer (Multi-Task)

The optional neural query analyzer uses a Transformer encoder with three classification heads:
- **Intent head**: 5-class (FACTUAL, EXPLANATION, PROCEDURAL, COMPARISON, GENERAL)
- **Answer type head**: 7-class (LOCATION, DEFINITION, PERSON, TEMPORAL, PROCEDURE, COMPARISON, SUMMARY)
- **Entity head**: BIO tagging (3 tags per token: O, B-entity, I-entity)

Training data (~27K+ samples) is auto-generated from the corpus using entity extraction and 22 question templates.


---

## 7. Data Flow: Ingestion

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
  +- terms.bin   (sorted term dictionary)
  +- postings.bin (varint+delta encoded posting lists)
  +- docs.bin    (document lengths)
  +- rawdocs.bin (raw document texts)
```

---

## 8. Data Flow: Hybrid Index Build

### 8a. BoW Path (default, no libtorch)

```
SegmentReader (existing segment)
  |
Vocabulary.build_from_terms() -> term<->ID mapping
  |
EmbeddingModel.init_random() -> BoW feedforward net
  |
For each document:
  Tokenize -> BoW vector -> EmbeddingModel.embed() -> dense vector
  |
HNSWIndex.add_point() -> build navigable small world graph
  |
Save: vocab.txt + model.bin
```

### 8b. Neural Path (after train_encoder)

```
SegmentReader (existing segment)
  |
Vocabulary.load(vocab.txt)
  |
EncoderTrainer.load(encoder.pt) -> trained Transformer
  |
For each document:
  Full text -> Tokenize -> Transformer encode -> L2-normalized dense vector
  |
HNSWIndex.add_point() -> build navigable small world graph
  (semantically meaningful vectors, not random BoW)
```

The neural path produces higher-quality embeddings where semantically similar documents cluster together, even without shared keywords.

---

## 9. File Structure

```
v.0.1.2/
+-- data/                    # 21 text documents across 7 topics
|   +-- biology/             # animals, plants
|   +-- computer_science/    # algorithms, databases, networking, security, python, blockchain
|   +-- economics/           # inflation
|   +-- engineering/         # solar energy, electric vehicles
|   +-- geography/           # japan, mars, climate change
|   +-- health/              # antibiotics
|   +-- misc/                # AI overview, history of computing
|   +-- sweden/              # stockholm, gothenburg, malmo
+-- src/
|   +-- answer/              # synthesis, validation, planning, self-ask, evidence normalization,
|   |                        #   answer scope, agreement compression
|   +-- chunk/               # document chunking and classification
|   +-- cli/                 # command-line interface (main, commands)
|   +-- common/              # types, varint, file utils
|   +-- conversation/        # cross-process conversation memory
|   +-- embedding/           # BoW embedding model, vocabulary
|   +-- encoder/             # neural Transformer encoder (optional, libtorch)
|   +-- hnsw/                # HNSW vector index
|   +-- hybrid/              # BM25+HNSW fusion search
|   +-- inverted/            # tokenizer, BM25, boolean search, phrase matching
|   +-- query/               # InformationNeed model, query analyzer (rule + neural)
|   +-- storage/             # binary segment reader/writer, WAL, manifest
|   +-- summarizer/          # extractive summarization
|   +-- tools/               # sandboxed command execution
+-- tests/
|   +-- test_*.cpp           # 76+ GoogleTest unit tests
|   +-- test_qa_integration.sh  # 75 QA integration tests
+-- CMakeLists.txt
+-- build.md
+-- history.md
+-- codebase_analysis.md
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
| QA integration tests | 75 | Property detection, answer content, validation status, multi-need decomposition, definition quality, compression field verification, edge cases across 7 topic areas |

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

---

## 12. Changes from v.0.1.1 to v.0.1.2

| Feature | v.0.1.1 | v.0.1.2 |
|---------|---------|---------|
| AnswerScope | Not present | STRICT/NORMAL/EXPANDED with property defaults |
| CLI flags | `--json` only | `--json`, `--brief`, `--detailed` |
| Scope inference | N/A | Query wording + property default + confidence adjustment |
| Agreement compression | Not present | NONE/LIGHT/STRONG post-synthesis compression |
| Definition synthesis | Generic keyword scoring | Entity-subject scoring with definitional pattern boosts |
| Chunk selection | 5 per doc, primary type only | 8 per doc, primary (+10) + secondary (+6) type boosts |
| DEFINITION validation signals | 8 signals | 12 signals (added capital, largest, known for, known as) |
| Chunk classification | "means " triggers DEFINITION | Removed (false positive) |
| Integration tests | 67 | 75 |
| JSON output | No compression field | Includes `compression` field |

### New Files in v.0.1.2

| File | Purpose |
|------|---------|
| `src/answer/answer_scope.h` | Scope policy: property defaults, confidence adjustment, char/segment limits |
| `src/answer/answer_compressor.h` | CompressionLevel enum, CompressionContext, AgreementCompressor class |
| `src/answer/answer_compressor.cpp` | Compression decision rules, sentence splitting, light/strong compression |
