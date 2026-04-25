#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../query/information_need.h"

enum class ChunkType {
    LOCATION,
    DEFINITION,
    HISTORY,
    TEMPORAL,
    PERSON,
    PROCEDURE,
    GENERAL
};

struct Chunk {
    uint32_t docId;
    uint32_t chunkId;
    ChunkType type;
    std::string text;
};

class Chunker {
public:
    std::vector<Chunk> chunk_document(uint32_t docId,
                                      const std::string& fullText) const;

    // Property-aware chunk selection: preferred types get boosted,
    // others are kept with lower priority
    static std::vector<Chunk> select_chunks(
        const std::vector<Chunk>& chunks,
        Property property,
        size_t max_chunks = 10);

private:
    std::vector<std::string> split_paragraphs(const std::string& text) const;
    ChunkType classify_chunk(const std::string& paragraph) const;
};
