# v.0.1.2 Change History

For build, test, and usage instructions, see [build.md](build.md).

---

## Step 1: AnswerScope Implementation

### Goal

Add **AnswerScope** — a dimension orthogonal to Property that controls *how much* information is produced, not *what* information is produced.

### AnswerScope Model

```cpp
enum class AnswerScope {
    STRICT,     // Minimal factual answer (1-2 sentences, max 200 chars)
    NORMAL,     // Default: fact + 1 supporting context (2-4 sentences, max 400 chars)
    EXPANDED    // Explainer / tutorial-style (multi-paragraph, max 700 chars)
};
```

### Scope Inference

Scope is inferred from query wording and answer form:

| Signal | Scope |
|--------|-------|
| "brief", "short", "quick", "just" | STRICT |
| "explain", "in detail", "overview", "describe", "comprehensive" | EXPANDED |
| AnswerForm::SHORT_FACT | STRICT |
| AnswerForm::EXPLANATION or SUMMARY | EXPANDED |
| Default | NORMAL |

### Confidence-Based Scope Adjustment

After confidence is computed, scope is dynamically adjusted:
- High confidence (>0.85) + NORMAL → compress to STRICT (sources agree, less explanation needed)
- Low confidence (<0.5) + STRICT → expand to NORMAL (add context for fragile answers)

### Synthesis Limits

| Scope | Max Chars | Max Segments |
|-------|-----------|-------------|
| STRICT | 200 | 2 |
| NORMAL | 400 | 4 |
| EXPANDED | 700 | 8 |

Text is truncated at sentence boundaries near the limit.

### Files Created

| File | Description |
|------|-------------|
| `src/answer/answer_scope.h` | `adjust_scope_by_confidence()`, `max_answer_chars()`, `max_answer_segments()` |

### Files Modified

| File | What Changed |
|------|-------------|
| `src/query/information_need.h` | Added `AnswerScope` enum, `scope` field to `InformationNeed`, `scope_str()` helper |
| `src/query/query_analyzer.h` | Added `infer_scope()` method declaration |
| `src/query/query_analyzer.cpp` | Implemented `infer_scope()` from query wording + form; wired into `analyze()` |
| `src/answer/answer_synthesizer.h` | Added `scope` field to `Answer` struct |
| `src/answer/answer_synthesizer.cpp` | Scope-aware text truncation in main `synthesize()` dispatch |
| `src/cli/commands.cpp` | Confidence-based scope adjustment; scope in JSON and text output |

### Example Output

```
# "where is stockholm" → STRICT (2 sentences)
{
  "scope": "STRICT",
  "answer": {
    "text": "Stockholm is situated on the eastern coast of Sweden, at the point where Lake Malaren flows into the Baltic Sea. The city spans 14 islands in an archipelago setting.",
    "confidence": 0.70
  }
}

# "explain in detail where stockholm is" → EXPANDED (full paragraph)
{
  "scope": "EXPANDED",
  "answer": {
    "text": "Stockholm is situated on the eastern coast of Sweden, at the point where Lake Malaren flows into the Baltic Sea. The city spans 14 islands in an archipelago setting. The Stockholm archipelago extends eastward into the Baltic Sea and contains over 30,000 islands, skerries, and rocks. Stockholm is close to the sea and enjoys a maritime-influenced climate with mild summers and cold winters.",
    "confidence": 0.55
  }
}
```

### Integration Test Results

All 67 tests pass: `Results: 67 passed, 0 failed, 67 total`

---

### How to Demo AnswerScope

AnswerScope is automatically inferred from query wording — no CLI flags needed.

#### STRICT scope (1-2 sentences, max 200 chars)

Triggered by: SHORT_FACT form, or words like "brief", "short", "quick", "just".

```bash
# SHORT_FACT form → STRICT
./mysearch ask "where is stockholm"
# [LOCATION / SHORT_FACT / STRICT] Entity: stockholm
# Stockholm is situated on the eastern coast of Sweden...

# Explicit "brief" → STRICT
./mysearch ask "briefly, what is python"
# [DEFINITION / SHORT_FACT / STRICT] Entity: python

# "just" → STRICT
./mysearch ask "just tell me what is a database"
```

#### NORMAL scope (2-4 sentences, max 400 chars)

Default for LIST and COMPARISON forms.

```bash
./mysearch ask "what are the drawbacks of NoSQL"
# [LIMITATIONS / LIST / NORMAL] Entity: nosql

./mysearch ask "what are the advantages of solar energy"
# [ADVANTAGES / LIST / NORMAL] Entity: solar
```

#### EXPANDED scope (multi-paragraph, max 700 chars)

Triggered by: EXPLANATION/SUMMARY form, or words like "explain", "in detail", "describe", "comprehensive", "thorough".

```bash
# "explain" → EXPANDED
./mysearch ask "explain how TCP ensures reliability"
# [FUNCTION / EXPLANATION / EXPANDED] Entity: tcp

# "in detail" → EXPANDED
./mysearch ask "describe stockholm in detail"
# [LOCATION / SHORT_FACT / EXPANDED] Entity: stockholm

# "comprehensive" → EXPANDED
./mysearch ask "comprehensive overview of blockchain technology"
```

#### Scope comparison (same topic, different scopes)

```bash
# STRICT — minimal
./mysearch ask "where is stockholm" --json
# scope: "STRICT", ~160 chars

# EXPANDED — detailed
./mysearch ask "explain in detail where stockholm is" --json
# scope: "EXPANDED", ~390 chars
```

#### Confidence-based auto-adjustment

Scope adjusts automatically based on evidence quality:
- High confidence (>0.85) + NORMAL → auto-compresses to STRICT
- Low confidence (<0.5) + STRICT → auto-expands to NORMAL

This is visible in the JSON output when the `scope` field differs from what the query wording alone would suggest.

#### Scope in output

```bash
# Plain text: scope appears in the header bracket
./mysearch ask "where is stockholm"
# [LOCATION / SHORT_FACT / STRICT] Entity: stockholm

# JSON: scope is a separate field
./mysearch ask "where is stockholm" --json
# "scope": "STRICT"
```

#### Quick demo script

```bash
./mysearch ingest ../data
./mysearch build-hnsw

echo "=== STRICT ==="
./mysearch ask "where is stockholm"
./mysearch ask "briefly, what is python"

echo "=== NORMAL ==="
./mysearch ask "what are the drawbacks of NoSQL"
./mysearch ask "what are the advantages of electricity"

echo "=== EXPANDED ==="
./mysearch ask "explain in detail how TCP ensures reliability"
./mysearch ask "describe the history of electric vehicles"

echo "=== Scope comparison (JSON) ==="
./mysearch ask "where is stockholm" --json
./mysearch ask "explain in detail where stockholm is" --json
```
