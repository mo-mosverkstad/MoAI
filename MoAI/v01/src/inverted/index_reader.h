#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "../common/types.h"
#include "../storage/segment_reader.h"

class IndexReader {
public:
    explicit IndexReader(const std::string& seg_dir);

    std::vector<PostingItem> get_postings(const std::string& term);

    uint32_t doc_length(DocID doc) const;
    uint32_t doc_count() const { return reader_.doc_count(); }
    double average_doc_length() const { return reader_.average_doc_length(); }

private:
    SegmentReader reader_;
};
