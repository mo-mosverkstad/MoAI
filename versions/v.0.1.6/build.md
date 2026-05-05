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

### Witout unit tests

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DUSE_TORCH=ON -DCMAKE_PREFIX_PATH=~/opt/libtorch \
         -DBUILD_TESTS=OFF
cmake --build .
```

### In Red Hat 8.10

VDI environments typically have an older system GCC and a corporate proxy that blocks GitHub access.

The VDI version is Red Hat 8.10.

```bash
$ cat /etc/os-release
NAME="Red Hat Enterprise Linux"
VERSION="8.10 (Ootpa)"
ID="rhel"
ID_LIKE="fedora"
VERSION_ID="8.10"
PLATFORM_ID="platform:el8"
PRETTY_NAME="Red Hat Enterprise Linux 8.10 (Ootpa)"
ANSI_COLOR="0;31"
CPE_NAME="cpe:/o:redhat:enterprise_linux:8::baseos"
HOME_URL="https://www.redhat.com/"
DOCUMENTATION_URL="https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/8"
BUG_REPORT_URL="https://issues.redhat.com/"

REDHAT_BUGZILLA_PRODUCT="Red Hat Enterprise Linux 8"
REDHAT_BUGZILLA_PRODUCT_VERSION=8.10
REDHAT_SUPPORT_PRODUCT="Red Hat Enterprise Linux"
REDHAT_SUPPORT_PRODUCT_VERSION="8.10"
```

**Problem 1: GoogleTest download fails (HTTP 403 from proxy)**

The `FetchContent` mechanism downloads GoogleTest during cmake configuration. Corporate proxies block this. Solution: disable tests with `-DBUILD_TESTS=OFF`.

**Problem 2: `GLIBCXX_3.4.26 not found` at runtime**

The binary is compiled with a newer GCC but at runtime finds the old system libstdc++. Solution: set `LD_LIBRARY_PATH` to the GCC module's library directory.

**Full build steps:**

```bash
# Load a GCC >= 9 (required for C++20 and compatible libstdc++)
module load gcc/10.3.0

# Clean build (important after switching compilers)
rm -rf build && mkdir build && cd build

# Configure — disable tests (proxy blocks GitHub), explicitly set compiler
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DUSE_TORCH=ON -DCMAKE_PREFIX_PATH=~/opt/libtorch \
         -DBUILD_TESTS=OFF \
         -DCMAKE_C_COMPILER=$(which gcc) \
         -DCMAKE_CXX_COMPILER=$(which g++)

# Build
cmake --build .

# Set runtime library path (required before running moai)
export LD_LIBRARY_PATH=$(dirname $(gcc -print-file-name=libstdc++.so)):$LD_LIBRARY_PATH

# Verify
./moai ingest ../data
./moai build-hnsw
./moai ask "where is stockholm"
```

**Tip:** Add the `export LD_LIBRARY_PATH=...` line to your `~/.bashrc` so you don't need to set it every session.

**Without libtorch:**

```bash
module load gcc/10.3.0
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build .
export LD_LIBRARY_PATH=$(dirname $(gcc -print-file-name=libstdc++.so)):$LD_LIBRARY_PATH
./moai ingest ../data
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

Training data: auto-generated from ingested documents by splitting them into overlapping chunks (short "query" chunk + longer "document" chunk). Uses InfoNCE contrastive loss.

Progress bar shows batch-level progress:
```
Epoch 3/10 [=========>                    ] 142/500 loss=0.4321
```

Output files (in `embeddings/`):
| File | Description |
|------|-------------|
| `encoder.pt` | Trained Transformer model |
| `vocab.txt` | Vocabulary (created if not present) |

### (optional, requires libtorch): Train neural query analyzer
Improves query analysis (the InformationNeed extraction step):
* Replaces the rule-based property/entity detection with a neural multi-task classifier
* Better entity extraction via BIO tagging (learned, not longest-keyword heuristic)
* Better property detection for unusual phrasings
* However: the neural analyzer currently produces a single InformationNeed per query — it doesn't support multi-need decomposition. So multi-clause queries like "tell me where stockholm is and why it is important" would lose the clause-splitting capability and produce only one need.

```bash
./moai train-qa                          # default: 10 epochs
./moai train-qa --epochs 30              # more epochs if needed

./moai ask "when was the transistor invented"
# stderr: "Using neural query analyzer"
```

Training data: auto-generated from ingested documents — entities are extracted from documents and combined with templates from `config/vocabularies/language.conf` `[TEMPLATES]` section. No manual labeling needed. With 201 documents, generates ~880,000 training samples. (~30 hours).

Three simultaneous classification tasks:
| Task | Classes | Purpose |
|------|---------|--------|
| Intent | FACTUAL, EXPLANATION, PROCEDURAL, COMPARISON, GENERAL | What kind of answer the user wants |
| Answer Type | LOCATION, DEFINITION, PERSON_PROFILE, TEMPORAL, PROCEDURE, COMPARISON, SUMMARY | What type of information to extract |
| Entity (BIO) | B (begin), I (inside), O (outside) | Which words in the query are the entity |

Progress bar shows batch-level progress:
```
Epoch 2/30 [====================>         ] 75196/110324 loss=0.0061
```

Output file: `embeddings/qa_model.pt` (written only after all epochs complete).

Note: The neural analyzer currently produces a single InformationNeed per query
(mapped from its legacy QueryAnalysis output). Multi-need decomposition is only
available via the rule-based analyzer in this version.

### Interrupting training

Training can be safely interrupted with Ctrl+C at any time:
* `train-encoder`: model saved only after all epochs — interruption leaves no partial files
* `train-qa`: model saved only after all epochs — interruption leaves no partial files
* No temp files are created during training
* The `embeddings/` directory will only contain files from previously completed runs

### Files in `embeddings/`

| File | Created by | Purpose |
|------|-----------|--------|
| `model.bin` | `moai build-hnsw` | BoW feedforward embedding model |
| `vocab.txt` | `moai build-hnsw` or `train-encoder` | Term-to-ID vocabulary |
| `encoder.pt` | `moai train-encoder` | Trained Transformer encoder (optional) |
| `qa_model.pt` | `moai train-qa` | Trained query analyzer model (optional) |

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
cmake .. -DCMAKE_BUILD_TYPE=Release -DUSE_TORCH=ON -DCMAKE_PREFIX_PATH=~/opt/libtorch
cmake --build .

./moai ingest ../data
./moai build-hnsw

# Train neural encoder (requires libtorch)
./moai train-encoder --epochs 10 --dim 128

# Train neural query analyzer (requires libtorch)
./moai train-qa
./moai train-qa --epochs 30

# default.conf
  query.analyzer = auto         # rule | neural | auto
  retrieval.retriever = hybrid   # bm25 | hnsw | hybrid
  embedding.method = auto        # bow | transformer | auto

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
