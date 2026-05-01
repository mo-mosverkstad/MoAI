This is a **very natural next step** for MoAI v0.1.4, and you are thinking about it in exactly the right way.

At this point MoAI has crossed from “algorithm correctness” into **system engineering and research infrastructure**.  
What you want now is **observability**: to *measure*, *compare*, and *reason about* algorithm choices objectively.

Below is a **clean, extensible design** for **performance data collection** that fits your current architecture and does **not contaminate algorithm logic**.

***

# 1. What “performance data” really means in MoAI

Before design, we must be precise about *what* you want to measure.

In MoAI there are **three distinct performance dimensions**, and they should not be mixed.

***

## 1.1 Runtime (Time complexity in practice)

Measured empirically, not theoretically:

*   QueryAnalyzer latency
*   Retrieval latency (BM25 / HNSW / Hybrid)
*   Chunking + synthesis latency
*   End‑to‑end query latency

Key characteristics:

*   wall‑clock time
*   per‑query and per‑component

***

## 1.2 Memory / space behavior

This is **long‑lived**, not per query:

*   Index size on disk
*   RAM usage of:
    *   inverted index
    *   HNSW graph
    *   neural models
*   Per‑query transient allocations (optional)

***

## 1.3 Algorithmic quality–performance tradeoff

Meaningful comparisons require correlating:

*   time
*   memory
*   answer quality proxies:
    *   confidence
    *   agreement
    *   coverage
    *   fallback rate

This lets you answer questions like:

> “Is neural query analysis worth 3× latency?”  
> “Does hybrid retrieval significantly increase recall per millisecond?”

***

# 2. Core principle: **Instrumentation, not modification**

The **single most important design rule**:

> **Algorithms must not know they are being measured.**

No:

*   `if (profile)`
*   no counters inside algorithms
*   no direct logging calls in core logic

Instead:

*   wrap components
*   intercept execution
*   measure externally

This preserves correctness and keeps benchmarks honest.

***

# 3. High‑level architecture for performance collection

## 3.1 Add a dedicated “Observability” layer

    ┌──────────────────────────────┐
    │       Pipeline Logic         │
    └──────────────┬───────────────┘
                   │
    ┌──────────────▼───────────────┐
    │   Profiling Wrappers         │  ← NEW
    │ (timing / memory / metrics)  │
    └──────────────┬───────────────┘
                   │
    ┌──────────────▼───────────────┐
    │ Algorithm Interfaces         │
    │ (IQueryAnalyzer, IRetriever) │
    └──────────────────────────────┘

Wrappers sit **between** pipeline and algorithms.

***

# 4. Time measurement: precise and safe

## 4.1 Unified profiler API

```cpp
// src/profiling/profiler.h
#pragma once
#include <string>

class Profiler {
public:
    static Profiler& instance();

    void begin(const std::string& component);
    void end(const std::string& component);

    void record(
        const std::string& component,
        double milliseconds
    );

    void flush();  // write results
};
```

***

## 4.2 RAII scope timing (best practice)

```cpp
// src/profiling/scope_timer.h
class ScopeTimer {
public:
    explicit ScopeTimer(const std::string& name)
        : name_(name),
          start_(std::chrono::high_resolution_clock::now()) {}

    ~ScopeTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(
                        end - start_).count();
        Profiler::instance().record(name_, ms);
    }

private:
    std::string name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};
```

Usage (outside algorithm code):

```cpp
{
    ScopeTimer t("QueryAnalyzer.neural");
    needs = analyzer->analyze(query);
}
```

✅ Zero intrusion  
✅ Exception‑safe  
✅ Minimal overhead

***

## 4.3 What to measure (recommended)

| Component         | Metric  |
| ----------------- | ------- |
| QueryAnalyzer     | latency |
| SelfAsk + Planner | latency |
| Retriever (BM25)  | latency |
| Retriever (HNSW)  | latency |
| Hybrid fusion     | latency |
| Chunker           | latency |
| Answer synthesis  | latency |
| End‑to‑End        | latency |

***

# 5. Memory / space measurement

## 5.1 Static memory (index size)

This is easy and deterministic:

*   Size of segment files
*   Size of HNSW graph files
*   Size of neural model files

Record once at load time:

```cpp
Profiler::instance().record_size("index.bm25.disk", bytes);
Profiler::instance().record_size("index.hnsw.disk", bytes);
```

