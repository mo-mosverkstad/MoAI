#pragma once
#include <string>
#include <vector>
#include "query_parser.h"
#include "../storage/segment_reader.h"
#include "phrase_matcher.h"
#include "bm25.h"

class SearchEngine {
public:
    SearchEngine(SegmentReader& reader);
    std::vector<std::pair<DocID,double>> search(const std::string& q,
                                                size_t topK);

    void set_json_output(bool v) { json_ = v; }

private:
    SegmentReader& reader_;
    bool json_ = false;

    std::vector<std::pair<DocID,double>>
    eval_expr(const Expr& e, BM25& scorer);

    // helpers
    std::vector<DocID> eval_term(const std::string& t);
    std::vector<DocID> eval_phrase(const Phrase& p);
    std::vector<DocID> boolean_merge(const std::vector<DocID>& a,
                                     const std::vector<DocID>& b,
                                     BoolOp op);
};