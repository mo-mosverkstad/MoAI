#include "evidence_normalizer.h"
#include "../common/vocab_loader.h"
#include <algorithm>
#include <cctype>
#include <sstream>

static std::string to_lower_en(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

struct EvidenceVocab {
    std::vector<std::vector<std::string>> domain_vocabs;
    std::vector<std::string> negations;
    std::vector<std::pair<std::string, std::string>> opposites;

    static const EvidenceVocab& get() {
        static EvidenceVocab ev = []() {
            auto m = VocabLoader::load("../config/vocabularies/evidence_domains.conf");
            EvidenceVocab v;
            // Load domain types dynamically from [DOMAINS] section
            for (auto& domain : VocabLoader::get(m, "DOMAINS")) {
                auto& words = VocabLoader::get(m, domain);
                if (!words.empty()) v.domain_vocabs.push_back(words);
            }
            v.negations = VocabLoader::get(m, "NEGATION");

            // Parse opposites: "word1 | word2"
            for (auto& entry : VocabLoader::get(m, "OPPOSITES")) {
                auto bar = entry.find('|');
                if (bar != std::string::npos) {
                    std::string a = entry.substr(0, bar);
                    std::string b = entry.substr(bar + 1);
                    while (!a.empty() && a.back() == ' ') a.pop_back();
                    while (!b.empty() && b.front() == ' ') b.erase(b.begin());
                    if (!a.empty() && !b.empty()) v.opposites.push_back({a, b});
                }
            }
            return v;
        }();
        return ev;
    }
};

NormalizedClaim EvidenceNormalizer::normalize(
    const std::string& entity, const Evidence& ev) const
{
    NormalizedClaim c;
    c.entity = to_lower_en(entity);
    c.property = ev.type;
    c.docId = ev.docId;

    std::string t = to_lower_en(ev.text);
    auto& vocab = EvidenceVocab::get();

    for (auto& domain : vocab.domain_vocabs)
        for (auto& kw : domain)
            if (t.find(kw) != std::string::npos)
                c.keywords.insert(kw);

    for (auto& n : vocab.negations)
        if (t.find(n) != std::string::npos)
            c.negations.insert(n);

    return c;
}

double agreement_score(const NormalizedClaim& a, const NormalizedClaim& b) {
    if (a.entity != b.entity) return 0.0;
    if (a.property != b.property) return 0.0;
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
    if (a.docId == b.docId) return false;

    if (a.negations.empty() != b.negations.empty()) return true;

    for (auto& [x, y] : EvidenceVocab::get().opposites) {
        if ((a.keywords.count(x) && b.keywords.count(y)) ||
            (a.keywords.count(y) && b.keywords.count(x)))
            return true;
    }
    return false;
}
