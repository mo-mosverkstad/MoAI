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
