#!/bin/bash
# MoAI Benchmark Runner
# Runs a set of queries multiple times with profiling enabled and produces statistics.
#
# Usage:
#   bash ../tests/benchmark.sh                                    # defaults
#   bash ../tests/benchmark.sh --queries ../tests/benchmark_queries.txt --repeat 3
#   bash ../tests/benchmark.sh --config hybrid                    # test specific config
#
# Run from the build/ directory after: ingest + build-hnsw

set +e

QUERIES="../tests/benchmark_queries.txt"
REPEAT=3
RETRIEVER=""
ANALYZER=""
EMBEDDING=""
PROFILE_FILE="../profiling.jsonl"
CONFIG_FILE="../config/default.conf"

# Parse args
while [ $# -gt 0 ]; do
    case "$1" in
        --queries) QUERIES="$2"; shift 2 ;;
        --repeat) REPEAT="$2"; shift 2 ;;
        --retriever) RETRIEVER="$2"; shift 2 ;;
        --analyzer) ANALYZER="$2"; shift 2 ;;
        --embedding) EMBEDDING="$2"; shift 2 ;;
        --config) # shorthand: analyzer:retriever:embedding
            IFS=':' read -r ANALYZER RETRIEVER EMBEDDING <<< "$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; echo "Usage: benchmark.sh [--repeat N] [--retriever bm25|hnsw|hybrid] [--analyzer rule|neural|auto] [--embedding bow|transformer|auto] [--config analyzer:retriever:embedding] [--queries file]"; exit 1 ;;
    esac
done

if [ ! -f "$QUERIES" ]; then
    echo "Error: queries file not found: $QUERIES"
    exit 1
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

# Apply config overrides
if [ -n "$RETRIEVER" ]; then
    sed -i "s/retrieval.retriever = .*/retrieval.retriever = $RETRIEVER/" "$CONFIG_FILE"
fi
if [ -n "$ANALYZER" ]; then
    sed -i "s/query.analyzer = .*/query.analyzer = $ANALYZER/" "$CONFIG_FILE"
fi
if [ -n "$EMBEDDING" ]; then
    sed -i "s/embedding.method = .*/embedding.method = $EMBEDDING/" "$CONFIG_FILE"
fi
sync 2>/dev/null || true

PROFILE_FLAG="--profile"

# Clear profile output
rm -f "$PROFILE_FILE"

QUERY_COUNT=$(wc -l < "$QUERIES" | tr -d ' ')
TOTAL_RUNS=$((QUERY_COUNT * REPEAT))

printf "${CYAN}MoAI Benchmark${NC}\n"
printf "  Queries: %s (%d queries)\n" "$QUERIES" "$QUERY_COUNT"
printf "  Repeat:  %d times\n" "$REPEAT"
printf "  Total:   %d runs\n" "$TOTAL_RUNS"
if [ -n "$RETRIEVER" ]; then printf "  Retriever: %s\n" "$RETRIEVER"; fi
if [ -n "$ANALYZER" ]; then printf "  Analyzer:  %s\n" "$ANALYZER"; fi
if [ -n "$EMBEDDING" ]; then printf "  Embedding: %s\n" "$EMBEDDING"; fi
printf "\nRunning"

# Run queries
for run in $(seq 1 $REPEAT); do
    while IFS= read -r query || [ -n "$query" ]; do
        [ -z "$query" ] && continue
        ./moai ask "$query" $PROFILE_FLAG < /dev/null > /dev/null 2>/dev/null
        printf "."
    done < "$QUERIES"
done
printf " done\n\n"

# Analyze results with python
if [ ! -f "$PROFILE_FILE" ]; then
    echo "Error: no profile data generated"
    exit 1
fi

python3 - "$PROFILE_FILE" "$TOTAL_RUNS" << 'PYTHON'
import json, sys
from collections import defaultdict

profile_file = sys.argv[1]
expected_runs = int(sys.argv[2])

# Parse all records
records = []
with open(profile_file) as f:
    for line in f:
        line = line.strip()
        if line:
            try:
                records.append(json.loads(line))
            except:
                pass

if not records:
    print("No profile records found")
    sys.exit(1)

# Collect timing per component
timings = defaultdict(list)
for rec in records:
    for comp, ms in rec.get("timing_ms", {}).items():
        timings[comp].append(ms)

# Collect quality
confidences = [rec["quality"]["confidence"] for rec in records if "quality" in rec]
memory_deltas = [rec["memory_mb"]["rss_after"] - rec["memory_mb"]["rss_before"]
                 for rec in records if "memory_mb" in rec]

# Statistics
def stats(values):
    if not values:
        return 0, 0, 0, 0
    s = sorted(values)
    n = len(s)
    return s[n//2], s[int(n*0.95)] if n > 1 else s[0], sum(s)/n, s[-1]

# Print report
print(f"{'Component':<20} {'p50':>8} {'p95':>8} {'mean':>8} {'max':>8}")
print(f"{'-'*20} {'-'*8} {'-'*8} {'-'*8} {'-'*8}")

# Order: Total first, then alphabetical
order = ["Total"] + sorted(k for k in timings if k != "Total")
for comp in order:
    vals = timings[comp]
    p50, p95, mean, mx = stats(vals)
    print(f"{comp:<20} {p50:>7.1f}ms {p95:>7.1f}ms {mean:>7.1f}ms {mx:>7.1f}ms")

print()
print(f"Queries:    {len(records)} runs")
if confidences:
    print(f"Confidence: mean={sum(confidences)/len(confidences):.2f} min={min(confidences):.2f} max={max(confidences):.2f}")
if memory_deltas:
    print(f"Memory:     mean delta={sum(memory_deltas)/len(memory_deltas):.2f}MB max delta={max(memory_deltas):.2f}MB")

# Algorithm info from first record
if records:
    algos = records[0].get("algorithms", {})
    print(f"Algorithms: {', '.join(f'{k}={v}' for k,v in algos.items())}")
PYTHON

# Restore config
if [ -n "$RETRIEVER" ]; then
    sed -i 's/retrieval.retriever = .*/retrieval.retriever = hybrid/' "$CONFIG_FILE"
fi
if [ -n "$ANALYZER" ]; then
    sed -i 's/query.analyzer = .*/query.analyzer = auto/' "$CONFIG_FILE"
fi
if [ -n "$EMBEDDING" ]; then
    sed -i 's/embedding.method = .*/embedding.method = auto/' "$CONFIG_FILE"
fi

echo ""
