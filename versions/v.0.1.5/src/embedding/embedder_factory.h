#pragma once
#include "i_embedder.h"
#include "bow_embedder.h"
#include "../common/config.h"
#include <memory>
#include <string>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifdef HAS_TORCH
#include "transformer_embedder.h"
#endif

class EmbedderFactory {
public:
    // Returns nullptr if no embeddings available (caller should handle gracefully).
    static std::unique_ptr<IEmbedder> create(const std::string& embeddir) {
        auto& cfg = Config::instance();
        std::string method = cfg.get_string("embedding.method", "auto");

        std::string encoder_path = embeddir + "/encoder.pt";
        std::string model_path   = embeddir + "/model.bin";
        std::string vocab_path   = embeddir + "/vocab.txt";

        if (method == "transformer") {
#ifdef HAS_TORCH
            if (std::filesystem::exists(encoder_path) &&
                std::filesystem::exists(vocab_path)) {
                std::cerr << "Using transformer embedder (config)\n";
                return std::make_unique<TransformerEmbedder>(encoder_path, vocab_path);
            }
#endif
            throw std::runtime_error("Transformer embedder requested but model not found");
        }

        if (method == "bow") {
            if (std::filesystem::exists(model_path) &&
                std::filesystem::exists(vocab_path)) {
                std::cerr << "Using BoW embedder (config)\n";
                return std::make_unique<BoWEmbedder>(model_path, vocab_path);
            }
            throw std::runtime_error("BoW embedder requested but model not found");
        }

        if (method == "auto") {
#ifdef HAS_TORCH
            if (std::filesystem::exists(encoder_path) &&
                std::filesystem::exists(vocab_path)) {
                std::cerr << "Using transformer embedder (auto-detected)\n";
                return std::make_unique<TransformerEmbedder>(encoder_path, vocab_path);
            }
#endif
            if (std::filesystem::exists(model_path) &&
                std::filesystem::exists(vocab_path)) {
                std::cerr << "Using BoW embedder (auto-detected)\n";
                return std::make_unique<BoWEmbedder>(model_path, vocab_path);
            }
            return nullptr; // no embeddings available
        }

        throw std::runtime_error("Unknown embedding method: " + method +
            " (valid: bow, transformer, auto)");
    }
};
