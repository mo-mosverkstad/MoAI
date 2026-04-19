#pragma once
#include "sentence_encoder.h"
#include "../embedding/vocab.h"
#include "../storage/segment_reader.h"
#include <string>
#include <vector>

class EncoderTrainer {
public:
    EncoderTrainer(Vocabulary& vocab, int64_t dim = 128,
                   int64_t heads = 4, int64_t layers = 2,
                   int64_t max_len = 256);

    // Train on document pairs from segment
    void train(SegmentReader& reader, int epochs = 5,
               double lr = 3e-4, int batch_size = 4);

    void save(const std::string& path);
    void load(const std::string& path);

    // Encode a text string into an embedding vector
    std::vector<float> encode(const std::string& text);

    int64_t dim() const { return model_->dim(); }

private:
    // Tokenize text to vocab IDs, returns (token_ids, mask)
    std::pair<std::vector<int64_t>, std::vector<float>>
    tokenize(const std::string& text, int64_t max_len);

    Vocabulary& vocab_;
    SentenceEncoder model_{nullptr};
    int64_t max_len_;
};
