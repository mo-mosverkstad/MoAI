# MySearch — Full Codebase Analysis

## 1. Project Overview

MySearch is a fully self-contained, offline search engine and question-answering system written from scratch in C++20. It implements an inverted index, BM25 ranking, HNSW approximate nearest neighbor search, a neural sentence encoder (via optional libtorch), a neural multi-task query classifier (via optional libtorch), document chunking, query intent analysis, and extractive answer synthesis — all without relying on external search libraries like Lucene or Tantivy.

---

## 2. Directory Structure

```
src/
  common/          — Shared types, varint encoding, file I/O
  storage/         — Segment writer/reader, manifest, WAL
  inverted/        — Tokenizer, BM25, query parser, phrase matcher, search engine, index builder/reader
  hnsw/            — HNSW vector index (multilayer graph)
  embedding/       — BoW embedding model + vocabulary
  encoder/         — Transformer sentence encoder (libtorch, optional)
  hybrid/          — Hybrid BM25+ANN builder and search
  summarizer/      — Extractive sentence summarizer
  query/           — Query intent/answer-type analyzer (rule-based + neural)
  chunk/           — Document paragraph chunker with type classification
  answer/          — Answer synthesizer (type-aware, with temporal-specific extraction)
  conversation/    — Follow-up question memory
  cli/             — CLI entry point and command dispatch
  tools/           — Sandbox command executor
tests/             — 8 GoogleTest files (76+ tests)
data/              — Example documents (CS, Sweden, biology, misc)
embeddings/        — Trained encoder weights + vocab
segments/          — Binary index segments
```

---

## 3. Module-by-Module Analysis

### 3.1 Common (src/common/)

- types.h — Type aliases: DocID (uint32), TermID (uint32), Offset (uint64).
- varint.h/.cpp — Variable-length integer encoding (7-bit groups, MSB continuation). Used throughout the binary index format for compact storage of doc gaps, term lengths, positions, etc.
- file_utils.h/.cpp — Binary file read/write/append + recursive directory creation via std::filesystem.

### 3.2 Storage (src/storage/)

- SegmentWriter — Collects documents (text + tokens), tracks per-term postings with TF and positions, then serializes to 4 binary files:
  - docs.bin — varint-encoded document lengths
  - terms.bin — dictionary entries: term_len + term_bytes + postings_offset + postings_length
  - postings.bin — delta-encoded doc gaps + TF + delta-encoded positions (shifted by +1, 0 = sentinel)
  - rawdocs.bin — length-prefixed raw document texts
- SegmentReader — Loads all 4 files at construction. Provides get_postings(term), get_positions_for_doc(term, doc), doc_length(doc), doc_count(), average_doc_length(), all_terms(), get_document_text(doc).
- Manifest — Tracks segment directories in a newline-delimited text file. Supports add/save/load.
- WAL — Append-only write-ahead log (text lines). Designed for crash recovery.

Key design detail: Position deltas use a +1 shift so that 0 serves as the end-of-positions sentinel. Without this, position 0 (first token) would produce a gap of 0 and be indistinguishable from the sentinel.

### 3.3 Inverted Index (src/inverted/)

- Tokenizer — ASCII tokenizer: lowercases, splits on non-alphanumeric characters. No stemming, no stop-word removal, no CJK support.
- QueryParser — Parses queries into an AST supporting:
  - Single terms
  - Quoted phrases ("quick brown")
  - Boolean operators: AND, OR, NOT
  - Left-associative chaining (a AND b OR c becomes (a AND b) OR c)
  - Uses std::variant<TermNode, Phrase, BoolExpr*> for the AST node type.
- PhraseMatcher — Position-aware phrase matching. For phrase [t1, t2, t3], checks if there exists a position p in t1's positions such that p+1 is in t2's and p+2 is in t3's. Uses std::binary_search on sorted position lists.
- BM25 — Full Okapi BM25 implementation (k1=1.2, b=0.75 defaults). Computes corpus stats (N, avgdl) at construction. Uses a min-heap for top-K retrieval. Also provides score_docs() for scoring a specific doc subset.
- SearchEngine — Orchestrates query parsing, AST evaluation, and BM25 scoring. Evaluates terms, phrases, and boolean expressions recursively. Boolean merge uses sorted two-pointer intersection/union/difference. Supports JSON output.
- IndexBuilder — Recursively ingests a directory, tokenizes each file, feeds to SegmentWriter, then finalizes.
- IndexReader — Thin wrapper around SegmentReader for the inverted index API.

### 3.4 HNSW (src/hnsw/)

- HNSWNode — Stores node ID, max level, and per-layer neighbor lists.
- HNSWIndex — Full HNSW implementation:
  - Level sampling: level = -ln(uniform) * (1/ln(M)) per the HNSW paper
  - M0 = 2*M at layer 0 for better recall
  - Squared L2 distance (avoids sqrt for comparison)
  - Insertion: Greedy descent through upper layers, then beam search with efConstruction at each layer from min(level, maxLevel) down to 0. Bidirectional linking with pruning via select_neighbors_heuristic.
  - Search: Greedy descent to layer 0, then beam search with max(topK, efSearch).
  - Neighbor selection heuristic: Sorts candidates by distance, keeps closest M.

### 3.5 Embedding (src/embedding/)

- Vocabulary — Term-to-integer mapping. Can build from a term list, save/load to text file.
- EmbeddingModel — 2-layer feedforward neural network (BoW -> hidden -> output). ReLU activations, L2 normalization. Xavier initialization. Binary save/load of weights. This is the fallback when libtorch is not available.

### 3.6 Neural Encoder (src/encoder/, optional, requires libtorch)

- SentenceEncoderImpl — PyTorch nn::Module:
  - Token embedding + fixed sinusoidal positional encoding
  - nn::TransformerEncoder (configurable layers/heads, GELU activation)
  - Mean pooling over valid tokens (respects padding mask)
  - L2 normalization
- contrastive_loss — InfoNCE loss: cross_entropy(q*d^T/tau, labels) with in-batch negatives. Temperature tau=0.07.
- EncoderTrainer — Generates training pairs from segment documents (overlapping word chunks as query/doc pairs). Trains with AdamW (weight_decay=0.01). Provides encode(text) for inference.
- train_main.cpp — Standalone training executable with CLI args (--epochs, --lr, --dim, --segdir, --embeddir).

### 3.7 Hybrid Search (src/hybrid/)

