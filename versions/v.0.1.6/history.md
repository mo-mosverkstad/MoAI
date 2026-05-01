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
