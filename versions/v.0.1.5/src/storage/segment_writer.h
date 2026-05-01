#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "../common/types.h"

struct PostingTmp {
    DocID doc;
    uint32_t tf;
    std::vector<uint32_t> positions;
};

class SegmentWriter {
public:
    explicit SegmentWriter(const std::string& dir);

    // 收集一篇文档
    DocID add_document(const std::string& normalized_text,
                       const std::vector<std::string>& tokens);

    // 写出 segment（sorted term -> postings）
    void finalize();

private:
    std::string dir_;
    DocID next_docid_ = 1;

    // term -> vec(postings)
    std::unordered_map<std::string, std::vector<PostingTmp>> postings_;

    std::vector<uint32_t> doc_lens_;
    std::vector<std::string> doc_texts_;
};