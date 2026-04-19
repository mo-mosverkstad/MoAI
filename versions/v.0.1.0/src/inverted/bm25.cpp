#include "bm25.h"
#include <algorithm>
#include <unordered_set>

BM25::BM25(SegmentReader& reader, double k1, double b)
    : reader_(reader), k1_(k1), b_(b)
{
    // Compute corpus statistics
    N_ = reader_.doc_count();
    double total_len = 0.0;
    for (DocID d = 1; d <= N_; d++) {
        total_len += reader_.doc_length(d);
    }
    avgdl_ = (N_ > 0 ? total_len / N_ : 1.0);
}

double BM25::idf(const std::string& term) {
    auto postings = reader_.get_postings(term);
    uint32_t df = postings.size();
    if (df == 0) return 0.0;

    // Traditional BM25 IDF:
    // log( (N - df + 0.5) / (df + 0.5) + 1 )
    double num = (N_ - df + 0.5);
    double den = (df + 0.5);
    return std::log( (num / den) + 1.0 );
}

std::vector<std::pair<DocID, double>>
BM25::search(const std::vector<std::string>& tokens, size_t topK)
{
    if (tokens.empty()) return {};

    // Track unique query terms (remove duplicates)
    std::unordered_set<std::string> uniq(tokens.begin(), tokens.end());
    std::vector<std::string> qterms(uniq.begin(), uniq.end());

    struct Scored {
        DocID doc;
        double score;
        bool operator<(const Scored& o) const {
            return score > o.score; // min-heap
        }
    };

    std::priority_queue<Scored> heap;

    // Document → accumulated score
    std::unordered_map<DocID, double> scores;

    // Precompute IDF values
    std::unordered_map<std::string, double> idf_map;
    for (auto& t : qterms) {
        idf_map[t] = idf(t);
    }

    // Accumulate BM25 contributions
    for (auto& term : qterms) {
        auto postings = reader_.get_postings(term);
        double idf_val = idf_map[term];
        if (idf_val <= 0.0) continue;

        for (auto& p : postings) {
            DocID d = p.doc;
            double tf = p.tf;
            double dl = reader_.doc_length(d);

            double denom = tf + k1_ * (1.0 - b_ + b_ * (dl / avgdl_));
            double term_score = idf_val * ((tf * (k1_ + 1.0)) / denom);

            scores[d] += term_score;
        }
    }

    // Push into top‑K min‑heap
    for (auto& kv : scores) {
        DocID d = kv.first;
        double score = kv.second;

        if (heap.size() < topK) {
            heap.push({d, score});
        } else if (score > heap.top().score) {
            heap.pop();
            heap.push({d, score});
        }
    }

    // Extract sorted results (descending)
    std::vector<std::pair<DocID, double>> results;
    while (!heap.empty()) {
        results.push_back({ heap.top().doc, heap.top().score });
        heap.pop();
    }
    std::reverse(results.begin(), results.end());
    return results;
}

std::vector<std::pair<DocID, double>>
BM25::score_docs(const std::vector<std::string>& terms,
                 const std::vector<DocID>& docs)
{
    if (terms.empty() || docs.empty()) return {};

    std::unordered_set<DocID> doc_set(docs.begin(), docs.end());
    std::unordered_set<std::string> uniq(terms.begin(), terms.end());

    std::unordered_map<DocID, double> scores;

    for (auto& term : uniq) {
        double idf_val = idf(term);
        if (idf_val <= 0.0) continue;

        auto postings = reader_.get_postings(term);
        for (auto& p : postings) {
            if (doc_set.find(p.doc) == doc_set.end()) continue;
            double tf = p.tf;
            double dl = reader_.doc_length(p.doc);
            double denom = tf + k1_ * (1.0 - b_ + b_ * (dl / avgdl_));
            scores[p.doc] += idf_val * ((tf * (k1_ + 1.0)) / denom);
        }
    }

    std::vector<std::pair<DocID, double>> results;
    for (auto& kv : scores) {
        results.push_back({kv.first, kv.second});
    }
    return results;
}