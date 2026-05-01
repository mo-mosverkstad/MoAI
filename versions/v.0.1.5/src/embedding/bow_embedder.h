#pragma once
#include "i_embedder.h"
#include "embedding_model.h"
#include "vocab.h"
#include "../inverted/tokenizer.h"
#include <memory>

class BoWEmbedder : public IEmbedder {
public:
    BoWEmbedder(const std::string& model_path, const std::string& vocab_path) {
        vocab_.load(vocab_path);
        model_.load(model_path, vocab_.size());
    }

    std::vector<float> embed(const std::string& text) override {
        auto tokens = tok_.tokenize(text);
        std::vector<float> bow(vocab_.size(), 0.0f);
        for (auto& t : tokens) {
            int vid = vocab_.id(t);
            if (vid >= 0) bow[vid] += 1.0f;
        }
        return model_.embed(bow);
    }

    size_t dim() const override { return model_.dim(); }
    std::string name() const override { return "bow"; }

private:
    Vocabulary vocab_;
    EmbeddingModel model_;
    Tokenizer tok_;
};
