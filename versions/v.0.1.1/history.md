# v.0.1.1 Change History

## Step 1: Refactor QueryAnalyzer → InformationNeed Model

### Goal

Replace the old `Query → Intent (WHERE/WHAT/WHO) → Answer` pipeline with a cognitive **InformationNeed** model that works at the information-need level, not the interrogative-word level.

### Core Abstraction

A query now produces one or more **InformationNeeds**, each consisting of:

- **Entity** — what the question is about
- **Property** — which aspect is requested (11 values: LOCATION, DEFINITION, FUNCTION, COMPOSITION, HISTORY, TIME, COMPARISON, ADVANTAGES, LIMITATIONS, USAGE, GENERAL)
- **AnswerForm** — how the answer should look (SHORT_FACT, EXPLANATION, LIST, COMPARISON, SUMMARY)
- **Keywords** — content words for retrieval

### Files Created

| File | Description |
|------|-------------|
| `src/query/information_need.h` | New data model: `Property`, `AnswerForm`, `InformationNeed` struct, helper `property_str()` / `answer_form_str()` |
| `CMakeLists.txt` | Build config for v.0.1.1 |

### Files Rewritten

| File | What Changed |
|------|-------------|
| `src/query/query_analyzer.h` | `analyze()` returns `std::vector<InformationNeed>`; legacy `QueryAnalysis` kept for neural compatibility via `analyze_legacy()` |
| `src/query/query_analyzer.cpp` | Clause splitting (e.g. `"where is X and why is it important"` → 2 needs), property detection from semantic signals instead of interrogative words, entity propagation across clauses |
| `src/answer/answer_synthesizer.h` | Accepts `InformationNeed`; new `CompositeAnswer` for multi-need merging; `Answer.property` replaces `Answer.answerType` |
| `src/answer/answer_synthesizer.cpp` | Dispatches by `Property` then `AnswerForm`; dedicated synthesizers for location, definition, temporal, explanation, list, general |
| `src/conversation/conversation_state.h` | Works with `InformationNeed` vector instead of `QueryAnalysis` |
| `src/conversation/conversation_state.cpp` | Propagates entity across `InformationNeed` vector |
| `src/cli/commands.cpp` | `ask` command loops over needs: retrieve → chunk → synthesize per need; JSON output shows each need separately |

### Files NOT Modified

| File | Reason |
|------|--------|
| `src/query/neural_query_analyzer.h/.cpp` | Still produces `QueryAnalysis`; mapped to single `InformationNeed` in `QueryAnalyzer::analyze()` — no retraining needed |
| `src/chunk/chunker.h/.cpp` | Unchanged; chunk types already align with properties |
| `src/summarizer/summarizer.h/.cpp` | Unchanged; used only by `hybrid` command |

### Key Architectural Shift

| Before | After |
|--------|-------|
| `Query → 1 Intent + 1 AnswerType` | `Query → N InformationNeeds` |
| Dispatch on interrogative words (`where`, `what`, `who`) | Dispatch on semantic signals (`locat`, `coast`, `capital`, `advantage`, etc.) |
| Single answer output | `CompositeAnswer` with per-need parts |

### Example

Query: `"Tell me where Stockholm is and why it is important"`

**Before:** 1 result → LOCATION

**After:**
```
Need 1: Entity=Stockholm, Property=LOCATION, Form=SHORT_FACT
Need 2: Entity=Stockholm, Property=HISTORY, Form=EXPLANATION
```

---

## Step 2: Full Pipeline — Hybrid Retrieval + Property-Aware Chunk Selection

### Goal

Upgrade the `ask` pipeline from BM25-only retrieval to **Hybrid Retrieval (BM25 + HNSW)** with graceful fallback, and add **property-aware chunk selection** via `Chunker::select_chunks`.

### Pipeline (after Step 2)

```
User Query
  ↓
QueryAnalyzer
  ↓
InformationNeeds[]
  ↓
For each need:
  ├─ Hybrid Retrieval (BM25 + HNSW, or BM25-only fallback)
  ├─ Chunk Selection (by Property)
  └─ Answer Synthesis
  ↓
CompositeAnswer (merged)
```

### Files Modified

