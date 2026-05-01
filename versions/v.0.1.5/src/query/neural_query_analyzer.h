#pragma once
#ifdef HAS_TORCH

#include <torch/torch.h>
#include <string>
#include <vector>
#include "query_analyzer.h"
#include "../encoder/sentence_encoder.h"
#include "../embedding/vocab.h"

// Multi-task classification head: intent (5) + answer_type (7) + entity BIO tag (3 per token)
struct QueryClassifierImpl : torch::nn::Module {
    QueryClassifierImpl(int64_t vocab_size, int64_t dim = 128,
                        int64_t heads = 4, int64_t layers = 2,
                        int64_t max_len = 64);

    struct Output {
        torch::Tensor intent_logits;       // [B, 5]
        torch::Tensor answer_type_logits;  // [B, 7]
        torch::Tensor entity_logits;       // [B, T, 3]  BIO tags
    };

    Output forward(torch::Tensor tokens, torch::Tensor mask);
    int64_t dim() const { return dim_; }

private:
    int64_t dim_;
    SentenceEncoder encoder_{nullptr};
    // Re-encode without pooling for token-level entity tagging
    torch::nn::Embedding tok_emb_{nullptr};
    torch::Tensor pe_;
    torch::nn::TransformerEncoder token_encoder_{nullptr};

    torch::nn::Linear intent_head_{nullptr};
    torch::nn::Linear answer_head_{nullptr};
    torch::nn::Linear entity_head_{nullptr};
};
TORCH_MODULE(QueryClassifier);

// Training sample for query classification
struct QuerySample {
    std::string text;
    QueryIntent intent;
    AnswerType answer_type;
    std::string entity;  // the main entity substring
};

class NeuralQueryAnalyzer {
public:
    NeuralQueryAnalyzer(Vocabulary& vocab, int64_t dim = 128,
                        int64_t max_len = 64);

    // Generate training data from corpus heuristics, then train
    void train(const std::vector<QuerySample>& samples,
               int epochs = 20, double lr = 1e-3, int batch_size = 8);

    // Generate synthetic training samples from document texts
    static std::vector<QuerySample> generate_training_data(
        const std::vector<std::string>& doc_texts);

    QueryAnalysis analyze(const std::string& query) const;

    void save(const std::string& path);
    void load(const std::string& path);

private:
    std::pair<std::vector<int64_t>, std::vector<float>>
    tokenize(const std::string& text) const;

    std::string extract_entity_from_tags(
        const std::string& text,
        const std::vector<int64_t>& tag_ids) const;

    Vocabulary& vocab_;
    mutable QueryClassifier model_{nullptr};
    int64_t max_len_;
};

#endif // HAS_TORCH
