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
