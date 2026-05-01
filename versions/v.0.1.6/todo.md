# v.0.1.6 — CLI Unification: `moai`

## Goal

Unify all executables (`mysearch`, `train_encoder`) into a single binary named `moai` with consistent subcommands.

## Steps

### Step 1: Copy v.0.1.5 as base

- Copy entire `v.0.1.5/` into `v.0.1.6/` as starting point
- Verify build still works before any changes

### Step 2: Rename executable `mysearch` → `moai`

- CMakeLists.txt: `add_executable(moai ...)` instead of `add_executable(mysearch ...)`
- Update all test scripts (`test_qa_integration.sh`, `test_config_matrix.sh`, `benchmark.sh`) to use `./moai`

### Step 3: Absorb `train_encoder` into `moai train-encoder`

- Move `train_main.cpp` logic into `commands.cpp` as a `train-encoder` subcommand
- Remove standalone `add_executable(train_encoder ...)` from CMakeLists.txt
- Subcommand: `./moai train-encoder --epochs 10 --dim 128`

### Step 4: Update documentation

- `build.md`: All commands use `./moai`
- `README.md`: Update usage examples
- `history.md`: Document the CLI unification
- `codebase_analysis.md`: Update architecture references

### Step 5: Verify

- Build (with and without libtorch)
- 75/75 integration tests pass
- Config matrix tests pass
- Benchmark script works
- `./moai --help` shows all subcommands

## Subcommand Summary (after unification)

| Command | Description |
|---------|-------------|
| `moai ingest <dir>` | Ingest documents |
| `moai build-hnsw` | Build HNSW vector index |
| `moai search <query>` | BM25 lexical search |
| `moai hybrid <query>` | Legacy hybrid search |
| `moai ask <query>` | QA pipeline |
| `moai train-encoder` | Train neural sentence encoder (requires libtorch) |
| `moai train-qa` | Train neural query analyzer (requires libtorch) |
