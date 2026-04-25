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
    local expect_words="$4"
    local expect_validated="$5"
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

    if [ -n "$expect_property" ] && [ "$property" != "$expect_property" ]; then
        errors="${errors}  property: got '$property', expected '$expect_property'\n"
    fi

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

# ── Sweden ──
echo "--- Sweden ---"

check "Where is Stockholm" \
    "where is stockholm" \
    "LOCATION" "eastern,coast,sweden,sea" "true"

check "Stockholm close to sea (implicit)" \
    "is stockholm close to the sea" \
    "LOCATION" "sea,stockholm" "true"

check "Stockholm importance (multi, need 0)" \
    "tell me where stockholm is and why it is important" \
    "LOCATION" "eastern,coast,sea" "true" 0

check "Stockholm importance (multi, need 1)" \
    "tell me where stockholm is and why it is important" \
    "ADVANTAGES" "stockholm,important" "true" 1

echo ""

# ── Databases ──
echo "--- Databases ---"

check "What is a database" \
    "what is a database" \
    "DEFINITION" "database,data" "true"

check "Drawbacks of NoSQL" \
    "what are the drawbacks of NoSQL" \
    "LIMITATIONS" "nosql,limitation" "true"

check "Databases for beginners" \
    "databases for beginners what should I start with" \
    "USAGE" "beginner" ""

check "Why SQL is widely used" \
    "Why is SQL still widely used" \
    "ADVANTAGES" "sql" ""

echo ""

# ── Networking ──
echo "--- Networking ---"

check "How TCP ensures reliability" \
    "how does TCP ensure reliability" \
    "FUNCTION" "tcp" "true"

check "TCP reliability (explain)" \
    "Explain how TCP ensures reliability" \
    "FUNCTION" "tcp" ""

check "When networking became mainstream" \
    "When did computer networking start becoming mainstream" \
    "TIME" "1990" ""

echo ""

# ── Physics / Electricity ──
echo "--- Physics ---"

check "What is electricity" \
    "what is electricity" \
    "DEFINITION" "electric,charge" ""

check "How electricity works" \
    "how does electricity work" \
    "FUNCTION" "electron" ""

check "History of electricity" \
    "history of electricity" \
    "HISTORY" "" ""

check "Advantages of electricity" \
    "what are the advantages of electricity" \
    "ADVANTAGES" "versatile" ""

check "Limitations of electricity" \
    "what are the limitations of electricity" \
    "LIMITATIONS" "storage" ""

echo ""

# ── Solar Energy ──
echo "--- Solar Energy ---"

check "What is solar energy" \
    "what is solar energy" \
    "DEFINITION" "solar,sun" ""

check "How solar panels work" \
    "how do solar panels work" \
    "FUNCTION" "photovoltaic" ""

check "Advantages of solar energy" \
    "what are the advantages of solar energy" \
    "ADVANTAGES" "renewable" ""

check "Limitations of solar energy" \
    "what are the limitations of solar energy" \
    "LIMITATIONS" "intermittent" ""

echo ""

# ── Japan ──
echo "--- Japan ---"

check "Where is Japan" \
    "where is japan" \
    "LOCATION" "pacific,island,asia" "true"

check "Why Japan is important" \
    "why is japan important" \
    "ADVANTAGES" "japan,economy" ""

check "Limitations of Japan" \
    "what are the challenges facing japan" \
    "LIMITATIONS" "japan" ""

echo ""

# ── Python ──
echo "--- Python ---"

check "What is Python" \
    "what is python" \
    "DEFINITION" "python,programming" ""

check "Advantages of Python" \
    "what are the advantages of python" \
    "ADVANTAGES" "python,syntax" ""

check "Limitations of Python" \
    "what are the limitations of python" \
    "LIMITATIONS" "python,slow" ""

check "Python for beginners" \
    "is python good for beginners" \
    "USAGE" "beginner" ""

echo ""

# ── Climate Change ──
echo "--- Climate Change ---"

check "What is climate change" \
    "what is climate change" \
    "DEFINITION" "climate,temperature" ""

check "How climate change works" \
    "how does climate change work" \
    "FUNCTION" "greenhouse" ""

check "History of climate science" \
    "what is the history of climate change" \
    "HISTORY" "" ""

echo ""

# ── Cross-topic / Multi-need ──
echo "--- Multi-need & Cross-topic ---"

check "TCP definition + function (need 0)" \
    "what is TCP and how does it work" \
    "DEFINITION" "tcp" "" 0

check "TCP definition + function (need 1)" \
    "what is TCP and how does it work" \
    "FUNCTION" "" "" 1

