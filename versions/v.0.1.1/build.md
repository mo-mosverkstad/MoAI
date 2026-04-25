# v.0.1.1 — Build & Usage Guide (WSL Ubuntu)

## 1. Prerequisites

```bash
sudo apt update
sudo apt install -y build-essential cmake git unzip pkg-config
```

### Optional: Install libtorch (for neural encoder + neural query analyzer)

```bash
wget https://download.pytorch.org/libtorch/cpu/libtorch-shared-with-deps-2.1.0%2Bcpu.zip
unzip libtorch-shared-with-deps-2.1.0+cpu.zip
mkdir -p ~/opt
mv libtorch ~/opt/libtorch
```

---

## 2. Build

Navigate to the v.0.1.1 directory first:

```bash
cd versions/v.0.1.1
mkdir build && cd build
```

### Without libtorch (default, always works)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build .
```

### With libtorch (neural encoder + neural query analyzer)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON \
         -DUSE_TORCH=ON -DCMAKE_PREFIX_PATH=~/opt/libtorch
cmake --build .
```

### Release build (no tests, optimized)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build .
```

---

## 3. Run Tests

```bash
# All tests
ctest --output-on-failure

# Or run the test binary directly
./mysearch_tests

# Specific test suites
./mysearch_tests --gtest_filter="BM25Test.*"
./mysearch_tests --gtest_filter="HNSWTest.*"
./mysearch_tests --gtest_filter="SearchEngineTest.ANDQuery"

# List all available tests
./mysearch_tests --gtest_list_tests
```

---

## 4. Usage

All commands below assume you are inside the `build/` directory.

### Step 1: Ingest documents

```bash
./mysearch ingest ../data
# Ingested ../data -> ../segments/seg_000001
```

### Step 2: BM25 search (unchanged from v.0.1.0)

```bash
./mysearch search "database"
./mysearch search 'stockholm AND capital'
./mysearch search '"machine learning"' --json
```

### Step 3: Hybrid search (unchanged from v.0.1.0)

```bash
./mysearch build-hnsw
./mysearch hybrid "what is a database"
```

> **Important:** If you modify any text files in `data/`, you must re-ingest
> before the changes take effect. `build-hnsw` only rebuilds the HNSW index
> from the existing segment — it does not re-read the source text files.
>
> ```bash
> ./mysearch ingest ../data    # re-read text files into segment
> ./mysearch build-hnsw        # rebuild HNSW from new segment
> ```

### Step 4: Question-answering (NEW — InformationNeed pipeline)

This is the main change in v.0.1.1. The `ask` command now decomposes a query into
one or more **InformationNeeds** and produces a per-need answer.

#### Single-need queries

```bash
# Location (detected from semantic signals, not just "where")
./mysearch ask "where is stockholm"
# [LOCATION / SHORT_FACT] Entity: stockholm
# Confidence: 1.00
# Stockholm is Sweden's capital, built across 14 islands where Lake Malaren
# meets the Baltic Sea. ...

./mysearch ask "is stockholm close to the sea"
# [LOCATION / SHORT_FACT] Entity: stockholm
# (No "where" keyword — still detects LOCATION from "close to")

# Definition
./mysearch ask "what is a database"
# [DEFINITION / SHORT_FACT] Entity: database

# Temporal
./mysearch ask "when was the transistor invented"
# [TIME / SHORT_FACT] Entity: transistor

# Function / mechanism
./mysearch ask "how does TCP ensure reliability"
# [FUNCTION / EXPLANATION] Entity: tcp

# Advantages
./mysearch ask "what are the benefits of SQL"
# [ADVANTAGES / LIST] Entity: sql

# Limitations
./mysearch ask "what are the drawbacks of NoSQL"
# [LIMITATIONS / LIST] Entity: nosql

# Usage
./mysearch ask "databases for beginners what should I start with"
# [USAGE / LIST] Entity: databases
```

#### Multi-need queries (NEW capability)

The system splits compound questions into separate needs:

```bash
./mysearch ask "tell me where stockholm is and why it is important"
# [LOCATION / SHORT_FACT] Entity: stockholm
# Confidence: 1.00
# Stockholm is Sweden's capital, built across 14 islands ...
#
# [HISTORY / EXPLANATION] Entity: stockholm
# Confidence: ...
# Stockholm has been the political and economic center of Sweden ...

./mysearch ask "what is TCP and how does it work"
# [DEFINITION / SHORT_FACT] Entity: tcp
# ...
#
# [FUNCTION / EXPLANATION] Entity: tcp
# ...
```

#### JSON output

```bash
./mysearch ask "tell me where stockholm is and why it is important" --json
```

Output:

The output will contain a JSON object with a `needs` array — one entry per
InformationNeed — each with entity, property, form, confidence, and answer text.
Actual values depend on the indexed corpus.

#### More examples to try

```bash
# Implicit location (no interrogative word)
./mysearch ask "stockholm geography"

# History
./mysearch ask "tell me about the history of computing"

# Comparison (detected from "vs" / "difference")
./mysearch ask "difference between SQL and NoSQL"

# Composition
./mysearch ask "what are the types of databases"

# Multi-need with entity propagation
./mysearch ask "what is stockholm; why is it famous"
```

### Step 5 (optional, requires libtorch): Train neural encoder

```bash
./train_encoder --epochs 10 --dim 128
./mysearch hybrid "what is a database"
```

### Step 6 (optional, requires libtorch): Train neural query analyzer

```bash
./mysearch train-qa --epochs 30
./mysearch ask "when was the transistor invented"
# stderr: "Using neural query analyzer"
```

Note: The neural analyzer currently produces a single InformationNeed per query
(mapped from its legacy QueryAnalysis output). Multi-need decomposition is only
available via the rule-based analyzer in this version.

---

## 5. Quick Smoke Test (copy-paste)

Run this block after building to verify everything works.
**You must ingest before every test run if data files have changed.**

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON \
         -DUSE_TORCH=ON -DCMAKE_PREFIX_PATH=~/opt/libtorch
cmake --build .

./mysearch ingest ../data
./mysearch build-hnsw

echo "=== Single-need ==="
./mysearch ask "where is stockholm"
./mysearch ask "what is a database"
./mysearch ask "how does TCP work"

echo "=== Multi-need ==="
./mysearch ask "tell me where stockholm is and why it is important"
./mysearch ask "what is TCP and how does it work"

echo "=== JSON ==="
./mysearch ask "where is stockholm" --json
./mysearch ask "tell me where stockholm is and why it is important" --json
```
