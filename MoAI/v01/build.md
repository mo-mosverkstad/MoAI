# MySearch — Build & Usage Guide

## 1. Prerequisites

```bash
sudo apt update
sudo apt install -y build-essential cmake git unzip pkg-config
```

### Optional: Install libtorch (for neural encoder + neural query analyzer)

```bash
wget https://download.pytorch.org/libtorch/cpu/libtorch-shared-with-deps-2.1.0%2Bcpu.zip
unzip libtorch-shared-with-deps-2.1.0+cpu.zip
mkdir ~/opt
sudo mv libtorch ~/opt/libtorch
# Alternative if permission issues:
#   sudo mv libtorch /tmp/libtorch_tmp
#   mv /tmp/libtorch_tmp ~/opt/libtorch
```

---

## 2. Build

```bash
mkdir build && cd build
```

### Without libtorch (BoW fallback, always works)

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

### Disable tests (faster build)

```bash
cmake .. -DBUILD_TESTS=OFF
cmake --build .
```

---

## 3. Run Tests

```bash
# All tests via ctest
ctest --output-on-failure

# Or run the test binary directly (more verbose)
./mysearch_tests

# Run a specific test suite
./mysearch_tests --gtest_filter="BM25Test.*"
./mysearch_tests --gtest_filter="HNSWTest.*"

# Run a single test
./mysearch_tests --gtest_filter="SearchEngineTest.ANDQuery"
./mysearch_tests --gtest_filter="HNSWTest.RecallOnRandomData"

# List all available tests
./mysearch_tests --gtest_list_tests
```

---

## 4. Usage

### Step 1: Ingest documents

```bash
./mysearch ingest ../data
```

### Step 2: BM25 search (boolean + phrase)

```bash
./mysearch search "database"
# doc=4 score=3.40416 length=411

./mysearch search '"machine learning"'
# doc=7 score=4.47113 length=117
# doc=8 score=3.59409 length=121

./mysearch search 'stockholm AND capital'
# doc=11 score=7.24171 length=152

./mysearch search '"machine learning" OR database' --json
# { "results": [ { "doc": 7, "score": 4.47113 }, { "doc": 8, "score": 3.59409 }, { "doc": 4, "score": 3.40416 } ] }
```

### Step 3: Hybrid BM25+ANN search (BoW fallback)

```bash
./mysearch build-hnsw
./mysearch hybrid "what is a database"
./mysearch hybrid "stockholm sweden" --json
```

### Step 4: Question-answering

```bash
./mysearch ask "where is stockholm"
./mysearch ask "what is a database" --json
./mysearch ask "who is alan turing"
./mysearch ask "when was the transistor invented"
./mysearch ask "how does TCP work"
```

### Step 5 (optional, requires libtorch): Train neural sentence encoder

Trains a Transformer-based sentence encoder using contrastive learning (InfoNCE loss).
This improves the **hybrid search** command — the ANN side uses neural embeddings
instead of the default bag-of-words vectors, producing more semantically meaningful
similarity scores for document retrieval.

```bash
./train_encoder --epochs 10 --dim 128

# Hybrid search now auto-detects encoder.pt and uses neural embeddings
./mysearch hybrid "what is a database"
```

### Step 6 (optional, requires libtorch): Train neural query analyzer

Trains a multi-task Transformer classifier for the **ask** command's query analysis
stage. It replaces the rule-based intent/answer-type detection with a learned model
that jointly predicts query intent (5 classes), answer type (7 classes), and extracts
the main entity via BIO tagging — generalizing better to paraphrased or unusual queries.

```bash
./mysearch train-qa --epochs 30

# ask command now auto-detects qa_model.pt and uses neural analyzer
./mysearch ask "when was the transistor invented"
# stderr: "Using neural query analyzer"
```

### Sandboxed command execution

```bash
./mysearch run /bin/echo hello
# [Sandbox] exit code = 0
```