check "Algorithm scalability" \
    "What makes an algorithm scalable" \
    "" "scalab" ""

echo ""

# ── Validation checks ──
echo "--- Validation ---"

check "NoSQL limitations validated" \
    "what are the drawbacks of NoSQL" \
    "LIMITATIONS" "nosql" "true"

check "Stockholm location validated" \
    "where is stockholm" \
    "LOCATION" "coast" "true"

check "Japan location validated" \
    "where is japan" \
    "LOCATION" "pacific" "true"

echo ""

# ── Antibiotics ──
echo "--- Antibiotics ---"

check "What are antibiotics" \
    "what are antibiotics" \
    "DEFINITION" "antibiotic" ""

check "How antibiotics work" \
    "how do antibiotics work" \
    "FUNCTION" "bacteria" ""

check "History of antibiotics" \
    "what is the history of antibiotics" \
    "HISTORY" "fleming,penicillin" ""

check "Advantages of antibiotics" \
    "what are the advantages of antibiotics" \
    "ADVANTAGES" "antibiotic" ""

check "Limitations of antibiotics" \
    "what are the limitations of antibiotics" \
    "LIMITATIONS" "resistance" ""

check "Antibiotics usage" \
    "what are antibiotics used for" \
    "USAGE" "infection" ""

echo ""

# ── Blockchain ──
echo "--- Blockchain ---"

check "What is blockchain" \
    "what is blockchain" \
    "DEFINITION" "blockchain,decentralized" ""

check "How blockchain works" \
    "how does blockchain work" \
    "FUNCTION" "block,consensus" ""

check "Advantages of blockchain" \
    "what are the advantages of blockchain" \
    "ADVANTAGES" "decentraliz" ""

check "Limitations of blockchain" \
    "what are the limitations of blockchain" \
    "LIMITATIONS" "scalab" ""

check "History of blockchain" \
    "what is the history of blockchain" \
    "HISTORY" "" ""

echo ""

# ── Inflation ──
echo "--- Inflation ---"

check "What is inflation" \
    "what is inflation" \
    "DEFINITION" "inflation,price" ""

check "How inflation works" \
    "how does inflation work" \
    "FUNCTION" "demand" ""

check "Limitations of inflation" \
    "what are the drawbacks of high inflation" \
    "LIMITATIONS" "purchasing" ""

check "History of inflation" \
    "what is the history of inflation" \
    "HISTORY" "" ""

echo ""

# ── Mars ──
echo "--- Mars ---"

check "Where is Mars" \
    "where is mars" \
    "LOCATION" "sun,planet" "true"

check "Mars composition" \
    "what is mars composed of" \
    "COMPOSITION" "mars" ""

check "Advantages of Mars exploration" \
    "what are the advantages of exploring mars" \
    "ADVANTAGES" "mars" ""

check "Limitations of Mars exploration" \
    "what are the challenges of mars exploration" \
    "LIMITATIONS" "mars" ""

check "History of Mars exploration" \
    "what is the history of mars exploration" \
    "HISTORY" "mars" ""

echo ""

# ── Electric Vehicles ──
echo "--- Electric Vehicles ---"

check "What are electric vehicles" \
    "what are electric vehicles" \
    "DEFINITION" "electric,battery" ""

check "How electric vehicles work" \
    "how do electric vehicles work" \
    "FUNCTION" "battery,motor" ""

check "Advantages of EVs" \
    "what are the advantages of electric vehicles" \
    "ADVANTAGES" "emission" ""

check "Limitations of EVs" \
    "what are the limitations of electric vehicles" \
    "LIMITATIONS" "charging" ""

check "EVs vs gasoline cars" \
    "electric vehicles vs gasoline cars" \
    "COMPARISON" "" ""

check "History of EVs" \
    "what is the history of electric vehicles" \
    "HISTORY" "electric" ""

echo ""

# ── Edge cases ──
echo "--- Edge Cases ---"

check "Polite prefix ignored" \
    "please tell me where is stockholm" \
    "LOCATION" "stockholm" ""

check "No interrogative word" \
    "stockholm geography" \
    "LOCATION" "stockholm" ""

check "Implicit advantages" \
    "why is python popular" \
    "ADVANTAGES" "python" ""

check "Implicit definition" \
    "blockchain explained" \
    "" "blockchain" ""

check "Short query" \
    "TCP" \
    "" "tcp" ""

echo ""

# ── Summary ──
echo "========================================"
printf "Results: ${GREEN}%d passed${NC}, ${RED}%d failed${NC}, %d total\n" "$PASS" "$FAIL" "$TOTAL"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
