#ifdef HAS_TORCH
#include "neural_query_analyzer.h"
#include "../inverted/tokenizer.h"
#include "../common/rules_loader.h"
#include "../common/vocab_loader.h"
#include <algorithm>
#include <random>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <regex>
#include <cctype>

// ── QueryClassifier model ──

QueryClassifierImpl::QueryClassifierImpl(
    int64_t vocab_size, int64_t dim, int64_t heads,
    int64_t layers, int64_t max_len)
    : dim_(dim)
{
    tok_emb_ = register_module("tok_emb",
        torch::nn::Embedding(vocab_size, dim));

    auto pe = torch::zeros({max_len, dim});
    for (int64_t pos = 0; pos < max_len; ++pos)
        for (int64_t i = 0; i < dim; i += 2) {
            double div = std::pow(10000.0, static_cast<double>(i) / dim);
            pe[pos][i] = std::sin(pos / div);
            if (i + 1 < dim) pe[pos][i + 1] = std::cos(pos / div);
        }
    pe_ = register_buffer("pe", pe);

    auto layer_opts = torch::nn::TransformerEncoderLayerOptions(dim, heads)
        .dim_feedforward(4 * dim).dropout(0.1).activation(torch::kGELU);
    token_encoder_ = register_module("token_encoder",
        torch::nn::TransformerEncoder(
            torch::nn::TransformerEncoderOptions(
                torch::nn::TransformerEncoderLayer(layer_opts), layers)));

    intent_head_ = register_module("intent_head", torch::nn::Linear(dim, 5));
    answer_head_ = register_module("answer_head", torch::nn::Linear(dim, 7));
    entity_head_ = register_module("entity_head", torch::nn::Linear(dim, 3));
}

QueryClassifierImpl::Output QueryClassifierImpl::forward(
    torch::Tensor tokens, torch::Tensor mask)
{
    auto x = tok_emb_(tokens);  // [B, T, D]
    auto seq_len = x.size(1);
    x = x + pe_.index({torch::indexing::Slice(torch::indexing::None, seq_len)});

    auto xt = x.transpose(0, 1);  // [T, B, D]
    auto key_pad = (mask == 0);
    xt = token_encoder_->forward(xt, {}, key_pad);
    auto token_out = xt.transpose(0, 1);  // [B, T, D]

    // Mean-pool for sentence-level heads
    auto mask_f = mask.unsqueeze(-1).to(token_out.dtype());
    auto pooled = (token_out * mask_f).sum(1) / mask_f.sum(1).clamp_min(1e-6);

    Output out;
    out.intent_logits = intent_head_(pooled);
    out.answer_type_logits = answer_head_(pooled);
    out.entity_logits = entity_head_(token_out);  // [B, T, 3]
    return out;
}

// ── NeuralQueryAnalyzer ──

NeuralQueryAnalyzer::NeuralQueryAnalyzer(
    Vocabulary& vocab, int64_t dim, int64_t max_len)
    : vocab_(vocab), max_len_(max_len)
{
    model_ = QueryClassifier(
        static_cast<int64_t>(vocab.size()) + 1, dim, 4, 2, max_len);
}

std::pair<std::vector<int64_t>, std::vector<float>>
NeuralQueryAnalyzer::tokenize(const std::string& text) const {
    Tokenizer tok;
    auto words = tok.tokenize(text);
    std::vector<int64_t> ids;
    std::vector<float> mask;
    int64_t unk = static_cast<int64_t>(vocab_.size());
    for (auto& w : words) {
        if (static_cast<int64_t>(ids.size()) >= max_len_) break;
        int vid = vocab_.id(w);
        ids.push_back(vid >= 0 ? vid : unk);
        mask.push_back(1.0f);
    }
    while (static_cast<int64_t>(ids.size()) < max_len_) {
        ids.push_back(0);
        mask.push_back(0.0f);
    }
    return {ids, mask};
}

// ── Synthetic training data generation ──

static std::string to_lower(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
    return r;
}

// Extract noun-phrase-like entities from text
static std::vector<std::string> extract_entities(const std::string& text) {
    std::vector<std::string> entities;
    // Capitalized multi-word sequences
    std::regex cap_re("\\b([A-Z][a-z]+(?:\\s+[A-Z][a-z]+)*)\\b");
    auto begin = std::sregex_iterator(text.begin(), text.end(), cap_re);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it)
        entities.push_back((*it)[1].str());

    // Also grab quoted terms and technical terms
    std::regex tech_re("\\b([A-Z]{2,}(?:\\s+[A-Z]{2,})*)\\b");
    begin = std::sregex_iterator(text.begin(), text.end(), tech_re);
    for (auto it = begin; it != end; ++it)
        entities.push_back((*it)[1].str());

    // Deduplicate
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    return entities;
}

