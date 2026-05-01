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
moai train-qa [--epochs N]                             # requires libtorch
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
