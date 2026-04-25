#include "embedding_model.h"
#include <fstream>
#include <cmath>
#include <random>

bool EmbeddingModel::load(const std::string& path, size_t input_dim) {
    input_dim_ = input_dim;

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    f.read(reinterpret_cast<char*>(&hidden_dim_), sizeof(size_t));
    f.read(reinterpret_cast<char*>(&output_dim_), sizeof(size_t));

    W1.resize(hidden_dim_ * input_dim_);
    b1.resize(hidden_dim_);
    W2.resize(output_dim_ * hidden_dim_);
    b2.resize(output_dim_);

    f.read(reinterpret_cast<char*>(W1.data()), W1.size() * sizeof(float));
    f.read(reinterpret_cast<char*>(b1.data()), b1.size() * sizeof(float));
    f.read(reinterpret_cast<char*>(W2.data()), W2.size() * sizeof(float));
    f.read(reinterpret_cast<char*>(b2.data()), b2.size() * sizeof(float));

    return f.good();
}

void EmbeddingModel::init_random(size_t input_dim, size_t hidden_dim,
                                  size_t output_dim, uint32_t seed) {
    input_dim_ = input_dim;
    hidden_dim_ = hidden_dim;
    output_dim_ = output_dim;

    std::mt19937 rng(seed);
    // Xavier initialization scale
    auto xavier = [&](size_t fan_in, size_t fan_out) {
        float scale = std::sqrt(2.0f / static_cast<float>(fan_in + fan_out));
        std::uniform_real_distribution<float> dist(-scale, scale);
        return dist;
    };

    auto dist1 = xavier(input_dim, hidden_dim);
    W1.resize(hidden_dim * input_dim);
    for (auto& w : W1) w = dist1(rng);
    b1.assign(hidden_dim, 0.0f);

    auto dist2 = xavier(hidden_dim, output_dim);
    W2.resize(output_dim * hidden_dim);
    for (auto& w : W2) w = dist2(rng);
    b2.assign(output_dim, 0.0f);
}

bool EmbeddingModel::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    f.write(reinterpret_cast<const char*>(&hidden_dim_), sizeof(size_t));
    f.write(reinterpret_cast<const char*>(&output_dim_), sizeof(size_t));

    f.write(reinterpret_cast<const char*>(W1.data()), W1.size() * sizeof(float));
    f.write(reinterpret_cast<const char*>(b1.data()), b1.size() * sizeof(float));
    f.write(reinterpret_cast<const char*>(W2.data()), W2.size() * sizeof(float));
    f.write(reinterpret_cast<const char*>(b2.data()), b2.size() * sizeof(float));

    return f.good();
}

std::vector<float> EmbeddingModel::relu(const std::vector<float>& v) const {
    std::vector<float> r(v.size());
    for (size_t i = 0; i < v.size(); i++)
        r[i] = (v[i] > 0.0f) ? v[i] : 0.0f;
    return r;
}

void EmbeddingModel::l2_norm(std::vector<float>& v) const {
    float s = 0.0f;
    for (float x : v) s += x * x;
    s = std::sqrt(s);
    if (s > 0.0f)
        for (float& x : v) x /= s;
}

std::vector<float> EmbeddingModel::embed(const std::vector<float>& bow) const {
    // Layer 1: h = ReLU(W1 * bow + b1)
    std::vector<float> h(hidden_dim_, 0.0f);
    for (size_t hi = 0; hi < hidden_dim_; hi++) {
        float sum = b1[hi];
        for (size_t d = 0; d < input_dim_; d++)
            sum += W1[hi * input_dim_ + d] * bow[d];
        h[hi] = sum;
    }
    h = relu(h);

    // Layer 2: out = ReLU(W2 * h + b2)
    std::vector<float> out(output_dim_, 0.0f);
    for (size_t o = 0; o < output_dim_; o++) {
        float sum = b2[o];
        for (size_t hi = 0; hi < hidden_dim_; hi++)
            sum += W2[o * hidden_dim_ + hi] * h[hi];
        out[o] = sum;
    }
    out = relu(out);

    l2_norm(out);
    return out;
}