std::vector<QuerySample> NeuralQueryAnalyzer::generate_training_data(
    const std::vector<std::string>& doc_texts)
{
    std::vector<QuerySample> samples;
    std::mt19937 rng(42);

    // Load templates from config
    auto& loaded = PlanningRules::get().query_templates;

    for (auto& doc : doc_texts) {
        auto entities = extract_entities(doc);

        // Only use proper entities (capitalized phrases, acronyms)
        // Skip generic words — they produce nonsense training data
        std::sort(entities.begin(), entities.end());
        entities.erase(std::unique(entities.begin(), entities.end()),
                       entities.end());

        // Limit entities per document to keep training set manageable
        const size_t max_entities_per_doc = 20;
        if (entities.size() > max_entities_per_doc) {
            std::shuffle(entities.begin(), entities.end(), rng);
            entities.resize(max_entities_per_doc);
        }

        for (auto& entity : entities) {
            if (entity.size() < 3 || entity.size() > 40) continue;
            std::string entity_lower = to_lower(entity);

            // Generate queries from templates
            for (auto& tmpl : loaded) {
                QuerySample s;
                s.text = tmpl.prefix + entity_lower + tmpl.suffix;
                s.intent = tmpl.intent;
                s.answer_type = tmpl.answer_type;
                s.entity = entity_lower;
                samples.push_back(s);
            }
        }
    }

    // Shuffle
    std::shuffle(samples.begin(), samples.end(), rng);
    return samples;
}

// ── Training ──

