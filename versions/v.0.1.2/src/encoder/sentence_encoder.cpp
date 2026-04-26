#include "sentence_encoder.h"
#include <cmath>

SentenceEncoderImpl::SentenceEncoderImpl(
    int64_t vocab_size, int64_t dim, int64_t heads,
    int64_t layers, int64_t max_len)
    : dim_(dim), max_len_(max_len)
{
    tok_emb_ = register_module("tok_emb",
        torch::nn::Embedding(vocab_size, dim));

    // Fixed sinusoidal positional encoding
    auto pe = torch::zeros({max_len, dim});
    for (int64_t pos = 0; pos < max_len; ++pos) {
        for (int64_t i = 0; i < dim; i += 2) {
            double div_term = std::pow(10000.0, static_cast<double>(i) / dim);
            pe[pos][i] = std::sin(pos / div_term);
            if (i + 1 < dim)
                pe[pos][i + 1] = std::cos(pos / div_term);
        }
    }
    pe_ = register_buffer("pe", pe);

    auto enc_layer_opts = torch::nn::TransformerEncoderLayerOptions(dim, heads)
        .dim_feedforward(4 * dim)
        .dropout(0.1)
        .activation(torch::kGELU);
    auto enc_layer = torch::nn::TransformerEncoderLayer(enc_layer_opts);

    encoder_ = register_module("encoder",
        torch::nn::TransformerEncoder(
            torch::nn::TransformerEncoderOptions(enc_layer, layers)));
}

torch::Tensor SentenceEncoderImpl::forward(
    torch::Tensor tokens, torch::Tensor mask)
{
    // tokens: [B, T], mask: [B, T]
    auto x = tok_emb_(tokens);  // [B, T, D]

    // Add positional encoding
    auto seq_len = x.size(1);
    x = x + pe_.index({torch::indexing::Slice(torch::indexing::None, seq_len)});

    // TransformerEncoder expects [T, B, D]
    x = x.transpose(0, 1);

    // key_padding_mask: True where padded
    auto key_padding_mask = (mask == 0);
    x = encoder_->forward(x, /*mask=*/{}, /*src_key_padding_mask=*/key_padding_mask);

    // Back to [B, T, D]
    x = x.transpose(0, 1);

    // Mean pooling over valid tokens
    auto mask_f = mask.unsqueeze(-1).to(x.dtype());  // [B, T, 1]
    auto sum = (x * mask_f).sum(1);                   // [B, D]
    auto denom = mask_f.sum(1).clamp_min(1e-6);       // [B, 1]
    auto emb = sum / denom;                            // [B, D]

    // L2 normalize
    emb = torch::nn::functional::normalize(emb,
        torch::nn::functional::NormalizeFuncOptions().p(2).dim(1));

    return emb;
}

torch::Tensor contrastive_loss(torch::Tensor q, torch::Tensor d,
                                double temperature) {
    // q, d: [B, D], already L2-normalized
    auto logits = torch::mm(q, d.t()) / temperature;  // [B, B]
    auto labels = torch::arange(q.size(0), torch::kLong).to(q.device());
    return torch::nn::functional::cross_entropy(logits, labels);
}
