# v.0.1.5 Change History

For build, test, and usage instructions, see [build.md](build.md).

---

## Step 1: Performance Profiling Infrastructure

### Goal

Add non-intrusive per-query performance data collection. Algorithms never know they're being measured. Opt-in via config.

### What Was Built

- `Profiler` singleton — accumulates per-query timing records, writes JSON Lines output
- `ScopeTimer` RAII class — records elapsed time on destruction, zero overhead when profiling disabled
- 5 measurement points in `Pipeline::run()`: QueryAnalyzer, SelfAsk+Planning, Retriever, Chunker, Synthesizer + Total
- JSON Lines output file with algorithm identities and per-component timing

### Config

```
profiling.enabled = false              # true to enable
profiling.output_file = ../profiling.jsonl   # output path
```

### Output Format (JSON Lines)

Each query produces one line:

```json
{"query":"what is stockholm","algorithms":{"analyzer":"rule","retriever":"hybrid"},"timing_ms":{"QueryAnalyzer":25.65,"Retriever":11.46,"Chunker":5.98,"Synthesizer":6.20,"SelfAsk+Planning":0.01,"Total":75.06},"needs_count":1}
```

### Example Timing (21 documents, BoW embeddings)

| Component | stockholm | japan |
|-----------|-----------|-------|
| QueryAnalyzer | 25.6ms | 24.3ms |
| Retriever | 11.5ms | 12.1ms |
| Chunker | 6.0ms | 19.7ms |
| Synthesizer | 6.2ms | 7.5ms |
| Total | 75.1ms | 86.2ms |

QueryAnalyzer dominates (~33%) because it loads vocabularies on first call. Retriever includes HNSW index build (on-the-fly for each process invocation).

### Files Created

| File | Description |
|------|-------------|
| `src/profiling/profiler.h` | Profiler singleton: begin_query, record, end_query, JSON Lines output |
| `src/profiling/profiler.cpp` | Implementation with JSON serialization |
| `src/profiling/scope_timer.h` | RAII ScopeTimer: records elapsed ms on destruction |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/pipeline/pipeline.cpp` | Added 5 ScopeTimers + total timing + profiler begin/end calls |
| `src/cli/main.cpp` | Initialize Profiler from config at startup |
| `config/default.conf` | Added `profiling.enabled`, `profiling.output_file` |
| `CMakeLists.txt` | Added profiler.cpp |

### Design Principles

- **Zero intrusion**: No algorithm code modified. All timing in Pipeline::run() only.
- **Zero overhead when disabled**: ScopeTimer checks `enabled_` flag in constructor, skips timing if false.
- **Per-query granularity**: Each query gets its own complete profile record.
- **Machine-readable**: JSON Lines format for offline analysis with python/jq.

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`

Profiling tested: enabled → 2 queries → correct JSON Lines output → disabled → no output file created.


---

## Step 2: Memory RSS Measurement

### Goal

Add before/after RSS (Resident Set Size) measurement to each profiled query, enabling memory usage analysis per algorithm configuration.

### Implementation

- Read `/proc/self/statm` (Linux/WSL) for RSS in pages, convert to MB
- Call `record_rss_before()` at start of Pipeline::run()
- Call `record_rss_after()` at end, before end_query()
- Portable: returns 0.0 on non-Linux platforms (graceful degradation)

### Output Format

```json
{"query":"what is stockholm","algorithms":{...},"timing_ms":{...},"needs_count":1,"memory_mb":{"rss_before":5.38,"rss_after":5.75}}
```

The delta (rss_after - rss_before) shows per-query memory allocation. Useful for comparing algorithm memory footprints across configurations.

### Files Modified

| File | What Changed |
|------|-------------|
| `src/profiling/profiler.h` | Added rss_before_mb/rss_after_mb to ProfileRecord; added record_rss_before/after methods |
| `src/profiling/profiler.cpp` | Added get_rss_mb() reading /proc/self/statm; RSS fields in JSON output |
| `src/pipeline/pipeline.cpp` | Added record_rss_before/after calls |

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`
