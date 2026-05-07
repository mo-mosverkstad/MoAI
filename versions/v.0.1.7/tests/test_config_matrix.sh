#!/bin/bash
# MoAI Configuration Matrix Test Runner
# Runs test_qa_integration.sh with different algorithm combinations.
#
# Usage:
#   bash ../tests/test_config_matrix.sh                    # run default matrix
#   bash ../tests/test_config_matrix.sh combo1 combo2 ...  # run specific combos
#
# Run from the build/ directory after: ingest + build-hnsw
#
# Each combo is: analyzer:retriever:embedding
# Examples:
#   rule:bm25:auto
#   rule:hybrid:bow
#   auto:hybrid:auto

set +e

CONFIG="../config/default.conf"
TEST_SCRIPT="../tests/test_qa_integration.sh"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Default matrix: sensible combinations
DEFAULT_COMBOS=(
    "rule:bm25:auto"
    "rule:hybrid:auto"
    "rule:hybrid:bow"
    "rule:hnsw:auto"
    "auto:hybrid:auto"
)

# Parse args or use defaults
if [ $# -gt 0 ]; then
    COMBOS=("$@")
else
    COMBOS=("${DEFAULT_COMBOS[@]}")
fi

# Save original config
cp "$CONFIG" "${CONFIG}.bak"
trap 'cp "${CONFIG}.bak" "$CONFIG"; rm -f "${CONFIG}.bak"' EXIT

# Results tracking
declare -a COMBO_NAMES
declare -a COMBO_PASSED
declare -a COMBO_FAILED
declare -a COMBO_TOTAL
TOTAL_COMBOS=0
TOTAL_PASS=0
TOTAL_FAIL=0

run_combo() {
    local combo="$1"
    local analyzer=$(echo "$combo" | cut -d: -f1)
    local retriever=$(echo "$combo" | cut -d: -f2)
    local embedding=$(echo "$combo" | cut -d: -f3)

    TOTAL_COMBOS=$((TOTAL_COMBOS + 1))

    printf "\n${CYAN}════════════════════════════════════════${NC}\n"
    printf "${CYAN}  Combo %d: analyzer=%s retriever=%s embedding=%s${NC}\n" \
        "$TOTAL_COMBOS" "$analyzer" "$retriever" "$embedding"
    printf "${CYAN}════════════════════════════════════════${NC}\n"

    # Apply config
    cp "${CONFIG}.bak" "$CONFIG"
    sed -i "s/^query\.analyzer = .*/query.analyzer = $analyzer/" "$CONFIG"
    sed -i "s/^retrieval\.retriever = .*/retrieval.retriever = $retriever/" "$CONFIG"
    sed -i "s/^embedding\.method = .*/embedding.method = $embedding/" "$CONFIG"

    # Run tests, capture output
    local output
    output=$(bash "$TEST_SCRIPT" 2>/dev/null)
    local exit_code=$?

    # Strip ANSI color codes and parse results
    local clean_output=$(echo "$output" | sed 's/\x1b\[[0-9;]*m//g')
    local result_line=$(echo "$clean_output" | grep 'passed.*failed.*total')
    local passed=$(echo "$result_line" | sed 's/.*Results: \([0-9]*\) passed.*/\1/')
    local failed=$(echo "$result_line" | sed 's/.*passed, \([0-9]*\) failed.*/\1/')
    local total=$(echo "$result_line" | sed 's/.*failed, \([0-9]*\) total.*/\1/')

    passed=${passed:-0}
    failed=${failed:-0}
    total=${total:-0}

    COMBO_NAMES+=("$combo")
    COMBO_PASSED+=("$passed")
    COMBO_FAILED+=("$failed")
    COMBO_TOTAL+=("$total")
    TOTAL_PASS=$((TOTAL_PASS + passed))
    TOTAL_FAIL=$((TOTAL_FAIL + failed))

    if [ "$failed" -eq 0 ] && [ "$total" -gt 0 ]; then
        printf "${GREEN}  ✓ %d/%d passed${NC}\n" "$passed" "$total"
    elif [ "$total" -eq 0 ]; then
        printf "${RED}  ✗ No tests ran (command may have failed)${NC}\n"
        # Show any failures
        echo "$output" | grep -i 'FAIL\|ERROR\|FATAL' | head -5
    else
        printf "${RED}  ✗ %d/%d passed, %d failed${NC}\n" "$passed" "$total" "$failed"
        # Show failed tests
        echo "$output" | grep 'FAIL' | head -10
    fi
}

# Run all combos
for combo in "${COMBOS[@]}"; do
    run_combo "$combo"
done

# Summary report
printf "\n${CYAN}════════════════════════════════════════════════════════════${NC}\n"
printf "${CYAN}  CONFIGURATION MATRIX TEST REPORT${NC}\n"
printf "${CYAN}════════════════════════════════════════════════════════════${NC}\n\n"

printf "%-30s %8s %8s %8s\n" "COMBINATION" "PASSED" "FAILED" "TOTAL"
printf "%-30s %8s %8s %8s\n" "------------------------------" "--------" "--------" "--------"

all_green=true
for i in "${!COMBO_NAMES[@]}"; do
    local_pass=${COMBO_PASSED[$i]}
    local_fail=${COMBO_FAILED[$i]}
    local_total=${COMBO_TOTAL[$i]}

    if [ "$local_fail" -eq 0 ] && [ "$local_total" -gt 0 ]; then
        color=$GREEN
    else
        color=$RED
        all_green=false
    fi

    printf "${color}%-30s %8s %8s %8s${NC}\n" \
        "${COMBO_NAMES[$i]}" "$local_pass" "$local_fail" "$local_total"
done

printf "%-30s %8s %8s %8s\n" "------------------------------" "--------" "--------" "--------"
printf "%-30s %8d %8d %8d\n" "TOTAL" "$TOTAL_PASS" "$TOTAL_FAIL" "$((TOTAL_PASS + TOTAL_FAIL))"

printf "\n"
if $all_green; then
    printf "${GREEN}All %d configurations passed.${NC}\n" "$TOTAL_COMBOS"
else
    printf "${RED}Some configurations failed.${NC}\n"
fi
printf "\n"

# Restore original config
cp "${CONFIG}.bak" "$CONFIG"

if [ "$TOTAL_FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
