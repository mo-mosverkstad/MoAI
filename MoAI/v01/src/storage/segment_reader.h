#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "../common/types.h"

struct PostingItem {
    DocID doc;
    uint32_t tf;
    std::vector<uint32_t> positions;
};

class SegmentReader {
public:
    explicit SegmentReader(const std::string& dir);

    std::vector<PostingItem> get_postings(const std::string& term);
    std::vector<uint32_t> get_positions_for_doc(const std::string& term, DocID d);

    uint32_t doc_length(DocID d) const;
    uint32_t doc_count() const { return static_cast<uint32_t>(doc_lens_.size()); }
    double average_doc_length() const;

    std::vector<std::string> all_terms() const;
    std::string get_document_text(DocID d) const;

private:
    void load_docs();
    void load_terms();
    void load_raw_docs();

private:
    std::string dir_;

    std::vector<uint32_t> doc_lens_;
    std::vector<std::string> doc_texts_;

    struct TermInfo {
        uint32_t term_len;
        std::string term;
        uint32_t offset;
        uint32_t length;
    };

    std::unordered_map<std::string, TermInfo> dict_;
};