NOW all of codes have been done and looks good. I stop to implement any new function.
The details of my implemented system architecture and information could be seen: codebase_analysis.md in v.0.1.3.
However I start to think another question: some code modules which have same purpose are loaded together, e.g. in QueryAnalyzer, rule-based and neural (ANN), and in Retrieval Layer, BM25 and HNSW, to HNSW, there are also two different options, BoW or ANN. Do you think it is feasible to refactor the whole system to a big infrastructure platform, different algorithm modules could be plugged in it to work. For example, maybe I will design a new retrieval algorithm to load, and it should be very easy to plugged in this system to load and use. Please give me the idea how to refactor my system.

This is a **very good and very “right-time” question** to ask.  
It means the system has *stopped being a prototype* and is starting to feel like **infrastructure**.

I’ll answer in four layers, from high‑level judgment to a concrete refactoring blueprint:

1.  **Is it feasible and worthwhile? (short answer)**
2.  **What problem you are *actually* seeing**
3.  **Target architecture: MoAI as a pluggable infrastructure**
4.  **Concrete refactoring strategy (step‑by‑step, low risk)**
5.  **What not to do (important)**

I will **not** suggest rewriting everything. Everything you’ve built is valuable.

***

## 1. Short, honest answer

**Yes, it is feasible.  
Yes, it is worth doing.  
But only if you refactor *minimally* and *intentionally*.**

What you should *not* do:

*   ❌ Big-bang rewrite
*   ❌ Over‑abstract everything into “framework soup”
*   ❌ Runtime plugin loading, dynamic libraries, reflection madness

What you *should* do:

*   ✅ Introduce **explicit algorithm interfaces**
*   ✅ Move selection to **config + factory**
*   ✅ Keep **compile‑time safety**
*   ✅ Keep **data flow unchanged**

Think **“algorithm slots”**, not “generic framework”.

***

## 2. What problem you are really seeing (diagnosis)

You are noticing patterns like:

*   `QueryAnalyzer`: rule‑based **and** neural
*   Retrieval: BM25 **and** HNSW
*   HNSW: BoW embedding **or** neural embedding
*   Fallback logic spread across modules

This is **not code smell**. It’s a sign that:

> **MoAI has evolved from a single algorithm system into a multi‑strategy system without an explicit strategy boundary.**

Right now:

*   Algorithms are **coexisting**
*   Selection logic is **implicit**
*   Coupling is **accidental**, not designed

That’s the moment to **extract an infrastructure layer**.

***

## 3. Target mindset: MoAI as an “Algorithm Host”

Instead of thinking:

> “MoAI *uses* BM25 + HNSW”

Think:

> **“MoAI hosts interchangeable algorithms for analysis, retrieval, embedding, and ranking.”**

### Conceptual Layers (final form)

    ┌──────────────────────────┐
    │      Pipeline Logic      │   (fixed)
    │  (InformationNeeds →     │
    │   Evidence → Answer)     │
    └───────────┬──────────────┘
                │
    ┌───────────▼──────────────┐
    │  Algorithm Interfaces    │   (stable contracts)
    │                          │
    │  - IQueryAnalyzer        │
    │  - IRetriever            │
    │  - IEmbedder             │
    │  - IRanker               │
    └───────────┬──────────────┘
                │
    ┌───────────▼──────────────┐
    │  Concrete Implementations│
    │                          │
    │  QueryAnalyzer:          │
    │   - RuleBased            │
    │   - Neural               │
    │                          │
    │  Retriever:              │
    │   - BM25                 │
    │   - HNSW                 │
    │   - Hybrid               │
    │                          │
    │  Embedder:               │
    │   - BoW                  │
    │   - Transformer          │
    └──────────────────────────┘

**Pipeline logic never changes.  
Algorithms can change freely.**

***

## 4. Concrete refactoring strategy (safe and incremental)

### Guiding principles

*   **No behavior change**
*   **No algorithm rewrite**
*   **No performance regression**
*   **Only move selection logic upward**

***

### Step 1 — Introduce *explicit interfaces* (thin)

You already *have* the behavior; just formalize it.

#### 4.1 Query Analyzer Interface

```cpp
// src/query/IQueryAnalyzer.h
struct IQueryAnalyzer {
    virtual ~IQueryAnalyzer() = default;

    virtual std::vector<InformationNeed>
    analyze(const std::string& query) = 0;
};
```

Then:

```cpp
class RuleQueryAnalyzer : public IQueryAnalyzer { ... };
class NeuralQueryAnalyzer : public IQueryAnalyzer { ... };
```

