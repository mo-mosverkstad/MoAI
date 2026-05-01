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


---

## Step 3: Quality Metrics in Profile

### Goal

Add answer quality signals to each profile record, enabling correlation analysis between performance and quality (e.g., "Does higher retrieval latency correlate with higher confidence?").

### Quality Metrics Recorded

| Metric | Description |
|--------|-------------|
| `confidence` | Average confidence across all needs |
| `agreement` | Overall confidence (proxy for evidence agreement) |
| `validated` | Count of validated needs / total needs |
| `fallback_used` | Whether fallback retry was triggered |
| `compression` | Compression level applied (NONE/LIGHT/STRONG) |

### Output Format

```json
{"query":"what are the drawbacks of NoSQL","algorithms":{...},"timing_ms":{...},"needs_count":2,"memory_mb":{"rss_before":5.38,"rss_after":5.50},"quality":{"confidence":0.70,"agreement":0.70,"validated":2/2,"fallback_used":false,"compression":"STRONG"}}
```

### Files Modified

| File | What Changed |
|------|-------------|
| `src/profiling/profiler.h` | Added QualityMetrics struct; added record_quality method |
| `src/profiling/profiler.cpp` | Implemented record_quality; added quality fields to JSON output |
| `src/pipeline/pipeline.cpp` | Compute and record quality metrics after processing loop |

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`


---

## Step 4: Benchmark Runner Script

### Goal

Automated benchmark runner that runs a set of queries N times and produces summary statistics (p50, p95, mean, max) per component.

### Usage

```bash
# Default: 15 queries × 3 repeats = 45 runs
bash ../tests/benchmark.sh

# Custom
bash ../tests/benchmark.sh --queries ../tests/benchmark_queries.txt --repeat 5
bash ../tests/benchmark.sh --retriever bm25 --repeat 3
bash ../tests/benchmark.sh --analyzer rule --embedding bow --repeat 2
bash ../tests/benchmark.sh --config rule:hybrid:bow --repeat 3
```

The `--config` flag accepts `analyzer:retriever:embedding` combo format.
The profiling.jsonl file is cleared at the start of each benchmark run.

### Example Output

```
Component                 p50      p95     mean      max
-------------------- -------- -------- -------- --------
Total                  158.4ms   238.2ms   162.4ms   261.9ms
Chunker                  1.2ms     9.5ms     4.3ms    19.5ms
QueryAnalyzer           34.9ms    77.0ms    37.9ms    82.9ms
Retriever               29.7ms    63.0ms    32.4ms    79.7ms
SelfAsk+Planning         0.0ms     0.0ms     0.0ms     0.0ms
Synthesizer              0.1ms    34.7ms     7.0ms    97.4ms

Queries:    30 runs
Confidence: mean=0.71 min=0.40 max=0.85
Memory:     mean delta=0.30MB max delta=0.50MB
Algorithms: retriever=hybrid, analyzer=rule
```

### Implementation Details

- Uses `--profile` CLI flag (added to `ask` command) to enable profiling per-invocation
- Prints brief timing summary to stderr: `[Profile: Total=118.7ms Retriever=9.2ms QueryAnalyzer=23.0ms]`
- Writes full JSON record to `profiling.output_file` for offline analysis
- No config file modification needed (avoids OneDrive filesystem caching issues)
- Python analysis script reads JSON Lines and computes statistics
- Supports `--config` flag to test specific retriever configurations

### Files Created

| File | Description |
|------|-------------|
| `tests/benchmark.sh` | Benchmark runner with statistics |
| `tests/benchmark_queries.txt` | 15 representative benchmark queries |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/cli/commands.cpp` | Added `--profile` CLI flag to `ask` command |
| `src/profiling/profiler.h` | Added print_summary declaration |
| `src/profiling/profiler.cpp` | Fixed JSON output; added print_summary to stderr |

### Integration Test Results

All 75 tests pass: `Results: 75 passed, 0 failed, 75 total`

### Summary
* tests/benchmark.sh — runs queries N times with --profile flag, produces p50/p95/mean/max statistics
* tests/benchmark_queries.txt — 15 representative queries covering all property types
* --profile CLI flag added to ask command — enables profiling per-invocation without config file changes
* Fixed JSON output format (was producing invalid 1/1, now uses separate validated_count/total_count fields)

## The profiling system is now complete:
* Step 1: Profiler + ScopeTimer infrastructure
* Step 2: Memory RSS measurement
* Step 3: Quality metrics (confidence, agreement, compression, fallback)
* Step 4: Benchmark runner with statistics

# Results:
```
bash ../tests/benchmark.sh --config rule:hybrid:bow --repeat 3

MoAI Benchmark
  Queries: ../tests/benchmark_queries.txt (15 queries)
  Repeat:  3 times
  Total:   45 runs
  Retriever: hybrid
  Analyzer:  rule
  Embedding: bow

Running............................................. done

Component                 p50      p95     mean      max
-------------------- -------- -------- -------- --------
Total                   99.7ms   171.2ms   105.4ms   187.9ms
Chunker                  1.1ms     7.7ms     3.1ms     8.7ms
QueryAnalyzer           27.0ms    38.1ms    27.3ms    41.5ms
Retriever               21.6ms    38.0ms    21.7ms    50.7ms
SelfAsk+Planning         0.0ms     0.0ms     0.0ms     0.0ms
Synthesizer              0.1ms     7.7ms     2.4ms     8.5ms

Queries:    45 runs
Confidence: mean=0.71 min=0.40 max=0.85
Memory:     mean delta=0.30MB max delta=0.50MB
Algorithms: retriever=hybrid, analyzer=rule


bash ../tests/benchmark.sh --config rule:bm25:auto --repeat 3

MoAI Benchmark
  Queries: ../tests/benchmark_queries.txt (15 queries)
  Repeat:  3 times
  Total:   45 runs
  Retriever: bm25
  Analyzer:  rule
  Embedding: auto

Running............................................. done

Component                 p50      p95     mean      max
-------------------- -------- -------- -------- --------
Total                  101.5ms   184.2ms   106.0ms   238.4ms
Chunker                  1.1ms     7.8ms     3.1ms    10.4ms
QueryAnalyzer           25.6ms    36.7ms    26.3ms    45.9ms
Retriever               23.2ms    38.3ms    21.6ms    43.2ms
SelfAsk+Planning         0.0ms     0.0ms     0.0ms     0.0ms
Synthesizer              0.1ms     8.6ms     2.6ms    14.4ms

Queries:    45 runs
Confidence: mean=0.69 min=0.37 max=0.85
Memory:     mean delta=0.33MB max delta=0.50MB
Algorithms: retriever=bm25, analyzer=rule
```
