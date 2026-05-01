# v.0.1.6 — Build & Usage Guide (WSL Ubuntu)

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
cd versions/v.0.1.6
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
./moai ingest ../data
./moai build-hnsw
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
bash ../tests/benchmark.sh --queries my_queries.txt --repeat 10

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

### (optional, requires libtorch): Train neural encoder
Improves retrieval (the hybrid search step):
* Replaces the random BoW embedding model with a trained Transformer sentence encoder
* The HNSW vector index will use semantically meaningful embeddings instead of bag-of-words vectors
* Documents that are semantically similar to the query (even without exact keyword overlap) will rank higher
* Impact: better document ranking, especially for paraphrased or implicit queries like "Is Stockholm close to the sea?"

```bash
./moai train-encoder --epochs 10 --dim 128
./moai hybrid "what is a database"
```

### (optional, requires libtorch): Train neural query analyzer
Improves query analysis (the InformationNeed extraction step):
* Replaces the rule-based property/entity detection with a neural multi-task classifier
* Better entity extraction via BIO tagging (learned, not longest-keyword heuristic)
* Better property detection for unusual phrasings
* However: the neural analyzer currently produces a single InformationNeed per query — it doesn't support multi-need decomposition. So multi-clause queries like "tell me where stockholm is and why it is important" would lose the clause-splitting capability and produce only one need.

```bash
./moai train-qa --epochs 30
./moai ask "when was the transistor invented"
# stderr: "Using neural query analyzer"
```

Note: The neural analyzer currently produces a single InformationNeed per query
(mapped from its legacy QueryAnalysis output). Multi-need decomposition is only
available via the rule-based analyzer in this version.

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
./moai ask "where is stockholm" --profile
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
./moai ingest ../data
./moai build-hnsw
```

### Question answering

```bash
./moai ask "where is stockholm"
./moai ask "what is a database" --json
./moai ask "explain how TCP works" --detailed
./moai ask "where is stockholm" --brief
./moai ask "where is stockholm" --profile    # with profiling
cat ../profiling.jsonl
```

### Switch algorithms (no rebuild)

```bash
# Edit config/default.conf:
#   retrieval.retriever = bm25
./moai ask "where is stockholm"    # BM25-only

# Edit config/default.conf:
#   retrieval.retriever = hybrid
./moai ask "where is stockholm"    # back to hybrid
```

### Other commands

```bash
./moai search "database"                    # BM25 search
./moai search 'stockholm AND capital'       # Boolean search
./moai hybrid "what is a database"          # Legacy hybrid command
```

---

## 6. Quick Smoke Test

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release && cmake --build .

./moai ingest ../data
./moai build-hnsw

# Train neural encoder (requires libtorch)
./moai train-encoder --epochs 10 --dim 128

# Train neural query analyzer (requires libtorch)
./moai train-qa --epochs 30

# Integration tests
bash ../tests/test_qa_integration.sh

# Default matrix (5 combinations × 75 tests = 375 test runs)
bash ../tests/test_config_matrix.sh

# Specific combinations (analyzer:retriever:embedding)
bash ../tests/test_config_matrix.sh rule:bm25:auto rule:hybrid:bow

# Benchmark
bash ../tests/benchmark.sh --repeat 2
cat ../profiling.jsonl

# Combo format (analyzer:retriever:embedding)
bash ../tests/benchmark.sh --config rule:hybrid:bow --repeat 3
bash ../tests/benchmark.sh --config rule:bm25:auto --repeat 3

# Manual queries
./moai ask "where is stockholm"
./moai ask "what is a database" --json
./moai ask "explain how TCP works" --detailed

# Profile testing
./moai ask "where is stockholm" --profile
cat ../profiling.jsonl
```