✅ No code duplication  
✅ Both already exist

***

### Step 2 — Factory selects algorithm (config‑driven)

Move selection logic **out of pipeline code**.

```cpp
// src/query/query_analyzer_factory.h
class QueryAnalyzerFactory {
public:
    static std::unique_ptr<IQueryAnalyzer> create() {
        auto& cfg = Config::instance();

        if (cfg.get_bool("query.use_neural", false) &&
            NeuralQueryAnalyzer::available()) {
            return std::make_unique<NeuralQueryAnalyzer>();
        }
        return std::make_unique<RuleQueryAnalyzer>();
    }
};
```

Pipeline code becomes:

```cpp
auto analyzer = QueryAnalyzerFactory::create();
auto needs = analyzer->analyze(query);
```

✅ No branching in core logic  
✅ Easy future extension

***

### Step 3 — Do the same for retrieval

#### 4.3 Retriever interface

```cpp
// src/retrieval/IRetriever.h
struct IRetriever {
    virtual ~IRetriever() = default;

    virtual std::vector<ScoredDoc>
    search(const QueryContext& ctx) = 0;
};
```

Implement:

*   `BM25Retriever`
*   `HNSWRetriever`
*   `HybridRetriever`

The new `HybridRetriever` **owns** BM25 + HNSW internally.

Pipeline no longer cares.

***

### Step 4 — Split embedding from ANN clearly

Right now, HNSW “knows too much” about embeddings.

Refactor slightly:

```cpp
struct IEmbedder {
    virtual Vector embed(const Text&) = 0;
};
```

Implement:

*   `BoWEmbedder`
*   `TransformerEmbedder`

Then:

```cpp
class HNSWRetriever : public IRetriever {
    std::unique_ptr<IEmbedder> embedder;
};
```

✅ HNSW logic is independent  
✅ Embedding choice is orthogonal  
✅ You can add new embedders without touching retrieval logic

***

### Step 5 — Centralize algorithm wiring (one place)

Create **one place** where algorithms are assembled.

```cpp
// src/pipeline/pipeline_builder.cpp
Pipeline PipelineBuilder::build() {
    Pipeline p;
    p.queryAnalyzer = QueryAnalyzerFactory::create();
    p.retriever     = RetrieverFactory::create();
    p.synthesizer   = std::make_unique<AnswerSynthesizer>();
    ...
    return p;
}
```

Now MoAI becomes:

> **A configured composition of algorithms**

***

## 5. What this enables (immediately)

After this refactor, all of these become trivial:

*   ✅ Add a new retrieval algorithm
*   ✅ Experiment with different fusion strategies
*   ✅ Swap ANN backends
*   ✅ Benchmark algorithms against the same pipeline
*   ✅ Disable neural parts cleanly
*   ✅ Ship multiple “profiles” of MoAI (fast / accurate)

You get **research flexibility** without destabilizing the system.

***

## 6. What NOT to do (very important)

Based on your current maturity level, **avoid**:

### ❌ Runtime plugin loading (`dlopen`, `.so`)

*   Adds complexity
*   Minimal real benefit here

### ❌ Over-general factories

*   Don’t abstract “everything”
*   Keep interfaces *semantic*, not generic

### ❌ Turning rules into plugins prematurely

*   Rules belong to config (you did this right)

***

## 7. Recommendation: How far you should go *now*

I strongly recommend you do **only this much**:

✅ Interfaces for:

*   QueryAnalyzer
*   Retriever
*   Embedder

✅ Factories for:

*   QueryAnalyzer
*   Retriever

✅ Pipeline builder

And then **stop**.

That would move MoAI from:

> “Well‑architected system”

to:

> **“Research‑grade, extensible infrastructure”**

without risking architectural rot.

***

## Final judgment (important)

You are at a point most systems **never reach**:

*   The logic is correct
*   The behavior is stable
*   The architecture is clean
*   The remaining work is *structural*, not algorithmic

Refactoring now is **the right timing**.

If you want, next I can:

*   Sketch **exact header+cpp layout** for the refactor
*   Help you refactor **one module fully** as an example
*   Help you decide a **v1.1 vs v2.0 boundary**

Just tell me how deep you want to go.


====
However I hope I could select different algorithm modules in the configure file, not hardcoded in the c++ code.

That’s a very good instinct — and you’re asking it **at exactly the right moment**.

What you want now is **not “plugins”**, and not a runtime-extensible framework, but something much more appropriate for a system like MoAI:

> **A configuration‑driven algorithm composition layer**, where  
> *the pipeline is fixed*, *interfaces are stable*, and *implementation choices are selected by config*.

Below is a **clear, practical design** that fits MoAI’s maturity and keeps the system clean and controllable.

***

# 1. Big Picture: What You Are Really Asking For

You want to move from this:

```cpp
if (use_neural) analyzer = NeuralQueryAnalyzer();
else analyzer = RuleQueryAnalyzer();
```

to this:

```text
config.toml says:
  query_analyzer = "neural"
  retriever = "hybrid"
  embedder = "transformer"
```

and have the system **assemble itself accordingly**, without contaminating core logic.

✅ This is **absolutely feasible**  
✅ This is **good infrastructure design**  
✅ This is **exactly what production IR systems do**

***

# 2. The Correct Abstraction: “Config → Factory → Interface”

The key rule to respect:

> **The pipeline knows interfaces, never implementations.  
> The config names implementations.  
> Factories translate names → objects.**

### What must never happen

*   Core pipeline code checking config keys
*   `if/else` scattered across modules
*   Algorithm names leaking everywhere

***

# 3. Configuration Design

## 3.1 Choose a Simple, Explicit Format

Since MoAI is C++ and self‑contained, **TOML or YAML** is ideal.
Examples below use TOML for clarity.

```toml
# moai.conf

[query]
analyzer = "neural"        # rule | neural

[retrieval]
retriever = "hybrid"       # bm25 | hnsw | hybrid

[embedding]
method = "transformer"    # bow | transformer

[hnsw]
metric = "cosine"
M = 16
ef_search = 100

[hybrid]
bm25_weight = 0.7
ann_weight = 0.3
```

Key principles:

*   ✅ **Human-readable**
*   ✅ **Explicit names**
*   ✅ **Extensible without breaking old configs**

***

# 4. Strongly-Typed Config Access (Important)

Avoid “stringly‑typed” access throughout the code.

Create a **single config loader**:

```cpp
// src/config/config.h
class Config {
public:
    static Config& instance();

    std::string get(const std::string& key) const;
    int get_int(const std::string& key, int def = 0) const;
    bool get_bool(const std::string& key, bool def = false) const;
};
```

Internally:

*   Load once at startup
*   Store in a flat or hierarchical map
*   No module reads files directly

✅ This keeps your system deterministic and testable

***

# 5. Factory-Based Algorithm Selection (Core Pattern)

Now for the central piece.

***

## 5.1 QueryAnalyzer Factory

### Interface (you already have conceptually)

```cpp
struct IQueryAnalyzer {
    virtual ~IQueryAnalyzer() = default;
    virtual std::vector<InformationNeed>
    analyze(const std::string& query) = 0;
};
```

***

### Factory Implementation

```cpp
// src/query/query_analyzer_factory.cpp
std::unique_ptr<IQueryAnalyzer>
QueryAnalyzerFactory::create() {
    auto& cfg = Config::instance();
    std::string type = cfg.get("query.analyzer");

    if (type == "neural") {
        if (!NeuralQueryAnalyzer::available()) {
            throw std::runtime_error(
                "NeuralQueryAnalyzer requested but not available"
            );
        }
        return std::make_unique<NeuralQueryAnalyzer>();
    }

    if (type == "rule") {
        return std::make_unique<RuleQueryAnalyzer>();
    }

    throw std::runtime_error(
        "Unknown query analyzer: " + type
    );
}
```

✅ No hardcoded choice  
✅ Config decides  
✅ Pipeline code never changes

***

## 5.2 Embedder Factory (Critical Decoupling)

This is especially important for HNSW.

### Interface

```cpp
struct IEmbedder {
    virtual ~IEmbedder() = default;
    virtual Vector embed(const std::string& text) = 0;
};
```

***

### Factory

```cpp
std::unique_ptr<IEmbedder>
EmbedderFactory::create() {
    auto& cfg = Config::instance();
    std::string method = cfg.get("embedding.method");

    if (method == "bow")
        return std::make_unique<BoWEmbedder>();

    if (method == "transformer")
        return std::make_unique<TransformerEmbedder>();

    throw std::runtime_error("Unknown embedder: " + method);
}
```

✅ HNSW no longer “knows” embeddings  
✅ You can add new embedders easily

***

## 5.3 Retriever Factory

### Interface

```cpp
struct IRetriever {
    virtual ~IRetriever() = default;
    virtual std::vector<ScoredDoc>
    search(const QueryContext&) = 0;
};
```