- HybridBuilder — Builds HNSW index from segment documents. For each doc: constructs BoW vector, embeds via EmbeddingModel, inserts into HNSW. Also provides bootstrap() which auto-creates vocab + random model + HNSW from a segment.
- HybridSearch — Fuses BM25 and ANN results:
  - BM25 retrieval on tokenized query
  - ANN retrieval via HNSW on embedded query
  - Score fusion: 0.7 * bm25_norm + 0.3 * ann_norm where BM25 is normalized to [0,1] by max, and ANN cosine is mapped from [-1,1] to [0,1].
  - Includes summarize_results() which loads document texts and calls the Summarizer.

### 3.8 Summarizer (src/summarizer/)

- Extractive summarizer: splits documents into sentences, scores each by query keyword overlap (word-boundary aware), gives bonus for multi-keyword matches, penalizes headers/list items, prefers moderate-length sentences. Deduplicates, limits per-doc sentences, returns top-N joined.

### 3.9 Query Analyzer (src/query/)

The query analyzer has a two-tier architecture: a unified `QueryAnalyzer` dispatcher that delegates to either a neural model or a rule-based fallback.

**Unified dispatcher** (`QueryAnalyzer`):
- On construction, attempts to load a neural model via `load_neural(model_path, vocab_path)`
- If the model file (`qa_model.pt`) and vocabulary exist and `HAS_TORCH` is defined, uses the neural analyzer
- Otherwise, falls back to `RuleBasedQueryAnalyzer`
- Uses an opaque `NeuralImpl` pointer (pimpl idiom) to avoid leaking torch headers into non-torch builds

**Which analyzer is actually used at runtime**:
- Built without libtorch (`USE_TORCH=OFF`): `load_neural()` always returns false → rule-based
- Built with libtorch but `qa_model.pt` doesn't exist: `load_neural()` returns false → rule-based
- Built with libtorch and `qa_model.pt` exists (after `./mysearch train-qa`): `load_neural()` succeeds → neural
- Deleting `../embeddings/qa_model.pt` reverts to rule-based automatically

**Rule-based analyzer** (`RuleBasedQueryAnalyzer`):
- Detects QueryIntent (FACTUAL, EXPLANATION, PROCEDURAL, COMPARISON, GENERAL) from leading interrogatives and verb patterns.
- Detects AnswerType (LOCATION, DEFINITION, PERSON_PROFILE, TEMPORAL, PROCEDURE, COMPARISON, SUMMARY) from question words and content keywords.
- Extracts keywords by filtering stop words (large built-in set).
- Extracts main entity as the longest keyword (heuristic).

**Neural analyzer** (`NeuralQueryAnalyzer`, requires libtorch):
- Multi-task transformer classifier (`QueryClassifier`) with three heads:
  - Intent classification (5 classes) — sentence-level, from mean-pooled encoder output
  - Answer type classification (7 classes) — sentence-level, from mean-pooled encoder output
  - Entity extraction (3 BIO tags per token) — token-level, from per-token encoder output
- Architecture: token embedding + sinusoidal positional encoding → TransformerEncoder → mean-pool for sentence heads, raw token output for entity head
- Training data is auto-generated from the corpus via `generate_training_data()`: extracts entities (capitalized phrases, technical terms, long words) from documents, combines them with 22 question templates to produce ~27K labeled samples
- Trained with AdamW, multi-task loss: `cross_entropy(intent) + cross_entropy(answer_type) + 0.5 * masked_cross_entropy(entity_BIO)`
- Entity extraction uses BIO tagging: B=begin, I=inside, O=outside; decoded by scanning for B→I* spans; falls back to longest non-stop keyword if no entity tagged

### 3.10 Chunker (src/chunk/)

- Splits documents into paragraphs (double-newline or heading boundaries).
- Classifies each paragraph by keyword heuristics:
  - LOCATION: "located", "capital", "coast", "island", etc.
  - DEFINITION: "is a", "refers to", "defined as", etc.
  - PERSON: "born", "inventor", "alan turing", etc.
  - TEMPORAL: "century", dates, "ancient", "history", etc.
  - PROCEDURE: "step", "algorithm", "method", etc.
  - HISTORY: "heritage", "medieval", "founded", etc.
  - GENERAL: fallback

### 3.11 Answer Synthesizer (src/answer/)

- Dispatches to specialized synthesizers based on AnswerType.
- Each synthesizer: filters evidence by preferred ChunkTypes, extracts best sentences by keyword scoring, assembles answer text.
- `synthesize_temporal()` uses enhanced extraction: searches ALL evidence (not just TEMPORAL/HISTORY chunks), uses `split_into_segments()` to handle both prose and bullet-point lists, boosts sentences containing year patterns (+6.0 via regex `\b(1[0-9]{3}|20[0-9]{2})\b`), entity mentions (+4.0), and stem-prefix keyword matches (+1.5 for partial matches like "invented" → "invention"). Collects candidates globally across all evidence before picking the best.
- Computes confidence as: 0.4 * keyword_coverage + 0.3 * avg_evidence_score + 0.3 * evidence_agreement.

### 3.12 Conversation State (src/conversation/)

- Stores last entity and last answer type.
- On follow-up queries with no entity, reuses the previous entity and injects it as a keyword.

### 3.13 CLI (src/cli/)

- main.cpp — Entry point with top-level exception handling.
- commands.cpp — Dispatches CLI commands:
  - ingest <path> — Build index from directory
  - search <query> [--json] — Boolean/phrase BM25 search
  - build-hnsw — Bootstrap BoW embeddings + HNSW
  - hybrid <query> [--json] — BM25+ANN hybrid search with summary (auto-detects neural encoder if available)
  - ask <query> [--json] — Full QA pipeline (analyze, retrieve, chunk, synthesize); auto-loads neural query analyzer if available
  - train-qa — Train neural query analyzer from corpus (when built with libtorch)
  - train-encoder — Train neural sentence encoder (when built with libtorch)
  - run <cmd> [args...] — Sandboxed command execution

### 3.14 Sandbox (src/tools/)

- Allowlist-based command execution. Only permits executables under specified directory prefixes (e.g., /usr/bin, /bin).
- Uses fork() + execvp() (Linux/WSL only).

---

## 4. Build System

- CMake 3.16+, C++20 standard.
- Core library mysearch_lib has no external dependencies.
- Optional USE_TORCH flag enables libtorch-based neural encoder and neural query analyzer (mysearch_encoder library + train_encoder executable).
- Tests use GoogleTest v1.14.0 via FetchContent. Controlled by BUILD_TESTS option.

Build commands:
  mkdir build && cd build
  cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
  cmake --build .

With libtorch:
  cmake .. -DUSE_TORCH=ON -DCMAKE_PREFIX_PATH=~/opt/libtorch
  cmake --build .

