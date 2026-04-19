#pragma once
#include <string>
#include "../storage/segment_writer.h"
#include "tokenizer.h"

/**
 * IndexBuilder builds a single-segment inverted index.
 * MVP version:
 *   - Tokenizes files in a directory
 *   - Writes one new segment
 */
class IndexBuilder {
public:
    IndexBuilder(const std::string& segdir);

    /** Ingest all files in a directory (recursively). */
    void ingest_directory(const std::string& dirpath);

    /** Finalize and write the segment. */
    void finalize();

private:
    void ingest_file(const std::string& path);

private:
    Tokenizer tokenizer_;
    SegmentWriter writer_;
};