| File | What Changed |
|------|-------------|
| `src/chunk/chunker.h` | Added `static select_chunks(chunks, property, max_chunks)` method |
| `src/chunk/chunker.cpp` | Implemented `select_chunks`: maps `Property` → preferred `ChunkType`s, partitions chunks into preferred-first ordering, limits to `max_chunks` |
| `src/cli/commands.cpp` | `ask` command now: (1) auto-detects `model.bin` + `vocab.txt` to enable hybrid retrieval, (2) builds HNSW on the fly, (3) fuses BM25 + ANN scores per need (0.7/0.3 weighting), (4) uses `Chunker::select_chunks` instead of inline score boosting. JSON output includes `"retrieval": "hybrid"` or `"bm25"`. Removed unused `preferred_chunks()` helper. Added `<memory>` include. |

### Key Behavior

- **With embeddings** (`embeddings/model.bin` + `embeddings/vocab.txt` present after `build-hnsw`):
  - `ask` uses hybrid retrieval, stderr shows `Using hybrid retrieval (BM25 + HNSW)`
  - JSON output includes `"retrieval": "hybrid"`
- **Without embeddings** (fresh build, no `build-hnsw` run):
  - `ask` falls back to BM25-only, works exactly as before
  - JSON output includes `"retrieval": "bm25"`

### Chunk Selection Logic

`Chunker::select_chunks(chunks, property, max_chunks)` works as follows:
1. Maps `Property` to a set of preferred `ChunkType`s (e.g. `LOCATION` → `{LOCATION, GENERAL}`)
2. Partitions chunks: preferred types first, then others
3. Returns up to `max_chunks` (default 10) chunks per document

---

## Step 3: Expand Corpus Data for 10 Benchmark Questions

### Goal

Enrich the text files in `data/` so that all 10 benchmark questions (plus the build.md examples) can find accurate, relevant answers through the InformationNeed pipeline.

### Questions Covered

| # | Question | Content Added To |
|---|----------|-----------------|
| 1 | "Stockholm vs Gothenburg: which is better for living?" | `stockholm.txt` (quality of life, cost of living), `gothenburg.txt` (same) |
| 2 | "Why is SQL still widely used?" | `databases.txt` (new "Why SQL Is Still Widely Used" section) |
| 3 | "What are the limitations of NoSQL databases?" | `databases.txt` (new "Limitations of NoSQL Databases" section) |
| 4 | "Explain how TCP ensures reliability" | `networking.txt` (new "How TCP Ensures Reliability" section) |
| 5 | "Give me an overview of database types and their use cases" | `databases.txt` (new "Types of Databases and Their Use Cases" section) |
| 6 | "Is Stockholm close to the sea?" | `stockholm.txt` (explicit "close to the sea" in geography section) |
| 7 | "When did computer networking start becoming mainstream?" | `networking.txt` (new "History and Timeline" section with 1990s mainstream date) |
| 8 | "What makes an algorithm scalable?" | `algorithms.txt` (new "What Makes an Algorithm Scalable" section) |
| 9 | "Databases for beginners — what should I start with?" | `databases.txt` (new "Databases for Beginners" section) |
| 10 | "How is Sweden connected to continental Europe?" | `malmo.txt` (expanded Oresund Bridge, ferry, air connections) |

### Files Modified

| File | Changes |
|------|---------|
| `data/sweden/stockholm.txt` | Added: Geography section (eastern coast, Baltic Sea, close to sea), Quality of Life, Cost of Living sections |
| `data/sweden/gothenburg.txt` | Added: Geography section, Quality of Life, Cost of Living, Economy sections |
| `data/sweden/malmo.txt` | Expanded: Geography section with Oresund Bridge details, ferry/air connections to continental Europe |
| `data/computer_science/databases.txt` | Added: Types and Use Cases, Why SQL Is Still Widely Used, Limitations of NoSQL, Databases for Beginners sections |
| `data/computer_science/networking.txt` | Added: History and Timeline section (1960s–2000), How TCP Ensures Reliability section (8 mechanisms) |
| `data/computer_science/algorithms.txt` | Added: What Makes an Algorithm Scalable section (6 factors) |
| `data/misc/history_of_computing.txt` | Added: explicit transistor invention date (1947, Bell Labs), integrated circuits (1958), more timeline dates |

### Additional build.md Examples Also Covered