Run tests:
  ctest --output-on-failure
  ./mysearch_tests --gtest_filter="BM25Test.*"

---

## 5. Test Coverage (8 test files, 76+ tests)

| File                     | Coverage                                                                                  |
|--------------------------|-------------------------------------------------------------------------------------------|
| test_varint.cpp          | Encode/decode round-trips (0, small, large, max, multi-value, byte counts)                |
| test_tokenizer.cpp       | Lowercase, punctuation, numbers, empty, mixed delimiters                                  |
| test_query_parser.cpp    | Single term, phrase, AND/OR/NOT, chained boolean, empty query                             |
| test_segment_io.cpp      | Write/read, postings, TF, doc lengths, positions, multi-positions, out-of-range           |
| test_bm25.cpp            | Search results, TF ranking, nonexistent terms, empty tokens, multi-term, score_docs, topK |
| test_search_engine.cpp   | Single term, AND, OR, NOT, phrase (position-aware), phrase+boolean, cross-topic, topK     |
| test_hnsw.cpp            | Empty index, exact match, topK ordering, recall (>=80% on 500 pts), high-dim, duplicates  |
| test_hybrid.cpp          | Vocab, embedding model, hybrid builder, hybrid search (fusion, ordering, cross-topic)     |

---

## 6. Data Flow / Coding Workflow

### Indexing Pipeline
```
Files on disk
  -> IndexBuilder.ingest_directory()
    -> recursive file scan
    -> fileutils::read_file()
    -> Tokenizer.tokenize()
    -> SegmentWriter.add_document(text, tokens)
  -> SegmentWriter.finalize()
    -> docs.bin, terms.bin, postings.bin, rawdocs.bin
```

### Search Pipeline (BM25)
```
Query string
  -> QueryParser.parse() -> AST (Term/Phrase/BoolExpr)
  -> SearchEngine.eval_expr()
    -> eval_term() / eval_phrase() / boolean_merge()
    -> BM25.score_docs()
  -> Sort by score, topK
```

### Hybrid Pipeline
```
Query string
  -> Tokenizer -> BM25.search() -> top-K BM25 docs
  -> embed_query() -> HNSW.search() -> top-K ANN docs
  -> Merge + normalize (0.7*bm25 + 0.3*ann)
  -> Summarizer.summarize()
```

### QA Pipeline (ask command)
```
Query string
  -> QueryAnalyzer.analyze()
       -> [if qa_model.pt exists] NeuralQueryAnalyzer (transformer multi-task classifier)
       -> [else] RuleBasedQueryAnalyzer (pattern matching fallback)
       -> intent, answerType, entity, keywords
  -> BM25.search(keywords)
  -> Chunker.chunk_document() for each retrieved doc
  -> Filter/boost chunks by preferred ChunkTypes for the answerType
  -> AnswerSynthesizer.synthesize()
       -> [if TEMPORAL] year-aware extraction across ALL evidence with stem matching
       -> [else] type-filtered extraction with keyword scoring
  -> Output with confidence score
```

---

## 7. Key Features Summary

 1. Custom binary index format — varint + delta encoding for compact postings
 2. BM25 ranking — Standard Okapi BM25 with configurable k1/b
 3. Boolean query support — AND, OR, NOT with sorted merge
 4. Phrase queries — Position-aware matching with delta-encoded positions
 5. HNSW vector index — Full multilayer graph with efConstruction/efSearch
 6. BoW embedding model — 2-layer feedforward net, Xavier init, L2 normalization
 7. Neural sentence encoder — Transformer encoder with contrastive learning (optional libtorch)
 8. Hybrid BM25+ANN search — Normalized score fusion (0.7/0.3 weighting)
 9. Extractive summarization — Keyword-scored sentence extraction with deduplication
10. Query intent detection — Rule-based + neural multi-task classifier with auto-fallback
11. Document chunking — Paragraph splitting with semantic type classification
12. Answer-type-aware synthesis — Specialized answer generation per question type, with temporal-specific year-aware extraction
13. Confidence scoring — Coverage + relevance + agreement formula
14. Conversation memory — Entity/answer-type carry-over for follow-ups
15. Sandboxed command execution — Allowlist + fork/exec
16. WAL + Manifest — Crash recovery and segment tracking
17. JSON output — All search/ask commands support --json
18. Comprehensive test suite — 76+ GoogleTest cases covering all core components
19. Neural query analyzer — Multi-task transformer classifier for intent, answer type, and entity extraction (optional libtorch)

### Feature 1: Custom Binary Index Format

MySearch uses a fully hand-written binary storage format with no dependency on SQLite, Lucene, or any external database. Each segment consists of four binary files:

- **docs.bin**: Stores document lengths as varint-encoded uint32 values, one per document in insertion order. This allows O(1) lookup of any document's token count by DocID.
- **terms.bin**: A flat dictionary where each entry contains: term length (varint), raw term bytes, postings offset (varint), and postings byte length (varint). Terms are stored in hash-map iteration order (unsorted). Lookup is done by loading the entire dictionary into an in-memory `std::unordered_map` at segment open time.
- **postings.bin**: For each term, stores: document frequency (varint), then for each posting: doc ID gap (varint, delta-encoded from previous doc), term frequency (varint), and position gaps (varint, delta-encoded with +1 shift so 0 acts as end sentinel). This encoding is compact — small gaps and frequencies use only 1 byte.
- **rawdocs.bin**: Stores original document text as length-prefixed blobs (length as varint, then raw bytes). This enables the summarizer and answer synthesizer to access full document content without re-reading source files.

The varint encoding follows the standard 7-bit-per-byte scheme with MSB continuation bit. Values under 128 use 1 byte, values under 16384 use 2 bytes, etc. The position delta +1 shift is critical: without it, the first token at position 0 would produce a gap of 0, which is indistinguishable from the end-of-positions sentinel.

**Source files**: `src/common/varint.h/.cpp`, `src/storage/segment_writer.h/.cpp`, `src/storage/segment_reader.h/.cpp`

---

### Feature 2: BM25 Ranking

The BM25 implementation follows the standard Okapi BM25 formula:

```
score(Q, D) = sum over t in Q of: IDF(t) * (tf * (k1 + 1)) / (tf + k1 * (1 - b + b * |D| / avgdl))
```

Where:
- `IDF(t) = log((N - df + 0.5) / (df + 0.5) + 1)` — the "+1" variant prevents negative IDF for very common terms
- `k1 = 1.2` (default) — controls term frequency saturation
- `b = 0.75` (default) — controls document length normalization
- `N` = total document count in the segment
- `avgdl` = average document length (computed at BM25 construction)