***

## 5.2 Runtime memory (RAM)

There are two reasonable levels:

### Level A: coarse but portable (recommended)

*   Measure peak RSS per process
*   Attribute to algorithm configuration

On Linux/WSL:

*   `/proc/self/statm`
*   `getrusage(RUSAGE_SELF)`

Record:

*   before pipeline run
*   after pipeline run

This will already allow meaningful comparisons.

***

### Level B: fine‑grained allocations (optional)

If you want later:

*   override global `new/delete`
*   tag allocations by module

⚠️ This adds complexity; **do not do it initially**.

***

# 6. Algorithm identity and comparison keys

To compare algorithms, measurements must be labeled.

## 6.1 AlgorithmDescriptor (important)

```cpp
struct AlgorithmDescriptor {
    std::string query_analyzer;  // rule | neural
    std::string retriever;       // bm25 | hnsw | hybrid
    std::string embedder;        // bow | transformer
};
```

Attach this descriptor to every profiling run.

Store it:

*   once per run
*   once per query

***

# 7. Performance record format (machine‑readable)

Use **JSON Lines** (perfect for later analysis).

Example:

```json
{
  "query": "where is stockholm",
  "algorithms": {
    "query_analyzer": "rule",
    "retriever": "hybrid",
    "embedder": "transformer"
  },
  "timing_ms": {
    "QueryAnalyzer": 1.2,
    "Retriever.BM25": 3.4,
    "Retriever.HNSW": 5.8,
    "Chunker": 0.9,
    "Answer": 1.1,
    "Total": 13.6
  },
  "memory_mb": {
    "rss_before": 182.4,
    "rss_after": 183.1
  },
  "quality": {
    "confidence": 0.91,
    "agreement": 0.88,
    "fallback_used": false
  }
}
```

This allows:

*   offline analysis
*   plotting
*   regression tracking
*   algorithm comparison

***

# 8. How to compare algorithms correctly

## 8.1 Controlled experiments

Never compare algorithms unless:

*   same query set
*   same corpus
*   warm caches
*   multiple runs (median, not mean)

Provide a CLI like:

```bash
moai benchmark \
  --queries queries.txt \
  --config config_neural.toml \
  --repeat 5
```

***

## 8.2 Derived metrics that matter

Examples:

*   **ms / query**
*   **memory MB / query**
*   **confidence / ms**
*   **agreement / ms**
*   **fallback rate**
*   **p95 latency**

These are far more meaningful than raw complexity.

***

# 9. Where to integrate in your current system

Minimal, safe integration points:

1.  Wrap `Pipeline::runQuery()`
2.  Wrap each interface implementation call
3.  Record once per query
4.  Flush at end

You **do not touch**:

*   QueryAnalyzer logic
*   Retrieval logic
*   Answer logic

***

# 10. What not to do (very important)

❌ Do NOT add `#ifdef PROFILE` inside algorithms  
❌ Do NOT mix logging with profiling  
❌ Do NOT compute asymptotic “Big‑O” at runtime  
❌ Do NOT measure only total time — you already know where the time goes

***

# 11. Strategic recommendation

You should implement performance collection in **three increments**:

### Phase A — Timing only

*   Components + total
*   JSON output

### Phase B — Add memory (RSS)

*   coarse measurements

### Phase C — Quality‑performance analysis

*   confidence, agreement, fallback correlation

After Phase B you already get **huge value**.

***

# Final assessment

You are **absolutely right** to think about this now.

At v0.1.4, MoAI has become:

*   algorithmically correct ✅
*   architecturally clean ✅
*   behaviorally stable ✅

The next phase is:

> **becoming measurable, comparable, and research‑grade**

If you want, next I can:

*   design the `Profiler` class fully
*   design a benchmark runner CLI
*   propose visualization scripts
*   help you define “winning algorithm profiles”

Just tell me how deep you want to go.


---
## Key design decisions:

1. Non-intrusive : Use RAII ScopeTimer wrappers in Pipeline::run() only — algorithms never know they're being measured
2. Per-query granularity : Each query produces a complete timing profile (not just totals)
3. Opt-in via config : profiling.enabled = true in default.conf — zero overhead when disabled
4. JSON Lines output : One JSON object per query, written to a file for offline analysis
5. Three phases, each independently useful :
   * Phase A: Component timing (immediate value)
   * Phase B: Memory RSS (before/after pipeline run)
   * Phase C: Quality metrics correlation (confidence, agreement, fallback rate alongside timing)

