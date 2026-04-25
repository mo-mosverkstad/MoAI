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

---

## Step 8: Phase 3 Completion — QuestionPlanner + SelfAskModule

### Goal

Complete the two remaining Phase 3 requirements: explicit question planning with dependency ordering, and self-ask internal sub-question generation.

### 8.1 QuestionPlanner

New `QuestionPlanner` class (`src/answer/question_planner.h/.cpp`) that builds a dependency-ordered plan from InformationNeeds:

- **Priority ordering**: LOCATION/DEFINITION (0) → TIME (1) → HISTORY/FUNCTION/COMPOSITION (2) → USAGE/ADVANTAGES/LIMITATIONS (3) → COMPARISON (4) → GENERAL (5)
- **Dependency edges**: each need depends on the closest prerequisite with strictly lower priority
- **Stable sort**: preserves original order for same-priority needs

Example: "Where is Stockholm and why is it important?" → LOCATION (priority 0) is processed before ADVANTAGES (priority 3), with a dependency edge (0→1).

### 8.2 SelfAskModule

New `SelfAskModule` class (`src/answer/self_ask.h/.cpp`) that generates internal sub-questions before synthesis:

| Property | Internal Sub-Questions (keywords checked) |
|----------|------------------------------------------|
| LOCATION | located, country, region |
| ADVANTAGES | political, economic, cultural + capital, government, innovation |
| LIMITATIONS | standardization, consistency, scalability |
| FUNCTION | mechanism, process, ensures |
| HISTORY | founded, century, developed |
| DEFINITION | is a, refers to, system |
| COMPARISON | better, worse, difference |

`check_support_coverage()` measures what fraction of sub-question keywords appear in the evidence. If coverage < 50%, confidence is reduced by `(0.5 + coverage)` factor and a note is added.

### Pipeline After Step 8

```
Query → Analyze → InformationNeeds[]
  ↓
QuestionPlanner.build() → ordered QuestionPlan with dependencies
  ↓
For each need (in dependency order):
  ├─ SelfAsk: generate internal sub-questions
  ├─ Retrieve (BM25 + HNSW)
  ├─ Chunk Select (keyword + type)
  ├─ SelfAsk: check support coverage
  ├─ Synthesize
  ├─ Attach prior_context (dependent planning)
  ├─ Detect conflicts
  ├─ Validate answer vs property
  ├─ Apply support coverage penalty
  ├─ Retry with BM25-only if validation fails
  └─ Store answer as context for next need
  ↓
CompositeAnswer (with full reasoning metadata)
```

### Phase 3 Final Status

| # | Requirement | Status |
|---|---|---|
| 1 | Question Planning (`QuestionPlan` + `QuestionPlanner`) | ✅ Explicit dependency graph with priority ordering |
| 2 | Self-Ask (`SelfAskModule`) | ✅ Internal sub-questions with coverage check |
| 3 | Evidence Agreement & Confidence | ✅ Multi-factor + conflict + validation + self-ask coverage |
| 4 | Conversation State | ✅ File-persisted, cross-process |
| ✓ | Multi-part answers sequential & contextual | ✅ |
| ✓ | Follow-ups work naturally | ✅ |
| ✓ | Confidence reflects evidence strength | ✅ |
| ✓ | System can explain why it answered | ✅ validation_note + self-ask coverage |

**All Phase 3 requirements are now complete.**

### Files Created