- "where is stockholm" → Geography section with "eastern coast" and "Baltic Sea"
- "is stockholm close to the sea" → explicit "close to the sea" phrase
- "what is a database" → existing + expanded definition
- "when was the transistor invented" → "1947 at Bell Labs"
- "how does TCP ensure reliability" → 8-mechanism detailed section
- "what are the benefits of SQL" → Why SQL Is Still Widely Used section
- "what are the drawbacks of NoSQL" → Limitations of NoSQL section
- "databases for beginners" → Databases for Beginners section
- "difference between SQL and NoSQL" → both sections present for comparison
- "what are the types of databases" → Types of Databases and Their Use Cases

---

## Step 4: General Question Understanding Without LLMs

### Goal

Formalize the four deterministic mechanisms for general question understanding: semantic prototype scoring, chunk typing, evidence-driven answering, and conversation memory.

### Principle

Instead of `Sentence → Category`, we map `Sentence → Entities → Properties (scored) → Constraints → Answer Form`. Information needs are finite even though language varies infinitely.

### Mechanism 1: Property Detection via Semantic Prototypes

Replaced hard if/else chain in `detect_property` with a **scoring system**. Each property has a `PropertyPrototype` containing a vocabulary of signal words and a base weight:

```cpp
struct PropertyPrototype {
    Property property;
    std::vector<std::string> signals;
    double weight;
};
```

A query clause is scored against all prototypes simultaneously. The highest-scoring property wins, but the score is preserved in `InformationNeed::property_score` for downstream use. This is **scoring, not classification** — a query can activate multiple properties.

### Mechanism 2: Chunk Typing During Ingestion (unchanged)

Already implemented in Steps 1–3. Document chunks are classified by semantic type at ingestion time. At query time, `select_chunks` uses keyword relevance + type preference to select the most relevant chunks.

### Mechanism 3: Evidence-Driven Answering (unchanged)

Already implemented. Answers are synthesized from selected evidence with confidence scoring based on keyword coverage, relevance, and agreement between chunks. No hallucination, no guessing.

### Mechanism 4: Conversation Memory (newly integrated)

`ConversationState` was already implemented but not wired into the `ask` pipeline. Now integrated:
- Before retrieval: `conversation.apply(needs)` resolves ellipsis by propagating the last entity to needs with empty entities
- After synthesis: `conversation.update(need)` stores the current entity and property

This enables natural follow-up questions like:
```
> ask "where is stockholm"     → Entity: stockholm, Property: LOCATION
> ask "why is it important"    → Entity: stockholm (from memory), Property: HISTORY
```

### Files Modified

| File | What Changed |
|------|-------------|
| `src/query/information_need.h` | Added `property_score` field to `InformationNeed` |
| `src/query/query_analyzer.cpp` | Replaced if/else `detect_property` with `PropertyPrototype` scoring system via `score_properties()`. Each property has a signal vocabulary with weights. Stores `property_score` in each need. |
| `src/cli/commands.cpp` | Integrated `ConversationState`: `apply()` before retrieval, `update()` after synthesis. Added `property_score` to JSON output. |

### Files NOT Modified

| File | Reason |
|------|--------|
| `src/chunk/chunker.cpp` | Mechanism 2 already complete from Steps 1–3 |
| `src/answer/answer_synthesizer.cpp` | Mechanism 3 already complete from Steps 1–3 |
| `src/conversation/conversation_state.h/.cpp` | Already implemented, just needed wiring |

---

## Step 5: Property Detection Heuristics, Chunk Typing, and Typed Synthesizers

### Goal

Complete the property-driven architecture with three improvements:
1. Expanded property detection prototypes
2. New chunk types for FUNCTION, USAGE, ADVANTAGES, LIMITATIONS
3. Dedicated answer synthesizers for all major property types

### 5.1 Property Detection Heuristics

Expanded `PropertyPrototype` signal vocabularies in `query_analyzer.cpp`:
- LOCATION: added `sea`, `lake`, `island`
- TIME: added `period`, `date`
- ADVANTAGES: added `widely used`
- USAGE: added `suitable`
- HISTORY: added `developed`, `introduced`
- DEFINITION: added `means`
- COMPARISON: added `better than`

### 5.2 Chunk Typing Rules

Added 4 new `ChunkType` values: `FUNCTION`, `USAGE`, `ADVANTAGES`, `LIMITATIONS`.

