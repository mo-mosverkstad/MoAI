#include "search_engine.h"
#include <algorithm>
#include <iostream>

SearchEngine::SearchEngine(SegmentReader& r)
    : reader_(r)
{}

std::vector<DocID> SearchEngine::eval_term(const std::string& t) {
    auto postings = reader_.get_postings(t);
    std::vector<DocID> v;
    v.reserve(postings.size());
    for (auto& p : postings) v.push_back(p.doc);
    return v;
}

std::vector<DocID> SearchEngine::eval_phrase(const Phrase& p) {
    if (p.terms.empty()) return {};

    // Intersect doc lists for all phrase terms
    std::vector<std::vector<DocID>> doc_lists;
    for (auto& term : p.terms) {
        auto postings = reader_.get_postings(term);
        std::vector<DocID> docs;
        docs.reserve(postings.size());
        for (auto& pi : postings) docs.push_back(pi.doc);
        std::sort(docs.begin(), docs.end());
        doc_lists.push_back(std::move(docs));
    }

    std::vector<DocID> candidates = doc_lists[0];
    for (size_t i = 1; i < doc_lists.size(); i++) {
        candidates = boolean_merge(candidates, doc_lists[i], BoolOp::AND);
    }

    // Position-aware filtering using PhraseMatcher
    std::vector<DocID> result;
    for (DocID d : candidates) {
        std::vector<std::vector<uint32_t>> pos_lists;
        bool ok = true;
        for (auto& term : p.terms) {
            auto pos = reader_.get_positions_for_doc(term, d);
            if (pos.empty()) { ok = false; break; }
            pos_lists.push_back(std::move(pos));
        }
        if (ok && PhraseMatcher::match(pos_lists))
            result.push_back(d);
    }
    return result;
}

std::vector<DocID> SearchEngine::boolean_merge(
    const std::vector<DocID>& a,
    const std::vector<DocID>& b,
    BoolOp op)
{
    std::vector<DocID> result;
    size_t i=0, j=0;

    if (op == BoolOp::AND) {
        while (i < a.size() && j < b.size()) {
            if (a[i] == b[j]) { result.push_back(a[i]); i++; j++; }
            else if (a[i] < b[j]) i++;
            else j++;
        }
    }
    else if (op == BoolOp::OR) {
        while (i < a.size() || j < b.size()) {
            if (j >= b.size() || (i < a.size() && a[i] < b[j]))
                result.push_back(a[i++]);
            else if (i >= a.size() || b[j] < a[i])
                result.push_back(b[j++]);
            else {
                result.push_back(a[i]);
                i++; j++;
            }
        }
    }
    else if (op == BoolOp::NOT) {
        while (i < a.size()) {
            if (j >= b.size()) { result.push_back(a[i++]); }
            else if (a[i] == b[j]) { i++; j++; }
            else if (a[i] < b[j]) { result.push_back(a[i]); i++; }
            else { j++; }
        }
    }
    return result;
}

std::vector<std::pair<DocID,double>>
SearchEngine::eval_expr(const Expr& e, BM25& scorer) {
    if (std::holds_alternative<TermNode>(e.node)) {
        auto t = std::get<TermNode>(e.node).term;
        auto docs = eval_term(t);
        return scorer.score_docs({t}, docs);
    }
    else if (std::holds_alternative<Phrase>(e.node)) {
        auto p = std::get<Phrase>(e.node);
        auto docs = eval_phrase(p);
        return scorer.score_docs(p.terms, docs);
    }
    else {
        auto* be = std::get<BoolExpr*>(e.node);

        auto left = eval_expr(*be->left, scorer);
        auto right = eval_expr(*be->right, scorer);

        std::vector<DocID> A, B;
        for (auto& kv : left) A.push_back(kv.first);
        for (auto& kv : right) B.push_back(kv.first);
        std::sort(A.begin(), A.end());
        std::sort(B.begin(), B.end());

        auto merged = boolean_merge(A, B, be->op);

        // Score merged docs
        std::vector<std::pair<DocID,double>> out;
        for (DocID d : merged) {
            double score = 0.0;
            // Recompute BM25 only on merged docs
            for (auto& kv : left)
                if (kv.first == d) score += kv.second;
            for (auto& kv : right)
                if (kv.first == d) score += kv.second;

            out.push_back({d, score});
        }
        return out;
    }
}

std::vector<std::pair<DocID,double>>
SearchEngine::search(const std::string& q, size_t topK) {
    QueryParser qp;
    Expr expr = qp.parse(q);

    BM25 scorer(reader_);
    auto raw = eval_expr(expr, scorer);

    // sort by score descending
    std::sort(raw.begin(), raw.end(),
              [](auto&a, auto&b){ return a.second > b.second; });

    if (raw.size() > topK) raw.resize(topK);

    if (json_) {
        std::cout << "{\n  \"results\": [\n";
        for (size_t i = 0; i < raw.size(); i++) {
            std::cout << "    { \"doc\": " << raw[i].first
                      << ", \"score\": " << raw[i].second << " }";
            if (i + 1 < raw.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "  ]\n}\n";
    }

    return raw;
}