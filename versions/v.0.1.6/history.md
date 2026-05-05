# v.0.1.6 Change History

For build, test, and usage instructions, see [build.md](build.md).

---

## Step 1: Copy v.0.1.5 as Base

### Goal

Start v.0.1.6 from a clean copy of v.0.1.5 (the profiling version) as the foundation for CLI unification.

### What Was Done

- Copied entire v.0.1.5 into v.0.1.6 (excluding build directory)
- Verified build succeeds in WSL Ubuntu
- All 152 source/config/test/data files carried over

### Verification

- `cmake .. -DCMAKE_BUILD_TYPE=Release` — configures successfully
- `cmake --build .` — compiles and links without errors

---

## Step 2: Rename Executable `mysearch` → `moai`

### Goal

Unify the CLI entry point under the project name `moai`. All user-facing commands now use `./moai` instead of `./mysearch`.

### What Was Changed

| File | Change |
|------|--------|
| `CMakeLists.txt` | Project renamed `mysearch` → `moai`. Executable: `moai`. Library: `moai_lib`. Encoder lib: `moai_encoder`. Test binary: `moai_tests`. |
| `tests/test_qa_integration.sh` | `./mysearch` → `./moai` (4 occurrences) |
| `tests/benchmark.sh` | `./mysearch` → `./moai` (1 occurrence) |

### Verification

- Clean rebuild produces `./moai` binary (864KB)
- `./moai ingest ../data` — works
- `./moai build-hnsw` — works
- 75/75 integration tests pass

---

## Step 3: Absorb `train_encoder` into `moai train-encoder`

### Goal

Eliminate the standalone `train_encoder` binary. All training commands are now subcommands of `moai`.

### What Was Changed

| File | Change |
|------|--------|
| `src/cli/commands.cpp` | Added `train-encoder` subcommand (same logic as `train_main.cpp`): `--epochs`, `--dim`, `--lr`, `--segdir`, `--embeddir`. Updated all usage strings from `mysearch` to `moai`. |
| `CMakeLists.txt` | Removed `add_executable(train_encoder ...)` and its `target_link_libraries`. |

### Unified CLI

```
moai ingest <path>
moai search <query>
moai build-hnsw
moai hybrid <query>
moai ask <query> [--json] [--brief] [--detailed] [--profile]
moai train-encoder [--epochs N] [--dim D] [--lr R]    # requires libtorch
moai train-qa [--epochs N] [--threads N] [--resume]    # requires libtorch
moai run <cmd> [args...]
```

### Verification

- Clean rebuild produces single `./moai` binary (no `train_encoder` binary)
- `./moai` (no args) shows updated usage with `moai` prefix
- 75/75 integration tests pass

---

## Step 4: Update Documentation

### Goal

Update all documentation to reflect the unified `moai` CLI. Remove all `mysearch` and `train_encoder` references.

### What Was Changed

| File | Change |
|------|--------|
| `README.md` | Version → v.0.1.6, all `./mysearch` → `./moai` |
| `build.md` | Version → v.0.1.6, all `./mysearch` → `./moai`, `./train_encoder` → `./moai train-encoder`, fixed typo (`profiling.jsonls` → `profiling.jsonl`) |
| `codebase_analysis.md` | Version → v.0.1.6, noted unified CLI binary, `train_main.cpp` marked as legacy, file structure version updated |
| Root `README.md` | `./mysearch` → `./moai` in test commands, added v.0.1.5 and v.0.1.6 to version table |

### Verification

- No remaining `mysearch` references in any documentation or test scripts
- 75/75 integration tests pass

---

## Step 5: Final Verification

### Goal

Verify the complete CLI unification works correctly across all test dimensions.

### Results

| Check | Result |
|-------|--------|
| Build without libtorch | ✅ Only `moai` binary produced (no `train_encoder`) |
| `./moai` usage output | ✅ All subcommands show `moai` prefix |
| Unit tests (GoogleTest) | ✅ 76/76 pass |
| Integration tests | ✅ 75/75 pass |
| Config matrix (rule:hybrid:bow) | ✅ 75/75 pass |
| Config matrix (rule:bm25:auto) | ✅ 74/75 (known: 1 compression test requires hybrid) |
| Benchmark script | ✅ Works with `moai` binary |

### v.0.1.6 Complete

Single unified binary `moai` replaces both `mysearch` and `train_encoder`. All subcommands:

