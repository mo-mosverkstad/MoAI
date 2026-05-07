#include "segment_reader.h"
#include "../common/file_utils.h"
#include "../common/varint.h"
#include <sstream>

SegmentReader::SegmentReader(const std::string& dir)
    : dir_(dir)
{
    load_docs();
    load_terms();
    load_raw_docs();
}

void SegmentReader::load_docs() {
    auto data = fileutils::read_file(dir_ + "/docs.bin");
    size_t pos = 0;

    while (pos < data.size()) {
        uint32_t dl = varint::decode_u32(data, pos);
        doc_lens_.push_back(dl);
    }
}

void SegmentReader::load_terms() {
    auto data = fileutils::read_file(dir_ + "/terms.bin");
    size_t pos = 0;

    while (pos < data.size()) {
        uint32_t len = varint::decode_u32(data, pos);
        if (pos + len > data.size()) break;

        std::string term((char*)&data[pos], len);
        pos += len;

        uint32_t offset = varint::decode_u32(data, pos);
        uint32_t plen   = varint::decode_u32(data, pos);

        dict_[term] = { len, term, offset, plen };
    }
}

std::vector<PostingItem>
SegmentReader::get_postings(const std::string& term)
{
    auto it = dict_.find(term);
    if (it == dict_.end()) return {};

    const auto& meta = it->second;

    auto buf = fileutils::read_file(dir_ + "/postings.bin");
    size_t pos = meta.offset;

    uint32_t df = varint::decode_u32(buf, pos);

    std::vector<PostingItem> out;
    out.reserve(df);

    DocID last_doc = 0;

    for (uint32_t i = 0; i < df; i++) {
        uint32_t gap = varint::decode_u32(buf, pos);
        DocID doc = last_doc + gap;
        last_doc = doc;

        uint32_t tf = varint::decode_u32(buf, pos);

        // Read delta-encoded positions (shifted by +1, 0 = end marker)
        std::vector<uint32_t> positions;
        positions.reserve(tf);

        uint32_t last_pos = 0;
        while (true) {
            uint32_t pgap = varint::decode_u32(buf, pos);
            if (pgap == 0) break;
            uint32_t posv = last_pos + pgap - 1;
            last_pos = posv;
            positions.push_back(posv);
        }

        out.push_back({doc, tf, std::move(positions)});
    }

    return out;
}

std::vector<uint32_t>
SegmentReader::get_positions_for_doc(const std::string& term, DocID d)
{
    auto postings = get_postings(term);
    for (auto& p : postings)
        if (p.doc == d)
            return p.positions;
    return {};
}

uint32_t SegmentReader::doc_length(DocID d) const {
    if (d == 0 || d > doc_lens_.size()) return 0;
    return doc_lens_[d - 1];
}

double SegmentReader::average_doc_length() const {
    if (doc_lens_.empty()) return 0.0;
    uint64_t sum = 0;
    for (uint32_t dl : doc_lens_) sum += dl;
    return static_cast<double>(sum) / doc_lens_.size();
}

std::vector<std::string> SegmentReader::all_terms() const {
    std::vector<std::string> terms;
    terms.reserve(dict_.size());
    for (auto& [term, info] : dict_) terms.push_back(term);
    return terms;
}

void SegmentReader::load_raw_docs() {
    auto data = fileutils::read_file(dir_ + "/rawdocs.bin");
    if (data.empty()) return;
    size_t pos = 0;
    while (pos < data.size()) {
        uint32_t len = varint::decode_u32(data, pos);
        if (pos + len > data.size()) break;
        doc_texts_.emplace_back(reinterpret_cast<char*>(&data[pos]), len);
        pos += len;
    }
}

std::string SegmentReader::get_document_text(DocID d) const {
    if (d == 0 || d > doc_texts_.size()) return {};
    return doc_texts_[d - 1];
}
