#!/bin/bash
# Integration test for MoAI v.0.1.1 QA pipeline
# Run from the build/ directory after: ingest + build-hnsw
#
# Usage:
#   cd build
#   ./mysearch ingest ../data
#   ./mysearch build-hnsw
#   bash ../tests/test_qa_integration.sh

set +e

PASS=0
FAIL=0
TOTAL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

check() {
    local desc="$1"
    local query="$2"
    local expect_property="$3"
    local expect_words="$4"       # comma-separated words that MUST appear in answer text (case-insensitive)
    local expect_validated="$5"   # "true" or "false" or "" (skip)
    local need_index="${6:-0}"

    TOTAL=$((TOTAL + 1))

    local output
    output=$(./mysearch ask "$query" --json 2>/dev/null)
    if [ $? -ne 0 ] || [ -z "$output" ]; then
        FAIL=$((FAIL + 1))
        printf "${RED}FAIL${NC} %s — command failed\n" "$desc"
        return
    fi

    local property answer_text validated confidence
    property=$(echo "$output" | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin)
    print(d['needs'][$need_index]['property'])
except: print('ERROR')
" 2>/dev/null)
    answer_text=$(echo "$output" | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin)
    print(d['needs'][$need_index]['answer']['text'])
except: print('')
" 2>/dev/null)
    validated=$(echo "$output" | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin)
    print(str(d['needs'][$need_index]['answer']['validated']).lower())
except: print('unknown')
" 2>/dev/null)
    confidence=$(echo "$output" | python3 -c "
import sys,json
try:
    d=json.load(sys.stdin)
    print(d['needs'][$need_index]['answer']['confidence'])
except: print(0)
" 2>/dev/null)

    local errors=""

    # Check property
    if [ -n "$expect_property" ] && [ "$property" != "$expect_property" ]; then
        errors="${errors}  property: got '$property', expected '$expect_property'\n"
    fi

    # Check answer contains expected words
    if [ -n "$expect_words" ]; then
        IFS=',' read -ra WORDS <<< "$expect_words"
        local answer_lower
        answer_lower=$(echo "$answer_text" | tr '[:upper:]' '[:lower:]')
        for word in "${WORDS[@]}"; do
            word_lower=$(echo "$word" | tr '[:upper:]' '[:lower:]' | xargs)
            if [ -n "$word_lower" ] && ! echo "$answer_lower" | grep -qi "$word_lower"; then
                errors="${errors}  answer missing: '$word_lower'\n"
            fi
        done
    fi

    # Check validated
    if [ -n "$expect_validated" ] && [ "$validated" != "$expect_validated" ]; then
        errors="${errors}  validated: got '$validated', expected '$expect_validated'\n"
    fi

    if [ -z "$errors" ]; then
        PASS=$((PASS + 1))
        printf "${GREEN}PASS${NC} %s (confidence=%s)\n" "$desc" "$confidence"
    else
        FAIL=$((FAIL + 1))
        printf "${RED}FAIL${NC} %s\n" "$desc"
        printf "$errors"
        printf "  answer: %.100s...\n" "$answer_text"
    fi
}

echo "========================================"
echo "MoAI v.0.1.1 QA Integration Tests"
echo "========================================"
echo ""

# --- Core single-need queries ---
echo "--- Core queries ---"

check "Where is Stockholm" \
    "where is stockholm" \
    "LOCATION" "eastern,coast,sweden,sea" "true"

check "Stockholm close to sea (implicit location)" \
    "is stockholm close to the sea" \
    "LOCATION" "sea,stockholm" "true"

check "What is a database" \
    "what is a database" \
    "DEFINITION" "database,data" "true"

check "How does TCP ensure reliability" \
    "how does TCP ensure reliability" \
    "FUNCTION" "tcp" "true"

check "Drawbacks of NoSQL" \
    "what are the drawbacks of NoSQL" \
    "LIMITATIONS" "nosql,limitation" "true"

check "Databases for beginners" \
    "databases for beginners what should I start with" \
    "USAGE" "beginner" ""

check "When did networking become mainstream" \
    "When did computer networking start becoming mainstream" \
    "TIME" "1990" ""

check "TCP reliability (explain)" \
    "Explain how TCP ensures reliability" \
    "FUNCTION" "tcp" ""

check "Stockholm close to sea (benchmark)" \
    "Is Stockholm close to the sea" \
    "LOCATION" "sea" "true"

echo ""

# --- Multi-need queries ---
echo "--- Multi-need queries ---"

check "Stockholm location (need 0)" \
    "tell me where stockholm is and why it is important" \
    "LOCATION" "eastern,coast,sea" "true" 0

check "Stockholm importance (need 1)" \
    "tell me where stockholm is and why it is important" \
    "ADVANTAGES" "stockholm,important" "true" 1

echo ""

# --- Validation checks ---
echo "--- Validation checks ---"

check "NoSQL limitations validated" \
    "what are the drawbacks of NoSQL" \
    "LIMITATIONS" "nosql" "true"

check "Stockholm location validated" \
    "where is stockholm" \
    "LOCATION" "coast" "true"

echo ""

# --- Summary ---
echo "========================================"
printf "Results: ${GREEN}%d passed${NC}, ${RED}%d failed${NC}, %d total\n" "$PASS" "$FAIL" "$TOTAL"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
