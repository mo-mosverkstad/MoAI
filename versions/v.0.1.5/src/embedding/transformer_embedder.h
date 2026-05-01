#pragma once
#ifdef HAS_TORCH

#include "i_embedder.h"
#include "vocab.h"
#include "../encoder/encoder_trainer.h"
#include <memory>

class TransformerEmbedder : public IEmbedder {
public:
    TransformerEmbedder(const std::string& encoder_path, const std::string& vocab_path) {
        vocab_.load(vocab_path);
        encoder_ = std::make_unique<EncoderTrainer>(vocab_, 128, 4, 2, 256);
        encoder_->load(encoder_path);
    }

    std::vector<float> embed(const std::string& text) override {
        return encoder_->encode(text);
    }

    size_t dim() const override { return encoder_->dim(); }
    std::string name() const override { return "transformer"; }

private:
    Vocabulary vocab_;
    std::unique_ptr<EncoderTrainer> encoder_;
};

#endif // HAS_TORCH