New `classify_chunk` signals:
- ADVANTAGES: `advantage`, `benefit`, `strength`, `widely used`, `mature ecosystem`
- LIMITATIONS: `limitation`, `drawback`, `disadvantage`, `vendor lock`
- USAGE: `used for`, `use case`, `beginner`, `learning path`, `recommend`
- FUNCTION: `ensures`, `mechanism`, `handshake`, `retransmit`, `flow control`, `congestion`, `checksum`

Updated `preferred_types_for` to map properties to the new chunk types.

### 5.3 Typed Answer Synthesizers

Added 5 dedicated synthesizers using a shared `scored_segments` helper:

| Synthesizer | Boost Words | Max Segments |
|-------------|-------------|-------------|
| `synthesize_advantages` | advantage, benefit, strength, widely, proven, mature, standardiz, reliable, powerful | 6 |
| `synthesize_limitations` | limitation, drawback, disadvantage, lack, not suitable, weaker, vendor lock, costly | 6 |
| `synthesize_usage` | used for, use case, beginner, start with, recommend, suitable, learning path, best for | 6 |
| `synthesize_history` | history, origin, founded, century, developed, introduced, evolved, heritage | 4 |
| `synthesize_comparison` | vs, compare, difference, better, worse, more, less, affordable, expensive, cheaper | 6 |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/query/query_analyzer.cpp` | Expanded property prototype signal vocabularies |
| `src/chunk/chunker.h` | Added `FUNCTION`, `USAGE`, `ADVANTAGES`, `LIMITATIONS` to `ChunkType` enum |
| `src/chunk/chunker.cpp` | Added classify signals for new chunk types; updated `preferred_types_for` |
| `src/answer/answer_synthesizer.h` | Added 5 new synthesizer declarations |
| `src/answer/answer_synthesizer.cpp` | Added `scored_segments` helper, 5 typed synthesizers, updated `preferred_chunks_for` and dispatch |

### 5.4 JSON Structured Answers + Source Tracking

Added `sources` field to `Answer` struct — a list of unique document IDs that contributed evidence to the answer. The main `synthesize` dispatch now collects doc IDs from evidence after synthesis.

**JSON output format (new):**

```json
{
  "query": "where is stockholm",
  "retrieval": "hybrid",
  "needs": [
    {
      "entity": "stockholm",
      "property": "LOCATION",
      "property_score": 3.0,
      "form": "SHORT_FACT",
      "answer": {
        "text": "Stockholm is situated on the eastern coast...",
        "confidence": 0.95
      },
      "sources": [1, 3, 7]
    }
  ],
  "confidence": 0.95
}
```

**Changes vs previous JSON format:**
- `answer` is now a nested object with `text` and `confidence` (was flat `"answer": "..."` + `"confidence": 0.95`)
- Added `sources` array with document IDs used as evidence
- Entity string is now properly escaped in JSON

**Plain text output** also shows `Sources: [1, 3, 7]` line.

### Files Modified

| File | What Changed |
|------|-------------|
| `src/answer/answer_synthesizer.h` | Added `std::vector<uint32_t> sources` to `Answer` struct |
| `src/answer/answer_synthesizer.cpp` | Main `synthesize` dispatch restructured to collect unique doc IDs from evidence |
| `src/cli/commands.cpp` | JSON output uses nested `answer` object + `sources` array; plain text shows sources |

---

## Step 6: Phase 3 Completion — Weak Reasoning (Dependent Planning, Self-Ask, Conflict Detection)

### Goal

Complete Phase 3 by implementing the three missing reasoning mechanisms:
1. Dependent answer planning (answers are ordered and dependent)
2. Self-ask validation (verify answer addresses the property)
3. Conflict detection (reduce confidence on contradicting evidence)

### 6.1 Dependent Answer Planning

When processing multiple InformationNeeds from a single query, each need's answer now receives the previous need's answer as `prior_context`. This enables logical ordering:

- "Where is Stockholm and why is it important?" → Need 2 (IMPORTANCE) receives Need 1's LOCATION answer as context
- The `prior_context` field is exposed in JSON output as `used_prior_context: true`

### 6.2 Self-Ask Validation

New `AnswerValidator::validate()` checks whether the synthesized answer actually addresses the requested property by counting property-specific signal words:

- Each Property has an expected signal vocabulary (e.g., LOCATION expects "located", "capital", "coast", "sea", etc.)
- If 0 signals found → `validated=false`, confidence × 0.3, note explains the failure
- If <15% signals found → `validated=true` but confidence × 0.7, note warns "Weak property match"
- If ≥15% signals → `validated=true`, no penalty

This catches cases where the synthesizer returns irrelevant text (e.g., museum info for a LOCATION query).

### 6.3 Conflict Detection

New `AnswerValidator::detect_conflicts()` checks for contradicting factual claims across evidence chunks:

- Extracts sentences containing the entity from each evidence chunk
- Compares sentence pairs from different documents
- If one sentence has negation words ("not", "no", "never", "without", "lack") and the other doesn't → conflict detected
- Conflict ratio is converted to a confidence penalty (max -0.3)
- Note "Evidence conflict detected." is added to the answer

### Files Created

| File | Description |
|------|-------------|
| `src/answer/answer_validator.h` | `AnswerValidator` class declaration |
| `src/answer/answer_validator.cpp` | Self-ask validation + conflict detection implementation |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/answer/answer_synthesizer.h` | Added `validated`, `validation_note`, `prior_context` fields to `Answer` struct |
| `src/cli/commands.cpp` | Integrated `AnswerValidator` into pipeline: prior context propagation, conflict detection, self-ask validation. JSON/text output shows validation metadata. |
| `CMakeLists.txt` | Added `answer_validator.cpp` to build |

