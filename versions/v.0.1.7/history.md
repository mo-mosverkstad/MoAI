# v.0.1.7 Change History

For build, test, and usage instructions, see [build.md](build.md).

---

## Step 1: Initialize from v.0.1.6

### Goal

Start v.0.1.7 from v.0.1.6 source code with v.0.1.5 data (21 documents, smaller corpus for faster iteration).

### What Was Done

- Copied v.0.1.6 source code, config, and tests (excluding build/, data/, embeddings/, segments/)
- Copied v.0.1.5 data/ (21 documents across 7 topics)
- Fixed config: `embedding.method = auto` (was `transformer` from VDI testing)
- Fixed test expectation: compression test expects `STRONG` (v.0.1.6 chunk continuation improves agreement)
- Verified build and 75/75 integration tests pass

### Baseline

- Source code: v.0.1.6 (unified `moai` CLI, entity fix, chunk continuation, progress bars, --threads, --resume)
- Data: v.0.1.5 (21 documents, vocab 2,785 words)
- All 75 integration tests pass
