#pragma once
#include <string>
#include <vector>

struct IEmbedder {
    virtual ~IEmbedder() = default;

    // Embed a text string into a dense vector.
    virtual std::vector<float> embed(const std::string& text) = 0;

    // Embedding dimensionality.
    virtual size_t dim() const = 0;

    // Name of this embedder (for logging).
    virtual std::string name() const = 0;
};
