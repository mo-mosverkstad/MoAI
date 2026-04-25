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
