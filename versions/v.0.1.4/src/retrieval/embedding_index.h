#pragma once
#include "../hnsw/hnsw_index.h"
#include "../embedding/embedding_model.h"
#include "../embedding/vocab.h"
#include "../inverted/tokenizer.h"
#include "../storage/segment_reader.h"
#include <memory>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef HAS_TORCH
#include "../encoder/encoder_trainer.h"
#endif

// Shared HNSW + embedding infrastructure.
// Handles BoW vs neural embedding detection, index building, and query embedding.
class EmbeddingIndex {
public:
    bool init(SegmentReader& reader, const std::string& embeddir) {
#ifdef HAS_TORCH
        std::string encoder_path = embeddir + "/encoder.pt";
        if (std::filesystem::exists(encoder_path) &&
            std::filesystem::exists(embeddir + "/vocab.txt")) {
            vocab_.load(embeddir + "/vocab.txt");
            neural_encoder_ = std::make_unique<EncoderTrainer>(vocab_, 128, 4, 2, 256);
            neural_encoder_->load(encoder_path);
            hnsw_ = std::make_unique<HNSWIndex>(
                static_cast<uint32_t>(neural_encoder_->dim()), 16, 200, 100);
            uint32_t N = reader.doc_count();
            for (uint32_t d = 1; d <= N; d++)
                hnsw_->add_point(neural_encoder_->encode(reader.get_document_text(d)));
            use_neural_ = true;
            ready_ = true;
            std::cerr << "HNSW index built (neural encoder)\n";
            return true;
        }
#endif
        if (std::filesystem::exists(embeddir + "/model.bin") &&
            std::filesystem::exists(embeddir + "/vocab.txt")) {
            vocab_.load(embeddir + "/vocab.txt");
            bow_model_ = std::make_unique<EmbeddingModel>();
            bow_model_->load(embeddir + "/model.bin", vocab_.size());
            hnsw_ = std::make_unique<HNSWIndex>(
                static_cast<uint32_t>(bow_model_->dim()), 16, 200, 100);
            Tokenizer tok;
            uint32_t N = reader.doc_count();
            for (uint32_t d = 1; d <= N; d++) {
                auto tokens = tok.tokenize(reader.get_document_text(d));
                std::vector<float> bow(vocab_.size(), 0.0f);
                for (auto& t : tokens) {
                    int vid = vocab_.id(t);
                    if (vid >= 0) bow[vid] += 1.0f;
                }
                hnsw_->add_point(bow_model_->embed(bow));
            }
            ready_ = true;
            std::cerr << "HNSW index built (BoW)\n";
            return true;
        }
        return false;
    }

    std::vector<float> embed_query(const std::vector<std::string>& keywords) const {
#ifdef HAS_TORCH
        if (use_neural_ && neural_encoder_) {
            std::string text;
            for (auto& kw : keywords) {
                if (!text.empty()) text += " ";
                text += kw;
            }
            return neural_encoder_->encode(text);
        }
#endif
        if (bow_model_) {
            std::vector<float> bow(vocab_.size(), 0.0f);
            for (auto& kw : keywords) {
                int vid = vocab_.id(kw);
                if (vid >= 0) bow[vid] += 1.0f;
            }
            return bow_model_->embed(bow);
        }
        return {};
    }

    bool ready() const { return ready_; }
    HNSWIndex* hnsw() const { return hnsw_.get(); }

private:
    Vocabulary vocab_;
    std::unique_ptr<EmbeddingModel> bow_model_;
    std::unique_ptr<HNSWIndex> hnsw_;
    bool ready_ = false;
    bool use_neural_ = false;
#ifdef HAS_TORCH
    std::unique_ptr<EncoderTrainer> neural_encoder_;
#endif
};