At construction, BM25 iterates all documents to compute `avgdl`. For each search query, it:
1. Deduplicates query terms
2. Precomputes IDF for each unique term
3. Iterates postings for each term, accumulating per-document BM25 contributions
4. Maintains a min-heap of size topK for efficient top-K extraction
5. Returns results sorted by descending score

The `score_docs()` method scores only a specified subset of documents, used by the SearchEngine when evaluating boolean expressions where the candidate set is already filtered.

**Source files**: `src/inverted/bm25.h/.cpp`

---

### Feature 3: Boolean Query Support

The query parser builds a left-associative AST from queries like `fox AND quick`, `database OR algorithm`, or `firewall NOT security`. The AST node type uses `std::variant<TermNode, Phrase, BoolExpr*>`.

Boolean merge operations work on sorted DocID vectors using two-pointer algorithms:
- **AND**: Advances both pointers, emits when equal — O(n+m)
- **OR**: Merges both lists, deduplicating — O(n+m)
- **NOT**: Emits from left list only when not present in right list — O(n+m)

The SearchEngine evaluates the AST recursively:
- TermNode: retrieves postings, scores via BM25
- Phrase: evaluates with position-aware matching (see Feature 4)
- BoolExpr: evaluates left and right subtrees, merges DocID sets, then combines scores

For boolean expressions, scores from both sides are summed for documents that survive the merge. This means an AND query gives higher scores to documents matching both terms.

**Source files**: `src/inverted/query_parser.h/.cpp`, `src/inverted/search_engine.h/.cpp`

---

### Feature 4: Phrase Queries

Phrase queries like `"machine learning"` require position-aware matching. The system stores token positions in the postings list using delta encoding with a +1 shift.

The phrase matching algorithm:
1. For each term in the phrase, retrieve its postings with positions
2. Intersect document sets (AND) to find candidate documents
3. For each candidate document, collect position lists for all phrase terms
4. Check if there exists a starting position `p` in the first term's positions such that `p+1` is in the second term's positions, `p+2` in the third's, etc.
5. Position lookup uses `std::binary_search` on sorted position vectors

Example: For `"machine learning"` against a document with tokens `[artificial(0), intelligence(1), machine(2), learning(3), deep(4), neural(5)]`:
- "machine" has position [2], "learning" has position [3]
- Check: 2+1 = 3, and 3 is in learning's positions -> match!
- But `"learning machine"` would check: 3+1 = 4, and 4 is NOT in machine's positions -> no match

This correctly enforces word order in phrase queries.

**Source files**: `src/inverted/phrase_matcher.h/.cpp`, `src/inverted/search_engine.cpp` (eval_phrase method)

---

### Feature 5: HNSW Vector Index

The HNSW (Hierarchical Navigable Small World) implementation is a full multilayer proximity graph for approximate nearest neighbor search. Key parameters:

- **M** (default 16): Maximum number of connections per node per layer
- **M0** (= 2*M = 32): Maximum connections at layer 0 (denser for better recall)
- **efConstruction** (default 200): Beam width during index building
- **efSearch** (default 100): Beam width during search

**Level assignment**: Each new point gets a random level sampled as `level = floor(-ln(uniform) / ln(M))`. Most points end up at level 0; higher levels are exponentially rarer, creating the hierarchical structure.

**Insertion algorithm**:
1. Sample a level for the new point
2. Starting from the entry point, greedily descend through layers above the new point's level (ef=1)
3. At each layer from min(level, maxLevel) down to 0, perform beam search with efConstruction to find neighbors
4. Select up to M (or M0 at layer 0) neighbors using the heuristic (closest by distance)
5. Create bidirectional links; if any neighbor exceeds its connection limit, prune its weakest connections

**Search algorithm**:
1. Start at the entry point, greedily descend through upper layers (ef=1)
2. At layer 0, perform beam search with ef = max(topK, efSearch)
3. Return the topK closest results

The implementation uses squared L2 distance (avoiding sqrt since it's monotonic and doesn't affect ranking). Two priority queues are used during search: a min-heap for candidate expansion and a max-heap for bounding the search frontier.

Tests verify >= 80% recall on 500 random 8-dimensional points compared to brute-force kNN.

**Source files**: `src/hnsw/hnsw_node.h`, `src/hnsw/hnsw_index.h/.cpp`

---

### Feature 6: BoW Embedding Model

The bag-of-words embedding model is a lightweight 2-layer feedforward neural network that runs without any ML framework:

```
Input:  BoW vector (size = vocab_size, each element = term frequency)
Layer1: h = ReLU(W1 * input + b1)    [hidden_dim neurons]
Layer2: out = ReLU(W2 * h + b2)      [output_dim neurons]
Output: L2-normalized embedding vector
```

**Weight initialization**: Xavier/Glorot uniform — `scale = sqrt(2 / (fan_in + fan_out))`, weights sampled from `Uniform(-scale, scale)`. Biases initialized to zero.

**L2 normalization**: The output vector is divided by its L2 norm, ensuring all embeddings lie on the unit hypersphere. This is critical for cosine similarity comparisons in HNSW.

The model weights are stored in a binary file (`model.bin`): hidden_dim (size_t), output_dim (size_t), then W1, b1, W2, b2 as contiguous float arrays.

