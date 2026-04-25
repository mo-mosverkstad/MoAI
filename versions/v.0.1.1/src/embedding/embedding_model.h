#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "vocab.h"

class EmbeddingModel {
public:
    bool load(const std::string& path, size_t input_dim);

    // Initialize with random weights (for bootstrapping / testing)
    void init_random(size_t input_dim, size_t hidden_dim, size_t output_dim,
                     uint32_t seed = 42);

    bool save(const std::string& path) const;

    std::vector<float> embed(const std::vector<float>& bow) const;

    size_t dim() const { return output_dim_; }

private:
    size_t input_dim_ = 0;
    size_t hidden_dim_ = 0;
    size_t output_dim_ = 0;

    std::vector<float> W1, b1;
    std::vector<float> W2, b2;

    std::vector<float> relu(const std::vector<float>& v) const;
    void l2_norm(std::vector<float>& v) const;
};
