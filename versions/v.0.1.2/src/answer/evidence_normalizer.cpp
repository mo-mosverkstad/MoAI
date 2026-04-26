#include "evidence_normalizer.h"
#include <algorithm>
#include <cctype>

static std::string to_lower_en(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

// Domain keyword vocabularies (controlled, extendable)
static const std::vector<std::string> GEO_KEYWORDS = {
    "east", "west", "north", "south",
    "eastern", "western", "northern", "southern",
    "coast", "sea", "lake", "river", "ocean", "island",
    "border", "capital", "continent", "peninsula",
    "sweden", "europe", "asia", "pacific", "atlantic", "baltic",
    "located", "situated"
};

static const std::vector<std::string> TECH_KEYWORDS = {
    "database", "sql", "nosql", "relational", "distributed",
    "protocol", "tcp", "network", "internet", "encryption",
    "algorithm", "scalable", "efficient", "complexity",
    "blockchain", "decentralized", "consensus",
    "python", "programming", "interpreted", "compiled"
};

static const std::vector<std::string> SCIENCE_KEYWORDS = {
    "energy", "electric", "solar", "renewable", "fossil",
    "battery", "voltage", "current", "photovoltaic",
    "antibiotic", "bacteria", "resistance", "penicillin",
    "climate", "greenhouse", "carbon", "temperature",
    "planet", "mars", "orbit", "atmosphere"
};

static const std::vector<std::string> ECON_KEYWORDS = {
    "inflation", "price", "economy", "gdp", "debt",
    "interest", "monetary", "fiscal", "currency"
};

static const std::vector<std::string> NEGATION_MARKERS = {
    "not ", "no ", "never ", "neither ", "without ", "lack ",
    "cannot ", "unable ", "impossible "
};

// Directional opposites for contradiction detection
static const std::vector<std::pair<std::string, std::string>> OPPOSITES = {
    {"east", "west"}, {"eastern", "western"},
    {"north", "south"}, {"northern", "southern"},
    {"increase", "decrease"}, {"rising", "falling"},
    {"advantage", "disadvantage"}, {"benefit", "drawback"},
    {"cheap", "expensive"}, {"fast", "slow"},
    {"safe", "dangerous"}, {"reliable", "unreliable"},
    {"efficient", "inefficient"}, {"simple", "complex"}
};

NormalizedClaim EvidenceNormalizer::normalize(
    const std::string& entity, const Evidence& ev) const
{
    NormalizedClaim c;
    c.entity = to_lower_en(entity);
    c.property = ev.type;
    c.docId = ev.docId;

    std::string t = to_lower_en(ev.text);

    // Extract keywords from all domain vocabularies
    auto extract = [&](const std::vector<std::string>& vocab) {
        for (auto& kw : vocab)
            if (t.find(kw) != std::string::npos)
                c.keywords.insert(kw);
    };
    extract(GEO_KEYWORDS);
    extract(TECH_KEYWORDS);
    extract(SCIENCE_KEYWORDS);
    extract(ECON_KEYWORDS);

    // Extract negations
    for (auto& n : NEGATION_MARKERS)
        if (t.find(n) != std::string::npos)
            c.negations.insert(n);

    return c;
}

double agreement_score(const NormalizedClaim& a, const NormalizedClaim& b) {
    if (a.entity != b.entity) return 0.0;
    // Different properties can't agree or disagree
    if (a.property != b.property) return 0.0;

    // Negation mismatch = no agreement
    if (a.negations.empty() != b.negations.empty()) return 0.0;

    int overlap = 0;
    for (auto& k : a.keywords)
        if (b.keywords.count(k)) overlap++;

    int denom = static_cast<int>(std::max(a.keywords.size(), b.keywords.size()));
    if (denom == 0) return 0.0;

    return static_cast<double>(overlap) / denom;
}

bool contradicts(const NormalizedClaim& a, const NormalizedClaim& b) {
    if (a.entity != b.entity) return false;
    if (a.property != b.property) return false;
    // Same document can't contradict itself
    if (a.docId == b.docId) return false;

    // Negation conflict: one has negation, other doesn't
    if (a.negations.empty() != b.negations.empty()) return true;

    // Directional/semantic opposites
    for (auto& [x, y] : OPPOSITES) {
        if ((a.keywords.count(x) && b.keywords.count(y)) ||
            (a.keywords.count(y) && b.keywords.count(x)))
            return true;
    }

    return false;
}