### Pipeline After Step 6

```
Query → Analyze → InformationNeeds[]
  ↓
For each need (sequentially):
  ├─ Retrieve (BM25 + HNSW)
  ├─ Chunk Select (keyword + type)
  ├─ Synthesize
  ├─ Attach prior_context from previous need (6.1)
  ├─ Detect conflicts in evidence (6.3)
  ├─ Self-ask validate answer vs property (6.2)
  └─ Store answer as context for next need
  ↓
CompositeAnswer (with validation metadata)
```

### Phase 3 Completion Status

| Requirement | Status |
|---|---|
| 3.1 Question decomposition (ordered, dependent) | ✅ Prior context propagation |
| 3.2 Self-ask (internal checks) | ✅ Signal-word validation |
| 3.3 Answer consistency & confidence | ✅ Conflict detection + confidence penalty |
| 3.4 Conversation memory | ✅ File-persisted (from Step 4) |

---

## Step 7: QA Integration Test Suite

### Goal

Add an automated integration test script that runs benchmark queries against the QA pipeline and verifies correctness of property detection, answer content, and validation status.

### Test Script

`tests/test_qa_integration.sh` — a bash script that:
1. Runs 13 benchmark queries via `./mysearch ask "..." --json`
2. Parses JSON output with `python3` to extract property, answer text, validated status, and confidence
3. Checks each answer against expected property, content keywords, and validation status
4. Reports PASS/FAIL per test with confidence scores
5. Exits with code 0 if all pass, 1 if any fail

### Test Cases (13 total)

| Category | Test | Property | Expected Keywords |
|----------|------|----------|-------------------|
| Core | Where is Stockholm | LOCATION | eastern, coast, sweden, sea |
| Core | Stockholm close to sea (implicit) | LOCATION | sea, stockholm |
| Core | What is a database | DEFINITION | database, data |
| Core | How does TCP ensure reliability | FUNCTION | tcp |
| Core | Drawbacks of NoSQL | LIMITATIONS | nosql, limitation |
| Core | Databases for beginners | USAGE | beginner |
| Core | When did networking become mainstream | TIME | 1990 |
| Core | TCP reliability (explain) | FUNCTION | tcp |
| Core | Stockholm close to sea (benchmark) | LOCATION | sea |
| Multi-need | Stockholm location (need 0) | LOCATION | eastern, coast, sea |
| Multi-need | Stockholm importance (need 1) | ADVANTAGES | stockholm, important |
| Validation | NoSQL limitations validated | LIMITATIONS | nosql (validated=true) |
| Validation | Stockholm location validated | LOCATION | coast (validated=true) |

### How to Run

```bash
cd build
./mysearch ingest ../data
./mysearch build-hnsw
bash ../tests/test_qa_integration.sh
```

### Results

All 13 tests pass with the current codebase and data:

```
Results: 13 passed, 0 failed, 13 total
```

### Files Created

| File | Description |
|------|-------------|
| `tests/test_qa_integration.sh` | Bash integration test script (requires python3 for JSON parsing) |

### Files Modified

| File | What Changed |
|------|-------------|
| `build.md` | Added QA integration test section with usage instructions and expected output |
