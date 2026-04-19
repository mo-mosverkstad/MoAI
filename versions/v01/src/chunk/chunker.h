#pragma once
#include <string>
#include <vector>
#include <cstdint>

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

private:
    std::vector<std::string> split_paragraphs(const std::string& text) const;
    ChunkType classify_chunk(const std::string& paragraph) const;
};
