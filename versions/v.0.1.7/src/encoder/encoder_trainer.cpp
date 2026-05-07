#include "encoder_trainer.h"
#include "../inverted/tokenizer.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <random>

EncoderTrainer::EncoderTrainer(Vocabulary& vocab, int64_t dim,
                                int64_t heads, int64_t layers,
                                int64_t max_len)
    : vocab_(vocab), max_len_(max_len)
{
    model_ = SentenceEncoder(static_cast<int64_t>(vocab.size()) + 1,
                              dim, heads, layers, max_len);
}

std::pair<std::vector<int64_t>, std::vector<float>>
EncoderTrainer::tokenize(const std::string& text, int64_t max_len) {
    Tokenizer tok;
    auto words = tok.tokenize(text);

    std::vector<int64_t> ids;
    std::vector<float> mask;

    for (auto& w : words) {
        if (static_cast<int64_t>(ids.size()) >= max_len) break;
        int vid = vocab_.id(w);
        // Use vocab_size as UNK token (last index)
        ids.push_back(vid >= 0 ? vid : static_cast<int64_t>(vocab_.size()));
        mask.push_back(1.0f);
    }

    // Pad to max_len
    while (static_cast<int64_t>(ids.size()) < max_len) {
        ids.push_back(0);
        mask.push_back(0.0f);
    }

    return {ids, mask};
}

void EncoderTrainer::train(SegmentReader& reader, int epochs,
                            double lr, int batch_size) {
    model_->train();

    torch::optim::AdamW optimizer(
        model_->parameters(),
        torch::optim::AdamWOptions(lr).weight_decay(0.01));

    // Collect all document texts
    uint32_t N = reader.doc_count();
    std::vector<std::string> docs;
    for (uint32_t d = 1; d <= N; d++) {
        docs.push_back(reader.get_document_text(d));
    }

    // Generate training pairs: (query_chunk, doc_chunk) from same document
    // Split each doc into sentence-like chunks for query/doc pairs
    struct Pair { std::string query; std::string doc; };
    std::vector<Pair> pairs;

    Tokenizer tok;
    for (auto& doc_text : docs) {
        auto words = tok.tokenize(doc_text);
        if (words.size() < 10) continue;

        // Create overlapping chunks as training pairs
        for (size_t i = 0; i + 10 <= words.size(); i += 5) {
            size_t qend = std::min(i + 5, words.size());
            size_t dend = std::min(i + 15, words.size());

            std::string q, d;
            for (size_t j = i; j < qend; j++) {
                if (!q.empty()) q += " ";
                q += words[j];
            }
            for (size_t j = i; j < dend; j++) {
                if (!d.empty()) d += " ";
                d += words[j];
            }
            pairs.push_back({q, d});
        }
    }

    if (pairs.empty()) {
        std::cerr << "No training pairs generated.\n";
        return;
    }

    std::mt19937 rng(42);
    int64_t tok_max = std::min(max_len_, static_cast<int64_t>(64));

    int total_batches = static_cast<int>((pairs.size()) / batch_size);

    for (int epoch = 0; epoch < epochs; epoch++) {
        std::shuffle(pairs.begin(), pairs.end(), rng);

        double total_loss = 0.0;
        int steps = 0;

        for (size_t bi = 0; bi + batch_size <= pairs.size(); bi += batch_size) {
            std::vector<int64_t> q_ids_flat, d_ids_flat;
            std::vector<float> q_mask_flat, d_mask_flat;

            for (int b = 0; b < batch_size; b++) {
                auto [qi, qm] = tokenize(pairs[bi + b].query, tok_max);
                auto [di, dm] = tokenize(pairs[bi + b].doc, tok_max);
                q_ids_flat.insert(q_ids_flat.end(), qi.begin(), qi.end());
                q_mask_flat.insert(q_mask_flat.end(), qm.begin(), qm.end());
                d_ids_flat.insert(d_ids_flat.end(), di.begin(), di.end());
                d_mask_flat.insert(d_mask_flat.end(), dm.begin(), dm.end());
            }

            auto q_tokens = torch::from_blob(q_ids_flat.data(),
                {batch_size, tok_max}, torch::kInt64).clone();
            auto q_mask = torch::from_blob(q_mask_flat.data(),
                {batch_size, tok_max}, torch::kFloat32).clone();
            auto d_tokens = torch::from_blob(d_ids_flat.data(),
                {batch_size, tok_max}, torch::kInt64).clone();
            auto d_mask = torch::from_blob(d_mask_flat.data(),
                {batch_size, tok_max}, torch::kFloat32).clone();

            auto q_emb = model_->forward(q_tokens, q_mask);
            auto d_emb = model_->forward(d_tokens, d_mask);

            auto loss = contrastive_loss(q_emb, d_emb);

            optimizer.zero_grad();
            loss.backward();
            optimizer.step();

            total_loss += loss.item<double>();
            steps++;

            // Progress bar
            int bar_width = 30;
            int filled = (total_batches > 0) ? (steps * bar_width / total_batches) : bar_width;
            std::cerr << "\rEpoch " << (epoch + 1) << "/" << epochs << " [";
            for (int p = 0; p < bar_width; p++)
                std::cerr << (p < filled ? '=' : (p == filled ? '>' : ' '));
            std::cerr << "] " << steps << "/" << total_batches
                      << " loss=" << std::fixed << std::setprecision(4)
                      << (total_loss / steps) << std::flush;
        }

        std::cerr << "\rEpoch " << (epoch + 1) << "/" << epochs
                  << " loss=" << std::fixed << std::setprecision(4)
                  << (steps > 0 ? total_loss / steps : 0.0)
                  << std::string(40, ' ') << "\n";
    }

    model_->eval();
}

void EncoderTrainer::save(const std::string& path) {
    torch::save(model_, path);
}

void EncoderTrainer::load(const std::string& path) {
    torch::load(model_, path);
    model_->eval();
}

std::vector<float> EncoderTrainer::encode(const std::string& text) {
    torch::NoGradGuard no_grad;
    model_->eval();

    int64_t tok_max = std::min(max_len_, static_cast<int64_t>(64));
    auto [ids, mask] = tokenize(text, tok_max);

    auto tokens_t = torch::from_blob(ids.data(),
        {1, tok_max}, torch::kInt64).clone();
    auto mask_t = torch::from_blob(mask.data(),
        {1, tok_max}, torch::kFloat32).clone();

    auto emb = model_->forward(tokens_t, mask_t);  // [1, D]

    auto emb_acc = emb.accessor<float, 2>();
    std::vector<float> result(emb.size(1));
    for (int64_t i = 0; i < emb.size(1); i++)
        result[i] = emb_acc[0][i];

    return result;
}