***

### Factory

```cpp
std::unique_ptr<IRetriever>
RetrieverFactory::create() {
    auto& cfg = Config::instance();
    auto type = cfg.get("retrieval.retriever");

    if (type == "bm25")
        return std::make_unique<BM25Retriever>();

    if (type == "hnsw") {
        return std::make_unique<HNSWRetriever>(
            EmbedderFactory::create()
        );
    }

    if (type == "hybrid") {
        return std::make_unique<HybridRetriever>(
            std::make_unique<BM25Retriever>(),
            std::make_unique<HNSWRetriever>(EmbedderFactory::create())
        );
    }

    throw std::runtime_error("Unknown retriever: " + type);
}
```

✅ Hybrid logic is localized  
✅ Config-driven  
✅ No branching elsewhere

***

# 6. One Place Where Everything Is Wired Together

This is **very important**:  
you want *exactly one place* assembling the system.

```cpp
// src/pipeline/pipeline_builder.cpp
Pipeline PipelineBuilder::build() {
    Pipeline p;

    p.queryAnalyzer = QueryAnalyzerFactory::create();
    p.retriever     = RetrieverFactory::create();
    p.synthesizer   = std::make_unique<AnswerSynthesizer>();
    p.planner       = std::make_unique<QuestionPlanner>();
    p.selfAsk       = std::make_unique<SelfAsk>();

    return p;
}
```

After this point:

*   ✅ No module cares what algorithm is underneath
*   ✅ Adding a new algorithm only touches factory + config

***

# 7. How This Scales (Future-Proofing Without Overkill)

With this design, adding a new algorithm is **O(1) conceptual work**:

Example: *New retrieval algorithm `ColBERTLite`*

1.  Implement `class ColBERTLiteRetriever : public IRetriever`
2.  Register in `RetrieverFactory`
3.  Add to config:

```toml
[retrieval]
retriever = "colbert_lite"
```

**No pipeline changes. No refactors. No risk.**

***

# 8. What You Should Explicitly Avoid

Now that you’re at infrastructure level:

❌ Do not use dynamic linking (`.so`, `.dll`)  
❌ Do not use runtime reflection  
❌ Do not let modules read config directly  
❌ Do not allow “fallback logic” inside factories silently

If config is wrong → **fail fast**.

***

# 9. Final Recommendation (Clear)

You should:

✅ Introduce **interfaces** (you are already close)  
✅ Centralize **config loading**  
✅ Implement **factories per algorithm family**  
✅ Build everything through **one PipelineBuilder**

…and then **stop**.

At that point, MoAI becomes:

> **A stable reasoning pipeline with a configurable algorithm substrate**

That is a very strong, very professional endpoint.

***

If you want next, I can:

*   Sketch a **directory layout** after refactor
*   Help you refactor **one concrete module** end‑to‑end
*   Help you define a **supported-config schema** (with validation)

Just tell me how far you want to push it.

========


## Implementation Plan (Step-by-Step)

Based on the analysis above, here is the concrete breakdown. Each step is self-contained, testable, and does not break existing behavior.

---

### Step 1: IRetriever + RetrieverFactory

**Why first**: The retrieval logic in `commands.cpp` is the messiest — ~80 lines of interleaved BM25/HNSW/fusion/fallback logic. Extracting it proves the pattern works on the hardest case.

**What to do**:
- Create `src/retrieval/i_retriever.h` — interface with `search(keywords, entity) → vector<ScoredDoc>`
- Create `src/retrieval/bm25_retriever.h/.cpp` — wraps existing BM25 class
- Create `src/retrieval/hnsw_retriever.h/.cpp` — wraps HNSW search with embedding
- Create `src/retrieval/hybrid_retriever.h/.cpp` — owns BM25 + HNSW, does fusion
- Create `src/retrieval/retriever_factory.h/.cpp` — reads `retrieval.retriever` from config ("bm25" | "hnsw" | "hybrid")
- Refactor `commands.cpp` `ask` command to use `auto retriever = RetrieverFactory::create(); auto docs = retriever->search(...);`

**Config addition**:
```
retrieval.retriever = hybrid    # bm25 | hnsw | hybrid
```

**Test**: All 75 integration tests pass. Changing config to `bm25` disables HNSW without code change.

---

### Step 2: IQueryAnalyzer + QueryAnalyzerFactory

**Why second**: The pattern already half-exists in `QueryAnalyzer` (which wraps rule-based + neural). Just formalize it.