This model serves as the fallback embedding when libtorch is not available. While it lacks true semantic understanding (it's essentially a learned dimensionality reduction of BoW), it provides a baseline for hybrid search.

**Source files**: `src/embedding/embedding_model.h/.cpp`, `src/embedding/vocab.h/.cpp`

---

### Feature 7: Neural Sentence Encoder (Optional, libtorch)

When built with `USE_TORCH=ON`, the system includes a proper Transformer-based sentence encoder trained with contrastive learning:

**Architecture**:
- Token embedding layer (vocab_size + 1 for UNK token)
- Fixed sinusoidal positional encoding (same as original Transformer paper)
- N-layer TransformerEncoder (default: 2 layers, 4 heads, GELU activation, 4x FFN)
- Mean pooling over non-padded tokens
- L2 normalization

**Training** (InfoNCE contrastive loss):
- Training pairs are generated from documents: overlapping 5-word query chunks paired with 15-word document chunks from the same document
- Loss: `cross_entropy(q * d^T / temperature, identity_labels)` where temperature = 0.07
- In-batch negatives: every other document chunk in the batch serves as a negative example
- Optimizer: AdamW with weight_decay=0.01
- This teaches the encoder that queries should be close to their source documents in embedding space

**Integration**: The `hybrid` CLI command auto-detects `encoder.pt` at runtime. If found, it uses the neural encoder; otherwise falls back to the BoW model. The neural encoder produces much more discriminative ANN scores (relevant docs: 0.2-0.3, irrelevant: 0.05-0.15) compared to the BoW model (all docs cluster around 0.45-0.56).

**Source files**: `src/encoder/sentence_encoder.h/.cpp`, `src/encoder/encoder_trainer.h/.cpp`, `src/encoder/train_main.cpp`

---

### Feature 8: Hybrid BM25+ANN Search

The hybrid search fuses lexical (BM25) and semantic (ANN) retrieval for better coverage:

**Retrieval phase**:
1. Tokenize query, run BM25 search for top-K documents (lexical match)
2. Embed query into a vector, run HNSW search for top-K nearest neighbors (semantic match)
3. Merge results into a unified candidate set

**Score normalization and fusion**:
- BM25 scores are normalized to [0,1] by dividing by the maximum BM25 score in the result set
- ANN scores (cosine similarity in [-1,1]) are mapped to [0,1] via `(cosine + 1) * 0.5`
- Final score: `0.7 * bm25_norm + 0.3 * ann_norm`

The 0.7/0.3 weighting reflects that BM25 is more reliable for exact term matching (which dominates in technical/factual queries), while ANN helps surface semantically related documents that don't share exact terms.

**Summarization**: After scoring, the system loads raw document texts from the segment, passes them to the Summarizer, and produces an extractive summary. In JSON mode, the summary is included as a `"summary"` field alongside the scored results.

**Source files**: `src/hybrid/hybrid_builder.h/.cpp`, `src/hybrid/hybrid_search.h/.cpp`

---

### Feature 9: Extractive Summarization

The summarizer produces concise answers by extracting and ranking sentences from retrieved documents:

**Sentence splitting**: Text is split on `.`, `!`, `?` boundaries. Fragments shorter than 15 characters are discarded.

**Sentence scoring** (per query):
- +3.0 for each query keyword found (word-boundary aware matching, not substring)
- +3.0 * matched_count bonus when multiple query terms match (rewards comprehensive sentences)
- +0.5 for moderate length (30-300 chars)
- -2.0 for markdown headers (lines starting with `#`)
- -1.0 for list items (lines starting with `-`)

**Document selection**: Only the top 3 documents (by retrieval score) are considered, with a threshold of 30% of the top score. This prevents low-relevance documents from polluting the summary.

**Deduplication**: Before adding a sentence, its first 20 characters are checked against already-selected sentences to avoid near-duplicates.

**Output assembly**: Sentences are distributed across documents (max per-doc limit), ordered by document ranking, and joined into a paragraph. Default limit is 5 sentences.

**Source files**: `src/summarizer/summarizer.h/.cpp`

---

### Feature 10: Query Intent Detection

The QueryAnalyzer uses a two-tier architecture: a unified dispatcher that auto-selects between a neural multi-task classifier and a rule-based fallback.

**Unified dispatcher** (`QueryAnalyzer`):
- At startup, calls `load_neural(model_path, vocab_path)` to attempt loading a trained neural model
- If `qa_model.pt` exists and the build has `HAS_TORCH`, delegates all `analyze()` calls to `NeuralQueryAnalyzer`
- Otherwise, delegates to `RuleBasedQueryAnalyzer`
- Uses pimpl idiom (`struct NeuralImpl`) so torch headers never leak into non-torch translation units

**Runtime selection logic** (which analyzer is actually used):
- Built without libtorch (`USE_TORCH=OFF`): `load_neural()` always returns false → `RuleBasedQueryAnalyzer` is used
- Built with libtorch but no trained model (`qa_model.pt` doesn't exist): `load_neural()` returns false → `RuleBasedQueryAnalyzer` is used
- Built with libtorch and model trained (after `./mysearch train-qa`): `load_neural()` succeeds → `NeuralQueryAnalyzer` is used
- When the neural path is active, stderr prints "Using neural query analyzer" as confirmation
- Deleting `../embeddings/qa_model.pt` reverts to the rule-based fallback automatically

**Rule-based fallback** (`RuleBasedQueryAnalyzer`):

*Intent detection* (from leading words and verb patterns):
- `where/what/who/when` at start -> FACTUAL
- `how/why` at start, or contains `explain/describe` -> EXPLANATION
- Contains `how to/steps to/procedure` -> PROCEDURAL
- Contains `difference between/vs/compare` -> COMPARISON
- Otherwise -> GENERAL

*Answer type detection* (determines answer shape):
- `where` or `located/location of` -> LOCATION
- `who` or `person/inventor/founder` -> PERSON_PROFILE
- `when` or `what year/what date` -> TEMPORAL
- `what` or `define/definition/what is` -> DEFINITION
- `how to/how does` -> PROCEDURE
- Comparison intent -> COMPARISON
- Fallback -> SUMMARY

*Keyword extraction*: Tokenizes the query, removes a comprehensive stop-word list (100+ words including interrogatives, pronouns, prepositions, common verbs), keeps words longer than 1 character.

*Entity extraction*: Heuristic — the longest keyword is assumed to be the main entity. This works well for queries like "where is stockholm" (entity: "stockholm") or "what is a database" (entity: "database").

**Neural classifier** (`NeuralQueryAnalyzer`, optional libtorch) — see Feature 19 for full details.

**Source files**: `src/query/query_analyzer.h/.cpp`, `src/query/neural_query_analyzer.h/.cpp`

---

### Feature 11: Document Chunking

The Chunker splits documents into semantically typed paragraphs for fine-grained evidence selection:

**Paragraph splitting**: Text is split on double-newline (`\n\n`) or heading boundaries (`\n#`). Leading whitespace is trimmed. Paragraphs shorter than 10 characters are discarded.

**Chunk type classification** (keyword heuristics on lowercased text):
- **LOCATION**: "located", "capital", "coast", "island", "city", "region", "country", "bridge", "port", "archipelago", "built across", "connected to"
- **DEFINITION**: "is a", "is an", "refers to", "defined as", "collection of", "aims to", "studies how"
- **PERSON**: "born", "inventor", "founded by", "created by", plus specific names like "alan turing", "charles babbage"
- **TEMPORAL**: "century", specific years (1642, 1990), "ancient", "modern computing", "history", "evolution"
- **PROCEDURE**: "step", "how to", "procedure", "process", "algorithm", "method", "technique"
- **HISTORY**: "history", "heritage", "medieval", "centuries", "political", "economic center", "founded"
- **GENERAL**: fallback when no signals match

Each chunk carries its docId, chunkId (paragraph index), type, and full text. The answer synthesizer uses chunk types to prefer evidence matching the expected answer type.

**Source files**: `src/chunk/chunker.h/.cpp`

---

### Feature 12: Answer-Type-Aware Synthesis

The AnswerSynthesizer generates question-appropriate answers by dispatching to specialized synthesis methods:

**Dispatch logic**: Based on the QueryAnalysis.answerType, calls one of:
- `synthesize_location()` — prefers LOCATION and GENERAL chunks, extracts 2 sentences, max 300 chars
- `synthesize_definition()` — prefers DEFINITION and GENERAL chunks, extracts 3 sentences, max 500 chars
- `synthesize_person()` — prefers PERSON and HISTORY chunks, extracts 3 sentences, max 400 chars
- `synthesize_temporal()` — searches ALL evidence chunks (not just TEMPORAL/HISTORY), uses year-aware scoring, max 300 chars
- `synthesize_summary()` — uses all chunks, extracts 2 sentences per evidence, max 600 chars

**Evidence filtering**: Each method (except temporal) first filters evidence by preferred ChunkTypes. If no type-specific evidence exists, it falls back to all evidence (graceful degradation). The temporal synthesizer always searches all evidence because the most relevant date-containing sentence may reside in a chunk classified as PERSON or GENERAL.

**Sentence extraction**: Within each evidence chunk, sentences are scored by keyword overlap (+3.0 per keyword match, word-boundary aware), length preference (+0.5 for 30-300 chars), and header penalty (-2.0 for lines starting with `#`). Top-scoring sentences are selected.

**Temporal-specific extraction** (`synthesize_temporal`):
- Uses `split_into_segments()` which handles both prose (split on `.`/`!`/`?`) and bullet-point lists (each `- ` line becomes its own segment), solving the problem where bullet items like "Transistor invention enabled miniaturization" were previously invisible to the sentence splitter
- Year-pattern boost: +6.0 for sentences matching `\b(1[0-9]{3}|20[0-9]{2})\b` (any year 1000-2099)
- Entity boost: +4.0 for sentences containing the main entity as a whole word
- Stem-prefix matching: +1.5 when a keyword's stem prefix (last 2 chars stripped) appears in the sentence, enabling "invented" to partially match "invention"
- Collects ALL candidate segments from ALL evidence chunks, scores them globally, then picks the top 3

**Confidence computation**:
```
confidence = 0.4 * coverage + 0.3 * relevance + 0.3 * agreement
```
Where:
- `coverage` = fraction of query keywords found across all evidence
- `relevance` = average evidence score, clamped to [0, 1]
- `agreement` = min(1.0, evidence_count / 3.0) — more evidence chunks = higher agreement

This produces a 0.0-1.0 confidence score. A query about "cats" against a CS/Sweden corpus correctly returns confidence 0.00.

**Source files**: `src/answer/answer_synthesizer.h/.cpp`

---

### Feature 13: Confidence Scoring

Confidence scoring provides a trust indicator for each answer, computed from evidence quality rather than arbitrary heuristics:

**Three components**:
1. **Coverage (40%)**: What fraction of the query's keywords appear somewhere in the evidence? If the query has 3 keywords and only 2 are found, coverage = 0.67. This catches cases where retrieval missed a key concept.
2. **Relevance (30%)**: Average retrieval score of the evidence chunks, clamped to [0, 1]. High BM25/hybrid scores indicate strong lexical/semantic match.
3. **Agreement (30%)**: `min(1.0, evidence_count / 3.0)`. Having 3+ supporting evidence chunks gives full agreement score. A single weak chunk gives only 0.33.

**Practical behavior**:
- "where is stockholm" against the Sweden data: high coverage (stockholm found), high relevance (strong BM25 match), good agreement (multiple location chunks) -> confidence ~0.85
- "what is a cat" against CS/Sweden data: zero coverage (no keywords match), zero relevance, no evidence -> confidence 0.00

The confidence is exposed in both plain text (`Confidence: 0.85`) and JSON (`"confidence": 0.85`) output modes.

**Source files**: `src/answer/answer_synthesizer.cpp` (compute_confidence method)

---

### Feature 14: Conversation Memory

The ConversationState enables follow-up questions without repeating context:

**State stored**:
- `lastEntity_`: The main entity from the most recent answered query (e.g., "stockholm")
- `lastAnswerType_`: The answer type of the most recent query (e.g., LOCATION)

**Resolution logic** (in `apply()`):
- If the new query's `mainEntity` is empty (e.g., "and when was it founded?"), the system reuses `lastEntity_` and adds it to the keywords
- This allows chains like:
  - Q1: "Where is Stockholm?" -> entity=stockholm, type=LOCATION
  - Q2: "And Gothenburg?" -> entity=gothenburg, type=LOCATION (inherits type)
  - Q3: "When was it founded?" -> entity=gothenburg (inherited), type=TEMPORAL

**Lifecycle**: State is updated after each successful answer via `update()`. It can be explicitly cleared via `reset()`.

**Source files**: `src/conversation/conversation_state.h/.cpp`

---

### Feature 15: Sandboxed Command Execution

The Sandbox provides controlled execution of external commands with path-based allowlisting:

**Security model**:
- An allowlist of directory prefixes is configured (e.g., `/usr/bin`, `/bin`)
- Before executing any command, the requested executable path is checked against the allowlist using prefix matching (`rfind(prefix, 0) == 0`)
- Commands outside allowed directories are denied with an error message

**Execution**:
- Uses POSIX `fork()` to create a child process
- Child calls `execvp()` with the command and arguments
- Parent calls `waitpid()` and returns the child's exit code
- If `execvp()` fails, child exits with code 127

**Limitations**:
- Linux/WSL only (uses POSIX APIs)
- No network restrictions (designed for future Landlock integration)
- No resource limits (CPU, memory, time)
- Path check is prefix-based, not canonicalized (symlink bypass possible)

**Usage**: `./mysearch run /bin/echo hello` succeeds; `./mysearch run ./malicious.sh` is denied.

**Source files**: `src/tools/sandbox.h/.cpp`

---

### Feature 16: WAL + Manifest

**Write-Ahead Log (WAL)**:
- Append-only text log for durability
- Each entry is a single line terminated by `\n`
- Implemented by appending raw bytes via `fileutils::append_file()`
- Designed for crash recovery: replay the log to reconstruct state after an unexpected shutdown
- Currently used as infrastructure; full crash recovery replay is not yet implemented

**Manifest**:
- Tracks all segment directories in a newline-delimited text file
- Supports `add_segment()`, `save()`, `load()`
- On load, reads the file line by line and populates the segment list
- On save, writes all segment paths separated by newlines
- Enables future multi-segment search by enumerating available segments

**Source files**: `src/storage/wal.h/.cpp`, `src/storage/manifest.h/.cpp`

---

### Feature 17: JSON Output

All search and QA commands support `--json` flag for machine-readable output:

**Search command** (`mysearch search <query> --json`):
```json
{
  "results": [
    { "doc": 1, "score": 2.96626 },
    { "doc": 2, "score": 0.66301 }
  ]
}
```

**Hybrid command** (`mysearch hybrid <query> --json`):
```json
{
  "results": [
    { "doc": 4, "score": 0.845, "bm25": 6.28, "ann": -0.03 }
  ],
  "summary": "A database is a structured system..."
}
```

**Ask command** (`mysearch ask <query> --json`):
```json
{
  "query": "where is stockholm",
  "intent": "LOCATION",
  "entity": "stockholm",
  "confidence": 0.85,
  "answer": "Stockholm is Sweden's capital, built across 14 islands..."
}
```

JSON strings are properly escaped (quotes, backslashes, newlines). The summary/answer text has `"` escaped to `\"`, `\` to `\\`, and `\n` to `\\n`.

**Source files**: `src/inverted/search_engine.cpp`, `src/hybrid/hybrid_search.cpp`, `src/cli/commands.cpp`

---

### Feature 18: Comprehensive Test Suite

The project includes 76+ GoogleTest cases across 8 test files, fetched via CMake FetchContent (GoogleTest v1.14.0):

**test_varint.cpp** (7 tests):
Round-trip encoding/decoding for 0, small values (42), large values (123456), UINT32_MAX, sequential multi-value streams, and byte-count verification (127 = 1 byte, 128 = 2 bytes).

**test_tokenizer.cpp** (8 tests):
Basic word splitting, lowercase conversion, punctuation removal, number preservation, empty string, only-punctuation, multiple spaces, and mixed delimiters (hyphens, apostrophes).

**test_query_parser.cpp** (8 tests):
Single term parsing, phrase parsing, AND/OR/NOT boolean operators, phrase combined with boolean, empty query handling, and chained boolean left-associativity verification.

**test_segment_io.cpp** (9 tests):
Write-then-read round-trip, postings retrieval with correct TF, term frequency for repeated terms, out-of-range DocID handling, average document length calculation, position storage verification, multiple positions for same term, and cross-document position retrieval.

**test_bm25.cpp** (9 tests):
Search returns results, higher TF scores higher, nonexistent term returns empty, empty tokens returns empty, multi-term search ranks correctly, score_docs filters to specified docs, score_docs with empty input, all scores are positive, and topK limits result count.

**test_search_engine.cpp** (12 tests):
Single term search, AND query (intersection), OR query (union), NOT query (difference), position-aware phrase matching, reversed phrase no-match, phrase OR term combination, nonexistent term, topK limiting, descending score order, cross-topic AND (empty result), and Swedish city search.

**test_hnsw.cpp** (11 tests):
Empty index search, single point, exact match, topK ordering, topK larger than dataset, size tracking, recall measurement on 500 random points (>= 80%), nearest-neighbor exactness (>= 8/10), duplicate points, high-dimensional (128-dim) search, and incremental insert correctness.

**test_hybrid.cpp** (13 tests):
Vocabulary build/lookup, vocab save/load, embedding model init and embed, zero input handling, model save/load round-trip, different inputs produce different outputs, hybrid builder creates embeddings, embeddings are searchable, hybrid search returns results, BM25+ANN combination, descending score order, nonexistent query behavior, and cross-topic search.

All tests are self-contained — they create temporary segment directories, run assertions, and clean up via SetUp/TearDown fixtures.

**Test files**: `tests/test_varint.cpp`, `tests/test_tokenizer.cpp`, `tests/test_query_parser.cpp`, `tests/test_segment_io.cpp`, `tests/test_bm25.cpp`, `tests/test_search_engine.cpp`, `tests/test_hnsw.cpp`, `tests/test_hybrid.cpp`

---

### Feature 19: Neural Query Analyzer (Optional, libtorch)

When built with `USE_TORCH=ON`, the system includes a transformer-based multi-task query classifier that replaces the hardcoded rule-based query analyzer with a learned model.

**Motivation**: The rule-based analyzer relies on brittle pattern matching (e.g., `starts_with("when")` → TEMPORAL). It cannot handle paraphrased queries like "in what year did...", "tell me the date of...", or queries in unexpected word order. The neural analyzer learns these patterns from data.

**Model architecture** (`QueryClassifier`):
- Token embedding (vocab_size + 1 for UNK) + fixed sinusoidal positional encoding
- TransformerEncoder (default: 2 layers, 4 attention heads, GELU activation, 4x FFN dim)
- Three classification heads:
  - **Intent head**: Linear(dim, 5) on mean-pooled output → FACTUAL / EXPLANATION / PROCEDURAL / COMPARISON / GENERAL
  - **Answer type head**: Linear(dim, 7) on mean-pooled output → LOCATION / DEFINITION / PERSON_PROFILE / TEMPORAL / PROCEDURE / COMPARISON / SUMMARY
  - **Entity head**: Linear(dim, 3) on per-token output → BIO tags (B=begin entity, I=inside entity, O=outside)

**Training data generation** (`generate_training_data`):
- Extracts entities from corpus documents using:
  - Capitalized multi-word sequences via regex (e.g., "Alan Turing", "Stockholm")
  - Uppercase acronyms (e.g., "TCP", "HTTP")
  - Long words (≥5 chars) from tokenized text
- Combines each entity with 22 question templates covering all intent/answer types:
  - "where is {entity}", "where is {entity} located" → LOCATION
  - "what is {entity}", "what is a {entity}", "define {entity}" → DEFINITION
  - "who is {entity}", "who invented {entity}" → PERSON_PROFILE
  - "when was {entity} invented", "what year was {entity} invented" → TEMPORAL
  - "how does {entity} work", "how to use {entity}" → PROCEDURE
  - "explain {entity}", "tell me about {entity}" → SUMMARY
  - "difference between {entity} and something" → COMPARISON
- Generates BIO entity tags by locating the entity words within each synthetic query
- Produces ~27K training samples from the default corpus (scales with corpus size)

**Training** (`NeuralQueryAnalyzer::train`):
- Optimizer: AdamW (lr=1e-3, weight_decay=0.01)
- Multi-task loss: `L_intent + L_answer_type + 0.5 * L_entity`
  - Intent and answer type: standard cross-entropy
  - Entity: cross-entropy masked to non-padded tokens only (padded positions excluded from loss)
- Default: 30 epochs, batch size 8, max sequence length 64 tokens
- Training loss converges from ~0.30 to ~0.18 on the default corpus

**Inference** (`NeuralQueryAnalyzer::analyze`):
1. Tokenize query using the shared `Vocabulary` (same as sentence encoder)
2. Forward through `QueryClassifier` → intent logits, answer type logits, entity logits
3. Argmax on intent and answer type logits for classification
4. Argmax on per-token entity logits for BIO tags; decode B→I* spans to extract entity string
5. Fallback: if no entity tagged by the model, use the longest non-stop keyword (same heuristic as rule-based)
6. Keywords extracted by filtering stop words (same as rule-based)

**Integration**:
- The `ask` command calls `analyzer.load_neural(embeddir + "/qa_model.pt", embeddir + "/vocab.txt")` at startup
- If the model file exists and `HAS_TORCH` is defined, all subsequent `analyze()` calls use the neural path
- If not, the rule-based fallback is used transparently
- The `train-qa` CLI command trains the model: `./mysearch train-qa --epochs 30`

**Verified predictions** (after 30 epochs on default corpus):
```
"where is stockholm"                  → LOCATION   entity="stockholm"
"what is a database"                  → DEFINITION entity="database"
"who is alan turing"                  → PERSON     entity="alan turing"
"when was the transistor invented"    → TEMPORAL   entity="transistor"
"how does TCP work"                   → PROCEDURE  entity="tcp"
```

**Source files**: `src/query/neural_query_analyzer.h/.cpp`, `src/query/query_analyzer.h/.cpp` (unified dispatcher), `src/cli/commands.cpp` (train-qa command)

---

## 8. Notable Design Decisions

- No SQLite, no Lucene, no Tantivy — Everything is hand-written binary formats.
- Single-segment MVP — Multi-segment merge is designed for but not yet fully implemented in the search path.
- Position sentinel trick — Delta positions shifted by +1 so 0 can serve as end marker.
- Dual embedding paths — BoW fallback always works; neural encoder auto-detected at runtime via #ifdef HAS_TORCH and file existence check.
- Dual query analyzer paths — Rule-based fallback always works; neural multi-task classifier auto-detected at runtime via qa_model.pt existence check. Pimpl idiom prevents torch headers from leaking into non-torch builds.
- Sandbox is Linux-only — Uses fork()/execvp()/waitpid(), won't compile on native Windows.
- No stemming or stop-word removal in tokenizer — Stop words are handled separately in QueryAnalyzer and Summarizer.

---

## 9. File-by-File Reference

### Headers (27 files)
  src/common/types.h
  src/common/varint.h
  src/common/file_utils.h
  src/storage/segment_writer.h
  src/storage/segment_reader.h
  src/storage/manifest.h
  src/storage/wal.h
  src/inverted/tokenizer.h
  src/inverted/bm25.h
  src/inverted/query_parser.h
  src/inverted/phrase_matcher.h
  src/inverted/index_builder.h
  src/inverted/index_reader.h
  src/inverted/search_engine.h
  src/hnsw/hnsw_node.h
  src/hnsw/hnsw_index.h
  src/embedding/vocab.h
  src/embedding/embedding_model.h
  src/encoder/sentence_encoder.h
  src/encoder/encoder_trainer.h
  src/hybrid/hybrid_builder.h
  src/hybrid/hybrid_search.h
  src/summarizer/summarizer.h
  src/query/query_analyzer.h
  src/query/neural_query_analyzer.h
  src/chunk/chunker.h
  src/answer/answer_synthesizer.h
  src/conversation/conversation_state.h
  src/cli/commands.h
  src/tools/sandbox.h

### Source files (31 files)
  src/common/varint.cpp
  src/common/file_utils.cpp
  src/storage/segment_writer.cpp
  src/storage/segment_reader.cpp
  src/storage/manifest.cpp
  src/storage/wal.cpp
  src/inverted/tokenizer.cpp
  src/inverted/bm25.cpp
  src/inverted/query_parser.cpp
  src/inverted/phrase_matcher.cpp
  src/inverted/index_builder.cpp
  src/inverted/index_reader.cpp
  src/inverted/search_engine.cpp
  src/hnsw/hnsw_node.cpp
  src/hnsw/hnsw_index.cpp
  src/embedding/vocab.cpp
  src/embedding/embedding_model.cpp
  src/encoder/sentence_encoder.cpp
  src/encoder/encoder_trainer.cpp
  src/encoder/train_main.cpp
  src/hybrid/hybrid_builder.cpp
  src/hybrid/hybrid_search.cpp
  src/summarizer/summarizer.cpp
  src/query/query_analyzer.cpp
  src/query/neural_query_analyzer.cpp
  src/chunk/chunker.cpp
  src/answer/answer_synthesizer.cpp
  src/conversation/conversation_state.cpp
  src/cli/main.cpp
  src/cli/commands.cpp
  src/tools/sandbox.cpp

### Test files (8 files)
  tests/test_varint.cpp
  tests/test_tokenizer.cpp
  tests/test_query_parser.cpp
  tests/test_segment_io.cpp
  tests/test_bm25.cpp
  tests/test_search_engine.cpp
  tests/test_hnsw.cpp
  tests/test_hybrid.cpp

---

## 10. CLI Usage Reference

### Ingest documents
  ./mysearch ingest ../data

### BM25 search (boolean + phrase)
  ./mysearch search "database"
  ./mysearch search "firewall AND security"
  ./mysearch search '"machine learning"'
  ./mysearch search '"quick brown" OR testing' --json

### Bootstrap BoW embeddings + HNSW
  ./mysearch build-hnsw

### Hybrid BM25+ANN search
  ./mysearch hybrid "what is a database"
  ./mysearch hybrid "stockholm sweden" --json

### Train neural encoder (requires libtorch build)
  ./train_encoder --epochs 10 --dim 128

### Train neural query analyzer (requires libtorch build)
  ./mysearch train-qa --epochs 30

### Question-answering (auto-loads neural query analyzer if qa_model.pt exists)
  ./mysearch ask "where is stockholm"
  ./mysearch ask "what is a database" --json
  ./mysearch ask "who is alan turing"
  ./mysearch ask "when was the transistor invented"
  ./mysearch ask "how does TCP work"

### Sandboxed command execution
  ./mysearch run /bin/echo hello
