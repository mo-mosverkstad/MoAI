#include "commands.h"
#include "../tools/sandbox.h"
#include "../storage/segment_reader.h"
#include "../inverted/tokenizer.h"
#include "../inverted/bm25.h"
#include "../inverted/index_builder.h"
#include "../inverted/search_engine.h"
#include "../hybrid/hybrid_builder.h"
#include "../hybrid/hybrid_search.h"
#include "../query/query_analyzer.h"
#include "../chunk/chunker.h"
#include "../answer/answer_synthesizer.h"
#include "../conversation/conversation_state.h"
#ifdef HAS_TORCH
#include "../encoder/encoder_trainer.h"
#include "../query/neural_query_analyzer.h"
#endif
#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <memory>


int run_cli(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  mysearch ingest <path>\n"
                  << "  mysearch search <query>\n"
                  << "  mysearch build-hnsw\n"
                  << "  mysearch hybrid <query>\n"
                  << "  mysearch ask <query>\n"
#ifdef HAS_TORCH
                  << "  mysearch train-encoder\n"
                  << "  mysearch train-qa\n"
#endif
                  << "  mysearch run <cmd> [args...]\n";
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "ingest") {
        if (argc < 3) { std::cerr << "Error: missing path\n"; return 1; }
        std::string path = argv[2];
        std::string segdir = "../segments/seg_000001";
        IndexBuilder builder(segdir);
        builder.ingest_directory(path);
        builder.finalize();
        std::cout << "Ingested " << path << " -> " << segdir << "\n";
        return 0;
    }

    if (cmd == "search") {
        if (argc < 3) { std::cerr << "Missing query\n"; return 1; }
        std::string query = argv[2];
        bool json = (argc >= 4 && std::string(argv[3]) == "--json");
        SegmentReader reader("../segments/seg_000001");
        SearchEngine engine(reader);
        engine.set_json_output(json);
        auto results = engine.search(query, 10);
        if (!json) {
            for (auto& [doc, score] : results)
                std::cout << "doc=" << doc << " score=" << score
                          << " length=" << reader.doc_length(doc) << "\n";
        }
        return 0;
    }

    if (cmd == "build-hnsw") {
        HybridBuilder::bootstrap("../segments/seg_000001", "../embeddings");
        return 0;
    }

    // === Question-Answering pipeline (InformationNeed-based) ===
    // Pipeline: Query → Analyze → InformationNeeds[] → Hybrid Retrieve → Chunk Select → Synthesize → Answer
    if (cmd == "ask") {
        if (argc < 3) {
            std::cerr << "Usage: mysearch ask \"query\" [--json]\n";
            return 1;
        }

        std::string query = argv[2];
        bool json = (argc >= 4 && std::string(argv[3]) == "--json");
        std::string segdir = "../segments/seg_000001";
        std::string embeddir = "../embeddings";

        SegmentReader reader(segdir);

        // 1. Analyze query → multiple InformationNeeds
        QueryAnalyzer analyzer;
        analyzer.load_neural(embeddir + "/qa_model.pt",
                             embeddir + "/vocab.txt");
        if (analyzer.is_neural())
            std::cerr << "Using neural query analyzer\n";

        auto needs = analyzer.analyze(query);

        // Apply conversation memory (resolve ellipsis / follow-ups)
        // Persisted to file so it works across separate process invocations
        std::string conv_path = segdir + "/../.conversation";
        ConversationState conversation;
        conversation.load(conv_path);
        conversation.apply(needs);

        // 2. Set up retrieval: try hybrid (BM25+HNSW), fall back to BM25-only
        BM25 bm25(reader);
        Chunker chunker;
        AnswerSynthesizer synthesizer;
        CompositeAnswer composite;

        // Try to load embedding infrastructure for hybrid retrieval
        bool use_hybrid = false;
        Vocabulary vocab;
        std::unique_ptr<EmbeddingModel> emb_model;
        std::unique_ptr<HNSWIndex> hnsw;

#ifdef HAS_TORCH
        // Prefer neural encoder if available
        std::string encoder_path = embeddir + "/encoder.pt";
        if (std::filesystem::exists(encoder_path) &&
            std::filesystem::exists(embeddir + "/vocab.txt")) {
            vocab.load(embeddir + "/vocab.txt");
            EncoderTrainer encoder(vocab, 128, 4, 2, 256);
            encoder.load(encoder_path);

            hnsw = std::make_unique<HNSWIndex>(
                static_cast<uint32_t>(encoder.dim()), 16, 200, 100);
            uint32_t N = reader.doc_count();
            for (uint32_t d = 1; d <= N; d++)
                hnsw->add_point(encoder.encode(reader.get_document_text(d)));

            use_hybrid = true;
            std::cerr << "Using hybrid retrieval (BM25 + neural HNSW)\n";
        } else
#endif
        if (std::filesystem::exists(embeddir + "/model.bin") &&
            std::filesystem::exists(embeddir + "/vocab.txt")) {
            vocab.load(embeddir + "/vocab.txt");
            emb_model = std::make_unique<EmbeddingModel>();
            emb_model->load(embeddir + "/model.bin", vocab.size());
            hnsw = std::make_unique<HNSWIndex>(
                static_cast<uint32_t>(emb_model->dim()), 16, 200, 100);
            // Build HNSW on the fly
            Tokenizer tok;
            uint32_t N = reader.doc_count();
            for (uint32_t d = 1; d <= N; d++) {
                std::string text = reader.get_document_text(d);
                auto tokens = tok.tokenize(text);
                std::vector<float> bow(vocab.size(), 0.0f);
                for (auto& t : tokens) {
                    int vid = vocab.id(t);
                    if (vid >= 0) bow[vid] += 1.0f;
                }
                hnsw->add_point(emb_model->embed(bow));
            }
            use_hybrid = true;
            std::cerr << "Using hybrid retrieval (BM25 + HNSW)\n";
        }

        // 3. For each need: retrieve → chunk select → synthesize
        for (auto& need : needs) {
            // --- Retrieval ---
            auto bm25_results = bm25.search(need.keywords, 10);

            // Merge with ANN results if hybrid is available
            std::unordered_map<uint32_t, double> doc_scores;
            for (auto& [docId, score] : bm25_results)
                doc_scores[docId] = score;

            if (use_hybrid && hnsw) {
                std::vector<float> q_emb;
#ifdef HAS_TORCH
                // Prefer neural encoder for query embedding
                std::string enc_path = embeddir + "/encoder.pt";
                if (std::filesystem::exists(enc_path)) {
                    EncoderTrainer encoder(vocab, 128, 4, 2, 256);
                    encoder.load(enc_path);
                    // Encode the keywords as a query sentence
                    std::string query_text;
                    for (auto& kw : need.keywords) {
                        if (!query_text.empty()) query_text += " ";
                        query_text += kw;
                    }
                    q_emb = encoder.encode(query_text);
                } else
#endif
                if (emb_model) {
                    Tokenizer tok;
                    std::vector<float> bow(vocab.size(), 0.0f);
                    for (auto& kw : need.keywords) {
                        int vid = vocab.id(kw);
                        if (vid >= 0) bow[vid] += 1.0f;
                    }
                    q_emb = emb_model->embed(bow);
                }

                if (!q_emb.empty()) {
                    auto ann_ids = hnsw->search(q_emb, 10);

                // Normalize BM25
                double max_bm25 = 0.0;
                for (auto& [id, sc] : doc_scores)
                    if (sc > max_bm25) max_bm25 = sc;

                // Fuse ANN scores — only boost docs that BM25 also found
                for (uint32_t id : ann_ids) {
                    uint32_t doc = id + 1;
                    if (!doc_scores.count(doc)) continue; // skip ANN-only docs
                    const auto& vec = hnsw->get_vector(id);
                    float dot = 0.0f;
                    for (size_t i = 0; i < q_emb.size(); i++)
                        dot += q_emb[i] * vec[i];
                    double ann_norm = (static_cast<double>(dot) + 1.0) * 0.5;
                    double bm25_norm = (max_bm25 > 0) ? doc_scores[doc] / max_bm25 : 0.0;
                    doc_scores[doc] = 0.7 * bm25_norm + 0.3 * ann_norm;
                }

                    // Re-normalize BM25-only docs
                    for (auto& [doc, sc] : doc_scores) {
                        if (max_bm25 > 0) {
                            bool was_ann = false;
                            for (uint32_t id : ann_ids)
                                if (id + 1 == doc) { was_ann = true; break; }
                            if (!was_ann) sc = 0.7 * (sc / max_bm25);
                        }
                    }
                }
            }

            // Sort docs by fused score
            std::vector<std::pair<uint32_t, double>> ranked_docs(
                doc_scores.begin(), doc_scores.end());
            std::sort(ranked_docs.begin(), ranked_docs.end(),
                      [](auto& a, auto& b) { return a.second > b.second; });
            if (ranked_docs.size() > 10) ranked_docs.resize(10);

            // --- Chunk & Select ---
            std::vector<Evidence> evidence;
            for (auto& [docId, score] : ranked_docs) {
                std::string text = reader.get_document_text(docId);
                if (text.empty()) continue;
                auto all_chunks = chunker.chunk_document(docId, text);
                auto selected = Chunker::select_chunks(all_chunks, need.property, need.keywords, 5);
                for (auto& c : selected)
                    evidence.push_back({c.docId, c.type, c.text, score});
            }

            if (evidence.size() > 15) evidence.resize(15);

            // --- Synthesize ---
            composite.parts.push_back(synthesizer.synthesize(need, evidence));

            // Update conversation memory
            conversation.update(need);
        }

        // 4. Save conversation memory for next invocation
        if (!needs.empty())
            conversation.save(conv_path);

        // 5. Output
        if (json) {
            auto escaped = [](const std::string& s) {
                std::string r;
                for (char c : s) {
                    if (c == '"') r += "\\\"";
                    else if (c == '\\') r += "\\\\";
                    else if (c == '\n') r += "\\n";
                    else if (c == '\r') continue;
                    else r += c;
                }
                return r;
            };
            std::cout << "{\n  \"query\": \"" << escaped(query) << "\",\n"
                      << "  \"retrieval\": \"" << (use_hybrid ? "hybrid" : "bm25") << "\",\n"
                      << "  \"needs\": [\n";
            for (size_t i = 0; i < needs.size(); i++) {
                auto& n = needs[i];
                auto& a = composite.parts[i];
                std::cout << "    {\n"
                          << "      \"entity\": \"" << escaped(n.entity) << "\",\n"
                          << "      \"property\": \"" << property_str(n.property) << "\",\n"
                          << "      \"property_score\": " << std::fixed << std::setprecision(1) << n.property_score << ",\n"
                          << "      \"form\": \"" << answer_form_str(n.form) << "\",\n"
                          << "      \"answer\": {\n"
                          << "        \"text\": \"" << escaped(a.text) << "\",\n"
                          << "        \"confidence\": " << std::fixed << std::setprecision(2) << a.confidence << "\n"
                          << "      },\n"
                          << "      \"sources\": [";
                for (size_t j = 0; j < a.sources.size(); j++) {
                    if (j > 0) std::cout << ", ";
                    std::cout << a.sources[j];
                }
                std::cout << "]\n"
                          << "    }";
                if (i + 1 < needs.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "  ],\n"
                      << "  \"confidence\": " << std::fixed << std::setprecision(2)
                      << composite.overall_confidence() << "\n}\n";
        } else {
            if (use_hybrid)
                std::cerr << "Retrieval: hybrid (BM25 + HNSW)\n";
            for (size_t i = 0; i < needs.size(); i++) {
                auto& n = needs[i];
                auto& a = composite.parts[i];
                std::cout << "[" << property_str(n.property) << " / "
                          << answer_form_str(n.form) << "] "
                          << "Entity: " << n.entity << "\n"
                          << "Confidence: " << std::fixed << std::setprecision(2)
                          << a.confidence << "\n"
                          << "Sources: [";
                for (size_t j = 0; j < a.sources.size(); j++) {
                    if (j > 0) std::cout << ", ";
                    std::cout << a.sources[j];
                }
                std::cout << "]\n"
                          << a.text << "\n\n";
            }
        }
        return 0;
    }

    // === Existing hybrid command (kept for backward compatibility) ===
    if (cmd == "hybrid") {
        if (argc < 3) {
            std::cerr << "Usage: mysearch hybrid \"query\" [--json]\n";
            return 1;
        }

        std::string query = argv[2];
        bool json = (argc >= 4 && std::string(argv[3]) == "--json");
        std::string segdir = "../segments/seg_000001";
        std::string embeddir = "../embeddings";

        SegmentReader reader(segdir);
        Vocabulary vocab;
        vocab.load(embeddir + "/vocab.txt");

#ifdef HAS_TORCH
        std::string encoder_path = embeddir + "/encoder.pt";
        if (std::filesystem::exists(encoder_path)) {
            std::cerr << "Using neural encoder...\n";
            EncoderTrainer encoder(vocab, 128, 4, 2, 256);
            encoder.load(encoder_path);

            HNSWIndex hnsw(static_cast<uint32_t>(encoder.dim()), 16, 200, 100);
            uint32_t N = reader.doc_count();
            for (uint32_t d = 1; d <= N; d++)
                hnsw.add_point(encoder.encode(reader.get_document_text(d)));

            auto q_emb = encoder.encode(query);
            BM25 bm25(reader);
            Tokenizer tok;
            auto bm25_results = bm25.search(tok.tokenize(query), 20);
            auto ann_ids = hnsw.search(q_emb, 20);

            std::unordered_map<uint32_t, HybridSearch::Result> merged;
            for (auto& [doc, score] : bm25_results) {
                merged[doc].doc = doc;
                merged[doc].bm25 = score;
            }
            double max_bm25 = 0.0;
            for (auto& [doc, r] : merged)
                if (r.bm25 > max_bm25) max_bm25 = r.bm25;

            for (uint32_t id : ann_ids) {
                uint32_t doc = id + 1;
                const auto& vec = hnsw.get_vector(id);
                float dot = 0.0f;
                for (size_t i = 0; i < q_emb.size(); i++)
                    dot += q_emb[i] * vec[i];
                merged[doc].doc = doc;
                merged[doc].ann = static_cast<double>(dot);
            }

            std::vector<HybridSearch::Result> results;
            for (auto& [id, r] : merged) {
                double bm25_n = (max_bm25 > 0) ? r.bm25 / max_bm25 : 0.0;
                double ann_n = (r.ann + 1.0) * 0.5;
                r.score = 0.7 * bm25_n + 0.3 * ann_n;
                results.push_back(r);
            }
            std::sort(results.begin(), results.end(),
                      [](auto& a, auto& b) { return a.score > b.score; });

            EmbeddingModel dummy_model;
            HNSWIndex dummy_hnsw(1);
            HybridSearch hs(reader, dummy_model, vocab, dummy_hnsw);

            if (json) {
                std::string summary = hs.summarize_results(results, query);
                std::cout << "{\n  \"results\": [\n";
                for (size_t i = 0; i < results.size(); i++) {
                    auto& r = results[i];
                    std::cout << "    { \"doc\": " << r.doc
                              << ", \"score\": " << r.score
                              << ", \"bm25\": " << r.bm25
                              << ", \"ann\": " << r.ann << " }";
                    if (i + 1 < results.size()) std::cout << ",";
                    std::cout << "\n";
                }
                std::string escaped;
                for (char c : summary) {
                    if (c == '"') escaped += "\\\"";
                    else if (c == '\\') escaped += "\\\\";
                    else if (c == '\n') escaped += "\\n";
                    else if (c == '\r') continue;
                    else escaped += c;
                }
                std::cout << "  ],\n  \"summary\": \"" << escaped << "\"\n}\n";
            } else {
                for (auto& r : results)
                    std::cout << "doc=" << r.doc << " score=" << r.score
                              << " bm25=" << r.bm25 << " ann=" << r.ann << "\n";
                std::cout << "\n=== SUMMARY ===\n"
                          << hs.summarize_results(results, query) << "\n";
            }
            return 0;
        }
#endif
        EmbeddingModel model;
        model.load(embeddir + "/model.bin", vocab.size());
        HNSWIndex hnsw(static_cast<uint32_t>(model.dim()), 16, 200, 100);
        HybridBuilder builder(reader, model, vocab, hnsw);
        builder.build();
        HybridSearch hs(reader, model, vocab, hnsw);
        auto results = hs.search(query, 20, 20, json);
        if (!json) {
            for (auto& r : results)
                std::cout << "doc=" << r.doc << " score=" << r.score
                          << " bm25=" << r.bm25 << " ann=" << r.ann << "\n";
            std::cout << "\n=== SUMMARY ===\n"
                      << hs.summarize_results(results, query) << "\n";
        }
        return 0;
    }

#ifdef HAS_TORCH
    if (cmd == "train-qa") {
        std::string segdir = "../segments/seg_000001";
        std::string embeddir = "../embeddings";
        int epochs = 30;
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--epochs" && i + 1 < argc) epochs = std::stoi(argv[++i]);
            else if (arg == "--segdir" && i + 1 < argc) segdir = argv[++i];
            else if (arg == "--embeddir" && i + 1 < argc) embeddir = argv[++i];
        }

        SegmentReader reader(segdir);
        Vocabulary vocab;
        std::string vocab_path = embeddir + "/vocab.txt";
        if (!vocab.load(vocab_path)) {
            std::cerr << "Building vocabulary...\n";
            vocab.build_from_terms(reader.all_terms());
            std::filesystem::create_directories(embeddir);
            vocab.save(vocab_path);
        }
        std::cerr << "Vocab size: " << vocab.size() << "\n";

        std::vector<std::string> doc_texts;
        for (uint32_t d = 1; d <= reader.doc_count(); d++)
            doc_texts.push_back(reader.get_document_text(d));

        auto samples = NeuralQueryAnalyzer::generate_training_data(doc_texts);
        std::cerr << "Generated " << samples.size() << " training samples\n";

        NeuralQueryAnalyzer nqa(vocab, 128, 64);
        nqa.train(samples, epochs, 1e-3, 8);

        std::string model_path = embeddir + "/qa_model.pt";
        nqa.save(model_path);
        std::cerr << "Query analyzer model saved to " << model_path << "\n";

        // Use legacy interface for test output
        auto test = [&](const char* q) {
            auto qa = nqa.analyze(q);
            static const char* at_names[] = {
                "LOCATION","DEFINITION","PERSON","TEMPORAL","PROCEDURE","COMPARISON","SUMMARY"
            };
            int idx = static_cast<int>(qa.answerType);
            std::cerr << "  \"" << q << "\" -> "
                      << (idx >= 0 && idx < 7 ? at_names[idx] : "UNKNOWN")
                      << " entity=\"" << qa.mainEntity << "\"\n";
        };
        std::cerr << "Test predictions:\n";
        test("where is stockholm");
        test("what is a database");
        test("who is alan turing");
        test("when was the transistor invented");
        test("how does TCP work");
        return 0;
    }
#endif

    if (cmd == "run") {
        if (argc < 3) { std::cerr << "Error: missing command\n"; return 1; }
        std::vector<std::string> args;
        for (int i = 2; i < argc; i++) args.emplace_back(argv[i]);
        Sandbox sb;
        sb.set_allowed_paths({"/usr/bin", "/bin"});
        int ret = sb.exec(args);
        std::cout << "[Sandbox] exit code = " << ret << "\n";
        return ret;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 1;
}
