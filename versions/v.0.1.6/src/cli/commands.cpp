#include "commands.h"
#include "../tools/sandbox.h"
#include "../storage/segment_reader.h"
#include "../inverted/tokenizer.h"
#include "../inverted/bm25.h"
#include "../inverted/index_builder.h"
#include "../inverted/search_engine.h"
#include "../hybrid/hybrid_builder.h"
#include "../hybrid/hybrid_search.h"
#include "../pipeline/pipeline_builder.h"
#include "../profiling/profiler.h"
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
                  << "  moai ingest <path>\n"
                  << "  moai search <query>\n"
                  << "  moai build-hnsw\n"
                  << "  moai hybrid <query>\n"
                  << "  moai ask <query> [--json] [--brief] [--detailed] [--profile]\n"
#ifdef HAS_TORCH
                  << "  moai train-encoder [--epochs N] [--dim D] [--lr R]\n"
                  << "  moai train-qa [--epochs N]\n"
#endif
                  << "  moai run <cmd> [args...]\n";
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
            std::cerr << "Usage: moai ask \"query\" [--json] [--brief] [--detailed] [--profile]\n";
            return 1;
        }

        std::string query = argv[2];
        bool json = false;
        bool force_brief = false;
        bool force_detailed = false;
        bool force_profile = false;
        for (int i = 3; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--json") json = true;
            else if (arg == "--brief") force_brief = true;
            else if (arg == "--detailed") force_detailed = true;
            else if (arg == "--profile") force_profile = true;
        }
        std::string segdir = "../segments/seg_000001";
        std::string embeddir = "../embeddings";

        SegmentReader reader(segdir);
        if (force_profile) Profiler::instance().set_enabled(true);
        auto pipeline = PipelineBuilder::build(reader, segdir, embeddir);

        PipelineOptions opts;
        opts.json = json;
        opts.force_brief = force_brief;
        opts.force_detailed = force_detailed;

        auto result = pipeline.run(query, opts);
        auto& needs = result.needs;
        auto& composite = result.composite;

        // Filter: only show user-facing needs in output (not self-ask support)
        std::vector<size_t> user_indices;
        for (size_t i = 0; i < needs.size(); i++)
            if (!needs[i].is_support) user_indices.push_back(i);

        // 7. Output
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
                      << "  \"retrieval\": \"" << result.retriever_name << "\",\n"
                      << "  \"needs\": [\n";
            for (size_t ui = 0; ui < user_indices.size(); ui++) {
                size_t i = user_indices[ui];
                auto& n = needs[i];
                auto& a = composite.parts[i];
                std::cout << "    {\n"
                          << "      \"entity\": \"" << escaped(n.entity) << "\",\n"
                          << "      \"property\": \"" << property_str(n.property) << "\",\n"
                          << "      \"property_score\": " << std::fixed << std::setprecision(1) << n.property_score << ",\n"
                          << "      \"form\": \"" << answer_form_str(n.form) << "\",\n"
                          << "      \"scope\": \"" << scope_str(n.scope) << "\",\n"
                          << "      \"answer\": {\n"
                          << "        \"text\": \"" << escaped(a.text) << "\",\n"
                          << "        \"confidence\": " << std::fixed << std::setprecision(2) << a.confidence << ",\n"
                          << "        \"validated\": " << (a.validated ? "true" : "false") << ",\n"
                          << "        \"compression\": \"" << compression_str(a.compression) << "\"";
                if (!a.validation_note.empty())
                    std::cout << ",\n        \"note\": \"" << escaped(a.validation_note) << "\"";
                if (!a.prior_context.empty())
                    std::cout << ",\n        \"used_prior_context\": true";
                std::cout << "\n      },\n"
                          << "      \"sources\": [";
                for (size_t j = 0; j < a.sources.size(); j++) {
                    if (j > 0) std::cout << ", ";
                    std::cout << a.sources[j];
                }
                std::cout << "]\n"
                          << "    }";
                if (ui + 1 < user_indices.size()) std::cout << ",";
                std::cout << "\n";
            }
            std::cout << "  ],\n"
                      << "  \"confidence\": " << std::fixed << std::setprecision(2)
                      << composite.overall_confidence() << "\n}\n";
        } else {
            if (result.retriever_name != "bm25")
                std::cerr << "Retrieval: " << result.retriever_name << "\n";
            for (size_t ui = 0; ui < user_indices.size(); ui++) {
                size_t i = user_indices[ui];
                auto& n = needs[i];
                auto& a = composite.parts[i];
                std::cout << "[" << property_str(n.property) << " / "
                          << answer_form_str(n.form) << " / "
                          << scope_str(n.scope) << "] "
                          << "Entity: " << n.entity << "\n"
                          << "Confidence: " << std::fixed << std::setprecision(2)
                          << a.confidence
                          << (a.validated ? "" : " [UNVALIDATED]") << "\n";
                if (!a.validation_note.empty())
                    std::cout << "Note: " << a.validation_note << "\n";
                if (!a.prior_context.empty())
                    std::cout << "(uses prior context)\n";
                std::cout << "Sources: [";
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
            std::cerr << "Usage: moai hybrid \"query\" [--json]\n";
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
    if (cmd == "train-encoder") {
        std::string segdir = "../segments/seg_000001";
        std::string embeddir = "../embeddings";
        int epochs = 10;
        double lr = 3e-4;
        int64_t dim = 128;
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--epochs" && i + 1 < argc) epochs = std::stoi(argv[++i]);
            else if (arg == "--lr" && i + 1 < argc) lr = std::stod(argv[++i]);
            else if (arg == "--dim" && i + 1 < argc) dim = std::stoi(argv[++i]);
            else if (arg == "--segdir" && i + 1 < argc) segdir = argv[++i];
            else if (arg == "--embeddir" && i + 1 < argc) embeddir = argv[++i];
        }
        std::filesystem::create_directories(embeddir);
        std::cerr << "Loading segment from " << segdir << "...\n";
        SegmentReader reader(segdir);
        std::cerr << "Building vocabulary...\n";
        Vocabulary vocab;
        vocab.build_from_terms(reader.all_terms());
        vocab.save(embeddir + "/vocab.txt");
        std::cerr << "Vocab size: " << vocab.size() << "\n";
        std::cerr << "Initializing encoder (dim=" << dim << ")...\n";
        EncoderTrainer trainer(vocab, dim, /*heads=*/4, /*layers=*/2, /*max_len=*/256);
        std::cerr << "Training for " << epochs << " epochs...\n";
        trainer.train(reader, epochs, lr, /*batch_size=*/4);
        std::string model_path = embeddir + "/encoder.pt";
        trainer.save(model_path);
        std::cerr << "Model saved to " << model_path << "\n";
        auto emb = trainer.encode("what is a database");
        std::cerr << "Sample embedding dim=" << emb.size() << " [";
        for (size_t i = 0; i < std::min<size_t>(5, emb.size()); i++)
            std::cerr << emb[i] << (i + 1 < emb.size() ? ", " : "");
        std::cerr << "...]\n";
        std::cerr << "Done.\n";
        return 0;
    }

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
