#pragma once
#include <torch/torch.h>
#include <string>
#include <vector>

struct SentenceEncoderImpl : torch::nn::Module {
    SentenceEncoderImpl(int64_t vocab_size, int64_t dim = 128,
                        int64_t heads = 4, int64_t layers = 2,
                        int64_t max_len = 256);

    // tokens: [B, T] int64, mask: [B, T] float (1=valid, 0=pad)
    // Returns L2-normalized embeddings [B, D]
    torch::Tensor forward(torch::Tensor tokens, torch::Tensor mask);

    int64_t dim() const { return dim_; }

private:
    int64_t dim_;
    int64_t max_len_;
    torch::nn::Embedding tok_emb_{nullptr};
    torch::Tensor pe_;  // positional encoding buffer
    torch::nn::TransformerEncoder encoder_{nullptr};
};
TORCH_MODULE(SentenceEncoder);

// InfoNCE contrastive loss
torch::Tensor contrastive_loss(torch::Tensor q, torch::Tensor d,
                                double temperature = 0.07);
