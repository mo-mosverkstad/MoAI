#include "hybrid_builder.h"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <unordered_map>

static void print_progress(const std::string& label, uint32_t current, uint32_t total) {
    int width = 40;
    float ratio = (total > 0) ? static_cast<float>(current) / total : 1.0f;
    int filled = static_cast<int>(ratio * width);

    std::cerr << "\r" << label << " [";
    for (int i = 0; i < width; i++)
        std::cerr << (i < filled ? '#' : '.');
    std::cerr << "] " << current << "/" << total
              << " (" << std::fixed << std::setprecision(0) << (ratio * 100) << "%)"
              << std::flush;
}

HybridBuilder::HybridBuilder(SegmentReader& reader,
                             EmbeddingModel& model,
                             Vocabulary& vocab,
                             HNSWIndex& hnsw)
    : reader_(reader), model_(model), vocab_(vocab), hnsw_(hnsw)
{}

void HybridBuilder::build() {
    uint32_t N = reader_.doc_count();
    if (N == 0) return;

    // Build all BoW vectors in a single pass over terms
    std::vector<std::vector<float>> bows(N, std::vector<float>(vocab_.size(), 0.0f));

    auto terms = reader_.all_terms();
    uint32_t term_count = static_cast<uint32_t>(terms.size());

    for (uint32_t ti = 0; ti < term_count; ti++) {
        if (ti % 50 == 0 || ti + 1 == term_count)
            print_progress("Building BoW", ti + 1, term_count);

        int vid = vocab_.id(terms[ti]);
        if (vid < 0) continue;

        auto postings = reader_.get_postings(terms[ti]);
        for (auto& p : postings) {
            if (p.doc >= 1 && p.doc <= N)
                bows[p.doc - 1][vid] += static_cast<float>(p.tf);
        }
    }
    std::cerr << "\n";

    // Embed each doc and insert into HNSW
    for (uint32_t i = 0; i < N; i++) {
        print_progress("Embedding", i + 1, N);
        auto emb = model_.embed(bows[i]);
        hnsw_.add_point(emb);
    }
    std::cerr << "\n";
}

std::vector<float> HybridBuilder::doc_to_bow(DocID doc) {
    std::vector<float> bow(vocab_.size(), 0.0f);
    for (auto& term : reader_.all_terms()) {
        int vid = vocab_.id(term);
        if (vid < 0) continue;
        auto postings = reader_.get_postings(term);
        for (auto& p : postings) {
            if (p.doc == doc)
                bow[vid] += static_cast<float>(p.tf);
        }
    }
    return bow;
}

void HybridBuilder::bootstrap(const std::string& segdir,
                               const std::string& embed_dir,
                               size_t hidden_dim,
                               size_t output_dim) {
    std::filesystem::create_directories(embed_dir);

    std::cerr << "Loading segment...\n";
    SegmentReader reader(segdir);

    std::cerr << "Building vocabulary...\n";
    Vocabulary vocab;
    vocab.build_from_terms(reader.all_terms());
    vocab.save(embed_dir + "/vocab.txt");

    std::cerr << "Initializing model (vocab=" << vocab.size()
              << " hidden=" << hidden_dim
              << " dim=" << output_dim << ")...\n";
    EmbeddingModel model;
    model.init_random(vocab.size(), hidden_dim, output_dim);
    model.save(embed_dir + "/model.bin");

    HNSWIndex hnsw(static_cast<uint32_t>(output_dim), 16, 200, 100);

    HybridBuilder builder(reader, model, vocab, hnsw);
    builder.build();

    std::cout << "Bootstrap complete: vocab=" << vocab.size()
              << " docs=" << reader.doc_count()
              << " hnsw_size=" << hnsw.size()
              << " embed_dim=" << output_dim << "\n";
}
