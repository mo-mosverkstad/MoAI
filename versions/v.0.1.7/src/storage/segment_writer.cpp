#include "segment_writer.h"
#include "../common/file_utils.h"
#include "../common/varint.h"
#include <algorithm>
#include <filesystem>

SegmentWriter::SegmentWriter(const std::string& dir)
    : dir_(dir)
{
    std::filesystem::create_directories(dir_);
}

DocID SegmentWriter::add_document(
    const std::string& text,
    const std::vector<std::string>& tokens)
{
    DocID id = next_docid_++;
    doc_lens_.push_back(tokens.size());
    doc_texts_.push_back(text);

    // Track TF + positions per term
    std::unordered_map<std::string, PostingTmp> local;

    for (size_t i = 0; i < tokens.size(); i++) {
        const std::string& t = tokens[i];
        auto& p = local[t];
        if (p.doc == 0) p.doc = id;
        p.tf++;
        p.positions.push_back(i);
    }

    for (auto& kv : local) {
        postings_[kv.first].push_back(kv.second);
    }

    return id;
}

void SegmentWriter::finalize() {
    // Write doc table
    {
        std::vector<uint8_t> out;
        for (uint32_t dl : doc_lens_) varint::encode_u32(dl, out);
        fileutils::write_file(dir_ + "/docs.bin", out);
    }

    // Write postings + dictionary
    std::vector<uint8_t> termbuf;
    std::vector<uint8_t> postbuf;

    for (auto& kv : postings_) {
        const std::string& term = kv.first;
        auto& plist = kv.second;

        uint32_t offset = postbuf.size();

        // df
        varint::encode_u32(plist.size(), postbuf);

        DocID last_doc = 0;
        for (auto& p : plist) {
            uint32_t gap = p.doc - last_doc;
            last_doc = p.doc;

            varint::encode_u32(gap, postbuf);
            varint::encode_u32(p.tf, postbuf);

            // Positions: delta-encoded, shifted by +1 (0 = end marker)
            uint32_t last_pos = 0;
            for (uint32_t pos : p.positions) {
                uint32_t pgap = pos - last_pos + 1;
                last_pos = pos;
                varint::encode_u32(pgap, postbuf);
            }

            // End-of-positions marker
            varint::encode_u32(0, postbuf);
        }

        // Dictionary entry: term_len, term_bytes, offset, length
        varint::encode_u32(term.size(), termbuf);
        for (char c : term) termbuf.push_back((uint8_t)c);
        varint::encode_u32(offset, termbuf);
        varint::encode_u32((uint32_t)(postbuf.size() - offset), termbuf);
    }

    fileutils::write_file(dir_ + "/postings.bin", postbuf);
    fileutils::write_file(dir_ + "/terms.bin", termbuf);

    // Write raw document texts (length-prefixed)
    {
        std::vector<uint8_t> raw;
        for (auto& txt : doc_texts_) {
            varint::encode_u32(static_cast<uint32_t>(txt.size()), raw);
            for (char c : txt) raw.push_back(static_cast<uint8_t>(c));
        }
        fileutils::write_file(dir_ + "/rawdocs.bin", raw);
    }
}