| File | Description |
|------|-------------|
| `src/answer/question_planner.h` | `QuestionPlan` struct + `QuestionPlanner` class |
| `src/answer/question_planner.cpp` | Priority-based dependency ordering |
| `src/answer/self_ask.h` | `SelfAskModule` class |
| `src/answer/self_ask.cpp` | Property-specific sub-question generation + coverage check |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/cli/commands.cpp` | Integrated `QuestionPlanner` (reorders needs) and `SelfAskModule` (coverage check + confidence adjustment) |
| `CMakeLists.txt` | Added `question_planner.cpp` and `self_ask.cpp` to build |

### Integration Test Results

All 13 tests pass after Phase 3 completion:

```
Results: 13 passed, 0 failed, 13 total
```

---

## Step 8b: Rewrite QuestionPlanner + SelfAsk to Match Spec

### Changes from Step 8

**QuestionPlanner** rewritten with:
- Explicit pairwise `depends_on(a, b)` rules instead of priority-based ordering:
  - HISTORY depends on LOCATION
  - ADVANTAGES depends on LOCATION + DEFINITION
  - FUNCTION depends on DEFINITION
  - LIMITATIONS depends on DEFINITION
  - COMPARISON depends on DEFINITION
  - USAGE depends on DEFINITION
- Topological sort (one need placed per iteration for stable ordering)

**SelfAskModule** rewritten with:
- `expand()` generates actual `InformationNeed` sub-needs (not just keyword checks):
  - ADVANTAGES → DEFINITION + LOCATION
  - COMPARISON → DEFINITION
  - LIMITATIONS → USAGE
  - HISTORY → TIME + DEFINITION
  - FUNCTION → DEFINITION
- Sub-needs are marked `is_support = true` and filtered from output
- Sub-needs are deduplicated before adding to the plan
- Coverage check still runs post-synthesis for confidence adjustment

**Pipeline flow** now matches the spec:
```cpp
// Step 1: expand needs with self-ask sub-needs
for (auto& n : baseNeeds) {
    auto support = selfAsk.expand(n);
    expanded.insert(expanded.end(), support.begin(), support.end());
}
// Step 2: plan execution order
QuestionPlan plan = planner.build(expanded);
// Step 3: execute in dependency order
```

**InformationNeed** struct: added `bool is_support = false` to distinguish user-facing needs from internal sub-needs.

### Integration Test Results

All 13 tests pass: `Results: 13 passed, 0 failed, 13 total`


---

## Step 9: Layered Architecture Verification

### Goal

Verify the `ask` pipeline matches the final layered architecture spec.

### Architecture Mapping

```
┌─────────────────────────┐
│     QueryAnalyzer       │  Phase 1  → analyzer.analyze(query)
└──────────┬──────────────┘
           ↓
┌─────────────────────────┐
│  ConversationState      │  Phase 3  → conversation.apply(needs)
└──────────┬──────────────┘
           ↓
┌─────────────────────────┐
│  SelfAsk + Planner      │  Phase 3  → selfAsk.expand() + planner.build()
└──────────┬──────────────┘
           ↓
┌─────────────────────────┐
│  Retrieval Layer        │  Phase 2  → BM25 + HNSW fusion
│  + Chunk Selection      │           → Chunker::select_chunks()
└──────────┬──────────────┘
           ↓
┌─────────────────────────┐
│  AnswerSynthesizer      │  Phase 2  → synthesizer.synthesize()
│  + AnswerValidator      │  Phase 3  → validator.validate() + detect_conflicts()
└──────────┬──────────────┘
           ↓
┌─────────────────────────┐
│  ConversationState      │  Phase 3  → conversation.update() + save()
└─────────────────────────┘
```

### Main Loop (as implemented in commands.cpp)

```cpp
// Phase 1: Analyze
auto needs = analyzer.analyze(query);
conversation.apply(needs);

// Phase 3: Expand + Plan
for (auto& n : needs) {
    auto support = selfAsk.expand(n);
    expanded.insert(expanded.end(), support.begin(), support.end());
}
auto plan = planner.build(expanded);

