#include "index_reader.h"

IndexReader::IndexReader(const std::string& segdir)
    : reader_(segdir)
{}

std::vector<PostingItem> IndexReader::get_postings(const std::string& term) {
    return reader_.get_postings(term);
}

uint32_t IndexReader::doc_length(DocID d) const {
    return reader_.doc_length(d);
}