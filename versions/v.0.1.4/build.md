# v.0.1.4 — Build & Usage Guide (WSL Ubuntu)

## 1. Prerequisites

```bash
sudo apt update
sudo apt install -y build-essential cmake git unzip pkg-config
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
cd versions/v.0.1.4
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

### With tests

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
Each combo is `analyzer:retriever:embedding`.

```bash
# Run default matrix (5 combinations × 75 tests = 375 test runs)
bash ../tests/test_config_matrix.sh

# Run specific combinations
bash ../tests/test_config_matrix.sh rule:bm25:auto rule:hybrid:bow

# Run all sensible combinations
bash ../tests/test_config_matrix.sh \
    rule:bm25:auto \
    rule:hybrid:auto \
    rule:hybrid:bow \
    rule:hnsw:auto \
    auto:hybrid:auto
```

Example output:

```
COMBINATION                      PASSED   FAILED    TOTAL
------------------------------ -------- -------- --------
rule:bm25:auto                       74        1       75
rule:hybrid:auto                     75        0       75
rule:hybrid:bow                      75        0       75
rule:hnsw:auto                       30       45       75
auto:hybrid:auto                     75        0       75
------------------------------ -------- -------- --------
TOTAL                               329       46      375
```

Notes:
- `hybrid` configurations pass 75/75 (the system is optimized for hybrid retrieval)
- `bm25` fails 1 compression test (STRONG compression requires hybrid-level agreement)
- `hnsw` fails 45 tests (semantic-only retrieval misses keyword-dependent answers)

---

## 4. Configuration

All behavior is controlled by config files — no rebuild needed.

### Algorithm selection (`config/default.conf`)

```
query.analyzer = auto        # rule | neural | auto
retrieval.retriever = hybrid  # bm25 | hnsw | hybrid
embedding.method = auto       # bow | transformer | auto
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
```

### Switch algorithms (no rebuild)

```bash
# Edit config/default.conf:
#   retrieval.retriever = bm25
# Then run — BM25-only retrieval
./mysearch ask "where is stockholm"

# Edit config/default.conf:
#   retrieval.retriever = hybrid
# Then run — back to hybrid
./mysearch ask "where is stockholm"
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

# Integration tests
bash ../tests/test_qa_integration.sh

# Matrix tests
bash ../tests/test_config_matrix.sh

bash ../tests/test_config_matrix.sh auto:hybrid:auto

# Manual queries
./mysearch ask "where is stockholm"
./mysearch ask "what is a database" --json
./mysearch ask "tell me where stockholm is and why it is important"
./mysearch ask "explain how TCP works" --detailed
```
