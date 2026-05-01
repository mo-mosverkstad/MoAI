# v.0.1.5 — Build & Usage Guide (WSL Ubuntu)

## 1. Prerequisites

```bash
sudo apt update
sudo apt install -y build-essential cmake git python3
```

### Optional: Install libtorch (for neural encoder + neural query analyzer)

```bash
wget https://download.pytorch.org/libtorch/cpu/libtorch-shared-with-deps-2.1.0%2Bcpu.zip
unzip libtorch-shared-with-deps-2.1.0+cpu.zip
mkdir -p ~/opt && mv libtorch ~/opt/libtorch
```

---

## 2. Build

```bash
cd versions/v.0.1.5
mkdir build && cd build
```

### Without libtorch (default)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### With libtorch

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DUSE_TORCH=ON -DCMAKE_PREFIX_PATH=~/opt/libtorch
cmake --build .
```

### With unit tests

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build .
```

---

## 3. Run Tests

### Unit tests (GoogleTest, 76+)

```bash
ctest --output-on-failure
```

### QA integration tests (75 queries)

```bash
./mysearch ingest ../data
./mysearch build-hnsw
bash ../tests/test_qa_integration.sh
```

### Configuration matrix tests

Tests the pluggable algorithm platform across different config combinations.

```bash
# Default matrix (5 combinations × 75 tests = 375 test runs)
bash ../tests/test_config_matrix.sh

# Specific combinations (analyzer:retriever:embedding)
bash ../tests/test_config_matrix.sh rule:bm25:auto rule:hybrid:bow
```

### Performance benchmark

Runs queries multiple times with profiling and produces timing statistics.

```bash
# Default: 15 queries × 3 repeats
bash ../tests/benchmark.sh

# Custom repeat count
bash ../tests/benchmark.sh --repeat 5

# Test specific algorithm combinations
bash ../tests/benchmark.sh --retriever bm25 --repeat 3
bash ../tests/benchmark.sh --retriever hnsw --analyzer rule --repeat 2
bash ../tests/benchmark.sh --config rule:hybrid:bow --repeat 3

# Custom query set
# How to define my_queries.txt could be seen as below
bash ../tests/benchmark.sh --queries my_queries.txt --repeat 10

# Individual flags
bash ../tests/benchmark.sh --retriever bm25 --repeat 3
bash ../tests/benchmark.sh --analyzer rule --embedding bow --repeat 2

# Combo format (analyzer:retriever:embedding)
bash ../tests/benchmark.sh --config rule:hybrid:bow --repeat 3
bash ../tests/benchmark.sh --config rule:bm25:auto --repeat 3
```

The `--config` flag accepts `analyzer:retriever:embedding` combo format.
Individual flags (`--retriever`, `--analyzer`, `--embedding`) also work.
The profiling.jsonl file is cleared at the start of each benchmark run.

**Custom query file format**: One query per line, plain text. Example `my_queries.txt`:

```
where is stockholm
what is a database
how does TCP ensure reliability
what are the advantages of solar energy
history of electric vehicles
```

The default query set is `tests/benchmark_queries.txt` (15 queries covering all property types).

Example output:

```
Component                 p50      p95     mean      max
-------------------- -------- -------- -------- --------
Total                  158.4ms   238.2ms   162.4ms   261.9ms
QueryAnalyzer           34.9ms    77.0ms    37.9ms    82.9ms
Retriever               29.7ms    63.0ms    32.4ms    79.7ms
Chunker                  1.2ms     9.5ms     4.3ms    19.5ms
Synthesizer              0.1ms    34.7ms     7.0ms    97.4ms
SelfAsk+Planning         0.0ms     0.0ms     0.0ms     0.0ms

Queries:    30 runs
Confidence: mean=0.71 min=0.40 max=0.85
Memory:     mean delta=0.30MB max delta=0.50MB
Algorithms: retriever=hybrid, analyzer=rule
```

---

## 4. Configuration

All behavior is controlled by config files — no rebuild needed.

### Algorithm selection (`config/default.conf`)

```
query.analyzer = auto         # rule | neural | auto
retrieval.retriever = hybrid   # bm25 | hnsw | hybrid
embedding.method = auto        # bow | transformer | auto
```

### Profiling (`config/default.conf`)

```
profiling.enabled = false              # true to collect data on every query
profiling.output_file = ../profiling.jsonl   # output path for JSON Lines
```

Alternatively, use the `--profile` CLI flag for per-invocation profiling:

```bash
./mysearch ask "where is stockholm" --profile
# Prints timing summary to stderr:
#   [Profile: Total=118.7ms Retriever=9.2ms QueryAnalyzer=23.0ms Chunker=5.9ms Synthesizer=5.1ms]
# Also writes full JSON record to profiling.output_file
```

### Tuning parameters (`config/default.conf`)

80+ parameters: BM25 k1/b, HNSW M/ef, fusion weights, scope limits, compression thresholds, confidence weights, scoring boosts, proximity thresholds, etc.

### Vocabularies (`config/vocabularies/`)

| File | What to edit |
|------|-------------|
| `properties.conf` | Add/remove signal words per property (chunk/query/validate/synth) |
| `pipeline_rules.conf` | Add self-ask rules, dependencies, preferred chunks, form defaults |
| `domains.conf` | Add evidence domain keywords, negations, opposites |
| `language.conf` | Add stop words, non-entity words, training templates |

---

## 5. Usage

All commands run from the `build/` directory.

### Ingest + index

```bash
./mysearch ingest ../data
./mysearch build-hnsw
```

### Question answering

```bash
./mysearch ask "where is stockholm"
./mysearch ask "what is a database" --json
./mysearch ask "explain how TCP works" --detailed
./mysearch ask "where is stockholm" --brief
./mysearch ask "where is stockholm" --profile    # with profiling
cat ../profiling.jsonl
```

### Switch algorithms (no rebuild)

```bash
# Edit config/default.conf:
#   retrieval.retriever = bm25
./mysearch ask "where is stockholm"    # BM25-only

# Edit config/default.conf:
#   retrieval.retriever = hybrid
./mysearch ask "where is stockholm"    # back to hybrid
```

### Other commands

```bash
./mysearch search "database"                    # BM25 search
./mysearch search 'stockholm AND capital'       # Boolean search
./mysearch hybrid "what is a database"          # Legacy hybrid command
```

---

## 6. Quick Smoke Test

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build .

./mysearch ingest ../data
./mysearch build-hnsw

# Integration tests for all of test cases for one specific combination
bash ../tests/test_qa_integration.sh

# Default matrix (5 combinations × 75 tests = 375 test runs)
bash ../tests/test_config_matrix.sh

# Specific combinations (analyzer:retriever:embedding)
bash ../tests/test_config_matrix.sh rule:bm25:auto rule:hybrid:bow

# Benchmark
bash ../tests/benchmark.sh --repeat 2
cat ../profiling.jsonl

# Individual flags
bash ../tests/benchmark.sh --retriever bm25 --repeat 3
bash ../tests/benchmark.sh --analyzer rule --embedding bow --repeat 2

# Combo format (analyzer:retriever:embedding)
bash ../tests/benchmark.sh --config rule:hybrid:bow --repeat 3
bash ../tests/benchmark.sh --config rule:bm25:auto --repeat 3


# Manual queries
./mysearch ask "where is stockholm"
./mysearch ask "what is a database" --json
./mysearch ask "explain how TCP works" --detailed

# Profile testing
./mysearch ask "where is stockholm" --profile
cat ../profiling.jsonls
```