void NeuralQueryAnalyzer::train(
    const std::vector<QuerySample>& samples,
    int epochs, double lr, int batch_size,
    int start_epoch, const std::string& checkpoint_path)
{
    if (samples.empty()) {
        std::cerr << "No training samples.\n";
        return;
    }

    model_->train();
    torch::optim::AdamW optimizer(
        model_->parameters(),
        torch::optim::AdamWOptions(lr).weight_decay(0.01));

    // Precompute tokenized samples with BIO entity tags
    struct PreparedSample {
        std::vector<int64_t> token_ids;
        std::vector<float> mask;
        int64_t intent_label;
        int64_t answer_label;
        std::vector<int64_t> entity_tags;  // 0=O, 1=B, 2=I
    };

    Tokenizer word_tok;
    std::vector<PreparedSample> prepared;
    prepared.reserve(samples.size());

    for (auto& s : samples) {
        PreparedSample ps;
        auto [ids, msk] = tokenize(s.text);
        ps.token_ids = ids;
        ps.mask = msk;
        ps.intent_label = static_cast<int64_t>(s.intent);
        ps.answer_label = static_cast<int64_t>(s.answer_type);

        // Generate BIO tags
        auto query_words = word_tok.tokenize(s.text);
        auto entity_words = word_tok.tokenize(s.entity);
        ps.entity_tags.resize(max_len_, 0);  // all O

        if (!entity_words.empty()) {
            for (size_t i = 0; i + entity_words.size() <= query_words.size()
                 && static_cast<int64_t>(i) < max_len_; i++) {
                bool match = true;
                for (size_t j = 0; j < entity_words.size(); j++) {
                    if (i + j >= query_words.size() ||
                        query_words[i + j] != entity_words[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    if (static_cast<int64_t>(i) < max_len_)
                        ps.entity_tags[i] = 1;  // B
                    for (size_t j = 1; j < entity_words.size(); j++)
                        if (static_cast<int64_t>(i + j) < max_len_)
                            ps.entity_tags[i + j] = 2;  // I
                    break;
                }
            }
        }
        prepared.push_back(std::move(ps));
    }

    std::mt19937 rng(42);
    int total_batches = static_cast<int>(prepared.size() / batch_size);

    for (int epoch = start_epoch; epoch < epochs; epoch++) {
        std::vector<size_t> indices(prepared.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::shuffle(indices.begin(), indices.end(), rng);

        double total_loss = 0.0;
        int steps = 0;

        for (size_t bi = 0; bi + batch_size <= indices.size();
             bi += batch_size) {
            std::vector<int64_t> tok_flat, intent_labels, answer_labels, entity_flat;
            std::vector<float> mask_flat;

            for (int b = 0; b < batch_size; b++) {
                auto& ps = prepared[indices[bi + b]];
                tok_flat.insert(tok_flat.end(),
                    ps.token_ids.begin(), ps.token_ids.end());
                mask_flat.insert(mask_flat.end(),
                    ps.mask.begin(), ps.mask.end());
                intent_labels.push_back(ps.intent_label);
                answer_labels.push_back(ps.answer_label);
                entity_flat.insert(entity_flat.end(),
                    ps.entity_tags.begin(), ps.entity_tags.end());
            }

            auto tokens_t = torch::from_blob(tok_flat.data(),
                {batch_size, max_len_}, torch::kInt64).clone();
            auto mask_t = torch::from_blob(mask_flat.data(),
                {batch_size, max_len_}, torch::kFloat32).clone();
            auto intent_t = torch::from_blob(intent_labels.data(),
                {batch_size}, torch::kInt64).clone();
            auto answer_t = torch::from_blob(answer_labels.data(),
                {batch_size}, torch::kInt64).clone();
            auto entity_t = torch::from_blob(entity_flat.data(),
                {batch_size, max_len_}, torch::kInt64).clone();

            auto out = model_->forward(tokens_t, mask_t);

            auto loss_intent = torch::nn::functional::cross_entropy(
                out.intent_logits, intent_t);
            auto loss_answer = torch::nn::functional::cross_entropy(
                out.answer_type_logits, answer_t);

            // Entity loss: only on valid (non-padded) tokens
            auto entity_logits_flat = out.entity_logits.reshape({-1, 3});
            auto entity_labels_flat = entity_t.reshape({-1});
            auto entity_mask_flat = mask_t.reshape({-1});
            auto entity_loss_all = torch::nn::functional::cross_entropy(
                entity_logits_flat, entity_labels_flat,
                torch::nn::functional::CrossEntropyFuncOptions().reduction(torch::kNone));
            auto loss_entity = (entity_loss_all * entity_mask_flat).sum()
                / entity_mask_flat.sum().clamp_min(1.0);

            auto loss = loss_intent + loss_answer + 0.5 * loss_entity;

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

        // Save checkpoint after each epoch for resume capability
        if (!checkpoint_path.empty()) {
            torch::save(model_, checkpoint_path);
            std::string epoch_file = checkpoint_path.substr(0, checkpoint_path.rfind(".")) + "_epoch.txt";
            std::ofstream ef(epoch_file);
            ef << (epoch + 1);
        }
    }

    model_->eval();
}

// ── Inference ──

std::string NeuralQueryAnalyzer::extract_entity_from_tags(
    const std::string& text,
    const std::vector<int64_t>& tag_ids) const
{
    Tokenizer tok;
    auto words = tok.tokenize(text);

    std::string entity;
    bool in_entity = false;
    for (size_t i = 0; i < words.size()
         && static_cast<int64_t>(i) < max_len_; i++) {
        int64_t tag = tag_ids[i];
        if (tag == 1) {  // B
            entity = words[i];
            in_entity = true;
        } else if (tag == 2 && in_entity) {  // I
            entity += " " + words[i];
        } else {
            if (in_entity) break;
        }
    }

    // Fallback: if no entity tagged, use longest non-stop keyword
    if (entity.empty()) {
        static const auto sw = []() {
            auto m = VocabLoader::load("../config/vocabularies/language.conf");
            auto& words = VocabLoader::get(m, "STOP_WORDS");
            return std::unordered_set<std::string>(words.begin(), words.end());
        }();
        for (auto& w : words) {
            if (sw.count(w) == 0 && w.size() > entity.size())
                entity = w;
        }
    }
    return entity;
}

QueryAnalysis NeuralQueryAnalyzer::analyze(const std::string& query) const {
    torch::NoGradGuard no_grad;

    auto [ids, mask] = tokenize(query);
    auto tokens_t = torch::from_blob(ids.data(),
        {1, max_len_}, torch::kInt64).clone();
    auto mask_t = torch::from_blob(mask.data(),
        {1, max_len_}, torch::kFloat32).clone();

    auto out = model_->forward(tokens_t, mask_t);

    auto intent_idx = out.intent_logits.argmax(1).item<int64_t>();
    auto answer_idx = out.answer_type_logits.argmax(1).item<int64_t>();
    auto entity_tags = out.entity_logits.squeeze(0).argmax(1);  // [T]

    std::vector<int64_t> tag_vec(max_len_);
    auto acc = entity_tags.accessor<int64_t, 1>();
    for (int64_t i = 0; i < max_len_; i++) tag_vec[i] = acc[i];

    QueryAnalysis qa;
    qa.intent = static_cast<QueryIntent>(
        std::clamp(intent_idx, int64_t(0), int64_t(4)));
    qa.answerType = static_cast<AnswerType>(
        std::clamp(answer_idx, int64_t(0), int64_t(6)));
    qa.mainEntity = extract_entity_from_tags(query, tag_vec);

    // Keywords: all non-stop words
    Tokenizer tok;
    static const auto sw = []() {
        auto m = VocabLoader::load("../config/vocabularies/language.conf");
        auto& words = VocabLoader::get(m, "STOP_WORDS");
        return std::unordered_set<std::string>(words.begin(), words.end());
    }();
    for (auto& w : tok.tokenize(query)) {
        std::string lower = to_lower(w);
        if (sw.count(lower) == 0 && lower.size() > 1)
            qa.keywords.push_back(lower);
    }

    return qa;
}

void NeuralQueryAnalyzer::save(const std::string& path) {
    torch::save(model_, path);
}

void NeuralQueryAnalyzer::load(const std::string& path) {
    torch::load(model_, path);
    model_->eval();
}

#endif // HAS_TORCH