// Phase 2+3: Execute in dependency order
for (auto& need : plan.needs) {
    auto docs = bm25.search(need.keywords, 10);     // retrieval
    auto chunks = Chunker::select_chunks(...);        // chunk selection
    auto answer = synthesizer.synthesize(need, chunks); // synthesis
    validator.validate(answer, need);                  // validation
    composite.parts.push_back(answer);
    conversation.update(need);
}
conversation.save(conv_path);
```

### Verification Checklist

| Spec Requirement | Status |
|---|---|
| QueryAnalyzer produces InformationNeeds (not intents) | ✅ |
| SelfAsk expands needs before planning | ✅ |
| QuestionPlanner orders by dependency (topological sort) | ✅ |
| Retrieval uses BM25 + HNSW with property-aware chunk selection | ✅ |
| AnswerSynthesizer dispatches by Property with typed-chunk-first | ✅ |
| AnswerValidator checks signal words + conflict detection | ✅ |
| ConversationState persists across processes | ✅ |
| Support needs hidden from output (is_support flag) | ✅ |
| Prior context propagated between dependent needs | ✅ |
| BM25-only retry on validation failure | ✅ |
| JSON output with structured answer + sources + validation metadata | ✅ |

### What This Unlocks

✅ Multi-part questions answered in logical order
✅ Implicit follow-ups supported naturally
✅ No dependency on interrogative words
✅ Clear separation of concerns
✅ Future LLM integration becomes trivial (replace any layer independently)

### All Three Phases Complete

- **Phase 1** ✅ — Information Need model (entity + property + form), multi-need decomposition, property-aware synthesis
- **Phase 2** ✅ — Evidence-driven answers with chunk typing, hybrid retrieval, keyword-aware selection
- **Phase 3** ✅ — Question planning, self-ask expansion, conflict detection, validation, conversation memory

### Integration Test Results

All 13 tests pass: `Results: 13 passed, 0 failed, 13 total`

---

## Step 9b: Expanded Test Data and Integration Tests

### New Data Files (5 documents)

| File | Topic | Properties Covered |
|------|-------|--------------------|
| `data/physics/electricity.txt` | Electricity & magnetism | DEFINITION, HISTORY, FUNCTION, USAGE, ADVANTAGES, LIMITATIONS |
| `data/engineering/solar_energy.txt` | Solar energy | DEFINITION, HISTORY, FUNCTION, USAGE, ADVANTAGES, LIMITATIONS |
| `data/geography/japan.txt` | Japan | LOCATION, HISTORY, ADVANTAGES, LIMITATIONS |
| `data/geography/climate_change.txt` | Climate change | DEFINITION, HISTORY, FUNCTION, ADVANTAGES, LIMITATIONS |
| `data/computer_science/python.txt` | Python language | DEFINITION, HISTORY, FUNCTION, USAGE, ADVANTAGES, LIMITATIONS |

Total corpus: 16 documents (was 11).

### Expanded Integration Tests (36 test cases)

| Category | Tests | Topics |
|----------|-------|--------|
| Sweden | 4 | Stockholm location, importance, multi-need |
| Databases | 4 | Definition, NoSQL drawbacks, beginners, SQL advantages |
| Networking | 3 | TCP reliability, explain, timeline |
| Physics | 5 | Electricity: definition, function, history, advantages, limitations |
| Solar Energy | 4 | Definition, function, advantages, limitations |
| Japan | 3 | Location, importance, challenges |
| Python | 4 | Definition, advantages, limitations, beginners |
| Climate Change | 3 | Definition, function, history |
| Multi-need | 3 | TCP def+function, algorithm scalability |
| Validation | 3 | NoSQL, Stockholm, Japan validated |

### Bug Fixes

- Fixed `"city"` substring matching in LOCATION prototype — `"electricity"` was being detected as LOCATION because it contains `"city"`. Changed to `" city"` (with leading space).
- Added `"challenge"` to LIMITATIONS property prototype signals.
- Added `"island nation"` and `"port city"` as safe compound LOCATION signals in chunker.

### Results

```
Results: 36 passed, 0 failed, 36 total
```

---

## Step 9c: Expanded Corpus and Stress Tests

### New Data Files (5 documents, 21 total)

| File | Topic | Properties Covered |
|------|-------|--------------------|
| `data/health/antibiotics.txt` | Antibiotics | DEFINITION, HISTORY (Fleming 1928), FUNCTION (cell wall/protein/DNA), ADVANTAGES, LIMITATIONS (resistance), USAGE |
| `data/computer_science/blockchain.txt` | Blockchain | DEFINITION, HISTORY (Nakamoto 2008), FUNCTION (consensus), ADVANTAGES (decentralization), LIMITATIONS (scalability), USAGE |
| `data/economics/inflation.txt` | Inflation | DEFINITION, HISTORY (Weimar, 1970s), FUNCTION (demand-pull/cost-push), ADVANTAGES (moderate), LIMITATIONS (purchasing power) |
| `data/geography/mars.txt` | Mars | LOCATION (4th planet), HISTORY (Mariner 4 1965), COMPOSITION, ADVANTAGES (exploration), LIMITATIONS (distance/cost) |
| `data/engineering/electric_vehicles.txt` | Electric vehicles | DEFINITION, HISTORY (1832-Tesla), FUNCTION (battery/motor), COMPARISON (vs gasoline), ADVANTAGES (emissions), LIMITATIONS (charging) |

### Expanded Integration Tests (67 test cases)

| Category | Tests | New Tests Added |
|----------|-------|-----------------|
| Antibiotics | 6 | definition, function, history, advantages, limitations, usage |
| Blockchain | 5 | definition, function, advantages, limitations, history |
| Inflation | 4 | definition, function, limitations, history |
| Mars | 5 | location, composition, advantages, limitations, history |
| Electric Vehicles | 6 | definition, function, advantages, limitations, comparison, history |
| Edge Cases | 5 | polite prefix, no interrogative, implicit advantages, implicit definition, short query |
| Previous | 36 | unchanged |

### Bug Fixes

- Fixed `"city"` substring in LOCATION prototype: changed to `" city"` (with space) to prevent "electricity" matching
- Added `"challenge"` and `"problem"` to LIMITATIONS property prototype signals
- Added `"first described"`, `"first practical"`, `"first released"` to HISTORY chunk classifier
- Added `"island nation"`, `"port city"` as safe compound LOCATION signals in chunker
- Increased HISTORY synthesizer entity proximity threshold from 100 to 200 chars

### Results

```
Results: 67 passed, 0 failed, 67 total
```

---

## Step 10: Evidence Agreement & Contradiction Detection (Full Implementation)

### Goal

Replace the simple negation-based conflict detection with a full **NormalizedClaim-based** evidence agreement and contradiction system, producing refined multi-factor confidence scores.

### Architecture

```
Evidence chunks
  ↓