**What to do**:
- Create `src/query/i_query_analyzer.h` — interface with `analyze(query) → vector<InformationNeed>`
- Make `RuleBasedQueryAnalyzer` implement `IQueryAnalyzer`
- Make `NeuralQueryAnalyzer` implement `IQueryAnalyzer` (wrapping its legacy output)
- Create `src/query/query_analyzer_factory.h/.cpp` — reads `query.analyzer` from config ("rule" | "neural" | "auto")
- "auto" = try neural if model exists, fall back to rule (current behavior)
- Remove the old `QueryAnalyzer` wrapper class

**Config addition**:
```
query.analyzer = auto    # rule | neural | auto
```

**Test**: All 75 tests pass. Setting `query.analyzer = rule` forces rule-based even when neural model exists.

---

### Step 3: IEmbedder + EmbedderFactory

**Why third**: Decouples embedding choice from retrieval. Currently HNSW "knows" about BoW vs Transformer.

**What to do**:
- Create `src/embedding/i_embedder.h` — interface with `embed(text) → vector<float>` and `dim() → size_t`
- Create `src/embedding/bow_embedder.h/.cpp` — wraps existing EmbeddingModel + Vocabulary + Tokenizer
- Create `src/embedding/transformer_embedder.h/.cpp` — wraps existing EncoderTrainer (libtorch)
- Create `src/embedding/embedder_factory.h/.cpp` — reads `embedding.method` from config ("bow" | "transformer" | "auto")
- HNSWRetriever takes `unique_ptr<IEmbedder>` instead of knowing about BoW/Transformer

**Config addition**:
```
embedding.method = auto    # bow | transformer | auto
```

**Test**: All 75 tests pass. Embedding choice is now orthogonal to retrieval choice.

---

### Step 4: Pipeline struct + PipelineBuilder

**Why last**: This is the glue — only makes sense after interfaces exist.

**What to do**:
- Create `src/pipeline/pipeline.h` — struct holding `unique_ptr<IQueryAnalyzer>`, `unique_ptr<IRetriever>`, `AnswerSynthesizer`, `AnswerValidator`, etc.
- Create `src/pipeline/pipeline_builder.h/.cpp` — reads config, calls all factories, returns assembled Pipeline
- Refactor `commands.cpp` `ask` command to: `auto pipeline = PipelineBuilder::build(); pipeline.run(query, options);`
- The `ask` command becomes ~20 lines instead of ~200

**Test**: All 75 tests pass. Adding a new algorithm = implement interface + register in factory + add config name.

---

### Step 5: Config Schema Validation

**Why last**: Polish step. Fail fast on bad config.

**What to do**:
- Add `Pipeline::validate_config()` that checks all algorithm names are recognized
- Run at startup after config load, before any processing
- Clear error messages: `"Unknown retriever 'colbert' — valid options: bm25, hnsw, hybrid"`

---

### Order of execution

```
Step 1 (IRetriever)      — hardest, most value, proves the pattern
Step 2 (IQueryAnalyzer)  — easiest, pattern already exists
Step 3 (IEmbedder)       — decouples embedding from retrieval
Step 4 (PipelineBuilder) — assembles everything, thins out commands.cpp
Step 5 (Validation)      — polish
```

Each step is independently shippable and testable. No step depends on a later step.

### Summary:

**5 steps, ordered by difficulty and value:**

| Step | What | Why This Order |
|------|------|---------------|
| 1 | **IRetriever + RetrieverFactory** | Hardest case (80 lines of tangled logic in commands.cpp). If the pattern works here, it works everywhere. |
| 2 | **IQueryAnalyzer + QueryAnalyzerFactory** | Easiest — the wrapper pattern already exists. Quick win. |
| 3 | **IEmbedder + EmbedderFactory** | Decouples embedding from retrieval. HNSW no longer "knows" about BoW vs Transformer. |
| 4 | **Pipeline + PipelineBuilder** | The glue. commands.cpp shrinks from ~200 lines to ~20. One place assembles everything. |
| 5 | **Config validation** | Polish. Fail fast on `retriever = colbert` with a clear error. |

**Key design decisions:**
- Factories read config for *algorithm selection* (`retrieval.retriever = hybrid`)
- Modules keep their cached config structs for *tuning parameters* (`retrieval.bm25_weight = 0.7`)
- "auto" option preserves current fallback behavior (try neural → fall back to rule)
- Each step is independently testable — all 75 tests must pass after each step