## Implementation Strategy

Based on the design above, here is the concrete step-by-step plan.

### Guiding Principles

- **Zero intrusion**: Algorithms never know they're being measured. All timing happens in `Pipeline::run()` via RAII wrappers.
- **Opt-in**: Profiling is disabled by default. Enable via `profiling.enabled = true` in config. Zero overhead when off.
- **Per-query**: Each query produces a complete profile record. Not just totals.
- **Machine-readable**: JSON Lines output (one JSON object per line per query) for offline analysis.

---

### Step 1: Profiler + ScopeTimer

Create the profiling infrastructure:

- `src/profiling/profiler.h/.cpp` — Profiler singleton that accumulates per-query timing records
- `src/profiling/scope_timer.h` — RAII timer that records elapsed time on destruction
- Config: `profiling.enabled = false` (default off)

```cpp
// Usage in Pipeline::run():
{
    ScopeTimer t("Retriever");
    ranked_docs = retriever_->search(keywords);
}
// Profiler automatically records "Retriever: 3.4ms"
```

---

### Step 2: Instrument Pipeline::run()

Add ScopeTimers at 7 measurement points in `Pipeline::run()`:

| Timer Name | What It Measures |
|-----------|-----------------|
| `QueryAnalyzer` | analyzer_->analyze(query) |
| `SelfAsk+Planning` | expand + topological sort |
| `Retriever` | retriever_->search(keywords) — per need |
| `Chunker` | chunk_document + select_chunks — per need |
| `Synthesizer` | synthesizer_.synthesize() — per need |
| `Validator` | analyze_evidence + validate — per need |
| `Total` | entire Pipeline::run() |

No changes to any algorithm code. Only `pipeline.cpp` is modified.

---

### Step 3: JSON Lines Output

After each query, write a profile record to a file:

- Output file: `profiling.output_file = ../profiling.jsonl` (configurable)
- Also support `--profile` CLI flag to enable without editing config
- Format: one JSON object per line

```json
{"query":"where is stockholm","algorithms":{"analyzer":"rule","retriever":"hybrid","embedder":"bow"},"timing_ms":{"QueryAnalyzer":1.2,"Retriever":4.1,"Chunker":0.8,"Synthesizer":1.0,"Validator":0.5,"Total":8.9},"needs_count":1}
```

---

### Step 4: Memory RSS

Add before/after RSS measurement around Pipeline::run():

- Read `/proc/self/statm` (Linux/WSL) for resident set size
- Record `rss_before_mb` and `rss_after_mb` in the profile record
- Portable fallback: skip memory measurement on non-Linux

---

### Step 5: Quality Metrics in Profile

Add quality signals to each profile record:

```json
{"quality":{"confidence":0.88,"agreement":0.75,"compression":"STRONG","validated":true,"fallback_used":false}}
```

This enables correlation analysis: "Does higher retrieval latency correlate with higher confidence?"

---

### Step 6: Benchmark Runner Script

`tests/benchmark.sh` — runs a set of queries N times and produces statistics:

```bash
bash ../tests/benchmark.sh --queries ../tests/benchmark_queries.txt --repeat 5
```

Output:
```
Component        p50     p95     mean
QueryAnalyzer    1.1ms   2.3ms   1.4ms
Retriever        3.2ms   5.1ms   3.8ms
Chunker          0.7ms   1.2ms   0.8ms
Total            7.4ms  12.1ms   8.5ms
```

---

### Execution Order

```
Step 1 (Profiler + ScopeTimer)     — infrastructure
Step 2 (Instrument Pipeline)       — measurement points
Step 3 (JSON Lines output)         — data collection
Step 4 (Memory RSS)                — space measurement
Step 5 (Quality metrics)           — correlation data
Step 6 (Benchmark script)          — analysis tooling
```

Each step is independently testable. After Step 3, you already have full timing data for every query.

## Key design choices:

* Profiling is opt-in (profiling.enabled = false by default) — zero overhead in production
* All measurement happens via RAII ScopeTimers in Pipeline::run() — algorithms are never modified
* Output is JSON Lines — one record per query, easy to analyze with python/jq
* After Step 3 you already have actionable data; Steps 4-6 are progressive enrichment