EvidenceNormalizer → NormalizedClaim[]
  (entity, property, keywords, negations, docId)
  ↓
Pairwise analysis:
  ├─ agreement_score() → keyword overlap ratio
  └─ contradicts() → negation conflict OR directional opposites
  ↓
EvidenceAnalysis
  (agreement, agreement_pairs, contradiction_pairs, penalty)
  ↓
compute_refined_confidence()
  = 0.3 * coverage + 0.2 * volume + 0.3 * agreement + 0.2 * (1 - penalty)
```

### NormalizedClaim Model

Each evidence chunk is normalized to a claim with:
- **Entity**: lowercase entity name
- **Property**: chunk type (LOCATION, DEFINITION, etc.)
- **Keywords**: extracted from 4 domain vocabularies (geo, tech, science, economics)
- **Negations**: "not", "no", "never", "without", "lack", "cannot", "unable", "impossible"
- **DocId**: source document

### Agreement Detection

Two claims **agree** if:
- Same entity + same property
- Substantial keyword overlap (Jaccard-like ratio)
- No negation mismatch

### Contradiction Detection

Two claims **contradict** if:
- Same entity + same property + different documents
- Negation conflict (one has negation, other doesn't)
- OR directional opposites detected (east/west, north/south, cheap/expensive, etc.)

14 opposite pairs defined: east/west, eastern/western, north/south, northern/southern, increase/decrease, rising/falling, advantage/disadvantage, benefit/drawback, cheap/expensive, fast/slow, safe/dangerous, reliable/unreliable, efficient/inefficient, simple/complex.

### Refined Confidence Formula

```
confidence = 0.3 * coverage      (keyword match ratio)
           + 0.2 * volume        (min(1.0, evidence_count / 3.0))
           + 0.3 * agreement     (average agreement score)
           + 0.2 * (1 - penalty) (contradiction penalty, max 0.4)
```

### Files Created

| File | Description |
|------|-------------|
| `src/answer/evidence_normalizer.h` | `NormalizedClaim` struct, `EvidenceNormalizer` class, `agreement_score()`, `contradicts()` |
| `src/answer/evidence_normalizer.cpp` | Domain keyword vocabularies, normalization, agreement scoring, directional opposite detection |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/answer/answer_validator.h` | Added `EvidenceAnalysis` struct, `analyze_evidence()`, `compute_refined_confidence()`. Removed old `detect_conflicts()`. |
| `src/answer/answer_validator.cpp` | Rewritten to use `EvidenceNormalizer` for claim-level analysis |
| `src/cli/commands.cpp` | Pipeline uses `analyze_evidence()` + `compute_refined_confidence()` instead of old `detect_conflicts()` |
| `CMakeLists.txt` | Added `evidence_normalizer.cpp` |

### Integration Test Results

All 67 tests pass: `Results: 67 passed, 0 failed, 67 total`

### Example Output

```json
"answer": {
    "text": "Stockholm is situated on the eastern coast of Sweden...",
    "confidence": 0.70,
    "validated": true,
    "note": "10 contradiction(s) detected. 37 agreement(s)."
}
```