```
moai ingest <path>
moai search <query>
moai build-hnsw
moai hybrid <query>
moai ask <query> [--json] [--brief] [--detailed] [--profile]
moai train-encoder [--epochs N] [--dim D] [--lr R]    # requires libtorch
moai train-qa [--epochs N] [--threads N] [--resume]    # requires libtorch
moai run <cmd> [args...]
```

---

## Step 5b: Fix Entity Extraction + Answer Truncation

### Problem

`./moai ask "explain how TCP works"` produced:
- Entity: `works` (wrong — should be `tcp`)
- Answer truncated at "through several mechanisms:" (bullet-point list missing)

### Root Causes

1. **Entity extraction**: picked longest keyword (`works` > `tcp`) instead of recognizing `TCP` as an acronym
2. **Chunk continuation**: the bullet-point list was a separate chunk that didn't get included because it scored low on keyword matching

### Fixes

| File | Change |
|------|--------|
| `config/vocabularies/language.conf` | Added common verbs to `NON_ENTITY_WORDS`: works, ensures, functions, operates, uses, provides, supports, etc. |
| `src/query/query_analyzer.h` | Added `original_query` parameter to `extract_entity` |
| `src/query/query_analyzer.cpp` | `extract_entity` now prefers tokens that were all-uppercase in the original query (acronym detection: TCP, SQL, etc.) |
| `src/chunk/chunker.cpp` | `select_chunks` includes continuation chunks (next adjacent chunk when a selected chunk ends with `:`) |
| `src/pipeline/pipeline.cpp` | Evidence building merges continuation text (if chunk ends with `:`, appends next chunk's text) |
| `src/answer/answer_synthesizer.cpp` | Increased text limit from 500 to 1500 chars for explanation synthesis |

### Result

Before:
```
Entity: works
## How TCP Ensures Reliability TCP (Transmission Control Protocol) ensures reliable data delivery through several mechanisms:
```

After:
```
Entity: tcp
## How TCP Ensures Reliability TCP (Transmission Control Protocol) ensures reliable data delivery through several mechanisms: Three-way handshake: TCP establishes a connection using a SYN, SYN-ACK, ACK sequence... Retransmission: if a segment is lost or corrupted, TCP automatically retransmits it... Flow control: TCP uses a sliding window mechanism...
```

### Verification

- 75/75 integration tests pass (no regressions)

---

## Step 5c: CPU Throttling and Pause/Resume for `train-qa`

### Problem

`moai train-qa --epochs 30` consumed 100% of all CPU cores on a shared VDI, making the machine unusable for others. Training also could not be interrupted and resumed — a Ctrl+C meant starting over from epoch 1.

### Fixes

| Change | File | Description |
|--------|------|-------------|
| Added `--threads N` flag | `src/cli/commands.cpp` | Calls `torch::set_num_threads(N)` and `torch::set_num_interop_threads(N)` to limit CPU cores used |
| Added `--resume` flag | `src/cli/commands.cpp` | Loads checkpoint and reads saved epoch number before training |
| Added per-epoch checkpoint saving | `src/query/neural_query_analyzer.cpp` | Saves `qa_checkpoint.pt` and `qa_checkpoint_epoch_txt` after each epoch |
| Updated train method signature | `src/query/neural_query_analyzer.h/.cpp` | Added `start_epoch` and `checkpoint_path` parameters |
| Auto-cleanup on completion | `src/cli/commands.cpp` | Deletes checkpoint files after successful full training |
| Updated usage string | `src/cli/commands.cpp` | Shows `[--threads N] [--resume]` |

### CPU Usage

`--threads N` limits libtorch to N CPU cores. Usage is approximately `N / total_cores × 100%`:

| `--threads` | CPU usage (16-core) | Approx. time |
|-------------|--------------------|--------------:|
| (default)   | ~100%              | ~30 hours     |
| 4           | ~25%               | ~120 hours    |
| 2           | ~12%               | ~240 hours    |

### Pause and Resume

```bash
./moai train-qa --epochs 30 --threads 4    # start
# Ctrl+C at any epoch boundary
./moai train-qa --epochs 30 --threads 4 --resume   # continue
```

Checkpoint files are saved to `embeddings/qa_checkpoint.pt` and `embeddings/qa_checkpoint_epoch.txt` after each epoch, and deleted automatically on successful completion.

### Verification

- 75/75 integration tests pass
