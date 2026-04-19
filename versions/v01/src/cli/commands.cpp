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

static const char* answer_type_str(AnswerType t) {
    switch (t) {
        case AnswerType::LOCATION:       return "LOCATION";
        case AnswerType::DEFINITION:     return "DEFINITION";
        case AnswerType::PERSON_PROFILE: return "PERSON";
        case AnswerType::TEMPORAL:       return "TEMPORAL";
        case AnswerType::PROCEDURE:      return "PROCEDURE";
        case AnswerType::COMPARISON:     return "COMPARISON";
        case AnswerType::SUMMARY:        return "SUMMARY";
    }
    return "UNKNOWN";
}

// Map AnswerType to preferred ChunkTypes
static std::vector<ChunkType> preferred_chunks(AnswerType at) {
    switch (at) {
        case AnswerType::LOCATION:
            return {ChunkType::LOCATION, ChunkType::GENERAL};
        case AnswerType::DEFINITION:
            return {ChunkType::DEFINITION, ChunkType::GENERAL};
        case AnswerType::PERSON_PROFILE:
            return {ChunkType::PERSON, ChunkType::HISTORY};
        case AnswerType::TEMPORAL:
            return {ChunkType::TEMPORAL, ChunkType::HISTORY};
        case AnswerType::PROCEDURE:
            return {ChunkType::PROCEDURE, ChunkType::GENERAL};
        default:
            return {};
    }
}

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

    // === NEW: Question-Answering pipeline ===
    if (cmd == "ask") {
        if (argc < 3) {
            std::cerr << "Usage: mysearch ask \"query\" [--json]\n";
            return 1;
        }

        std::string query = argv[2];
        bool json = (argc >= 4 && std::string(argv[3]) == "--json");
        std::string segdir = "../segments/seg_000001";

        SegmentReader reader(segdir);

        // 1. Analyze query (try neural, fall back to rule-based)
        QueryAnalyzer analyzer;
        std::string embeddir = "../embeddings";
        analyzer.load_neural(embeddir + "/qa_model.pt",
                             embeddir + "/vocab.txt");
        if (analyzer.is_neural())
            std::cerr << "Using neural query analyzer\n";
        QueryAnalysis qa = analyzer.analyze(query);

        // 2. Retrieve docs using BM25 on keywords
        BM25 bm25(reader);
        auto bm25_results = bm25.search(qa.keywords, 10);

        // 3. Chunk retrieved docs and build evidence
        Chunker chunker;
        auto prefs = preferred_chunks(qa.answerType);

        std::vector<Evidence> evidence;
        for (auto& [docId, score] : bm25_results) {
            std::string text = reader.get_document_text(docId);
            if (text.empty()) continue;

            auto chunks = chunker.chunk_document(docId, text);
            for (auto& c : chunks) {
                // Prefer matching chunk types, but include all with lower score
                double chunk_score = score;
                bool preferred = false;
                for (auto pt : prefs) {
                    if (c.type == pt) { preferred = true; break; }
                }
                if (preferred) chunk_score *= 2.0;

                evidence.push_back({c.docId, c.type, c.text, chunk_score});
            }
        }

        // Sort evidence by score
        std::sort(evidence.begin(), evidence.end(),
                  [](auto& a, auto& b) { return a.score > b.score; });

        // Limit to top evidence
        if (evidence.size() > 15) evidence.resize(15);

        // 4. Synthesize answer
        AnswerSynthesizer synthesizer;
        Answer answer = synthesizer.synthesize(qa, evidence);

        // 5. Output
        if (json) {
            std::string escaped;
            for (char c : answer.text) {
                if (c == '"') escaped += "\\\"";
                else if (c == '\\') escaped += "\\\\";
                else if (c == '\n') escaped += "\\n";
                else if (c == '\r') continue;
                else escaped += c;
            }
            std::cout << "{\n"
                      << "  \"query\": \"" << query << "\",\n"
                      << "  \"intent\": \"" << answer_type_str(qa.answerType) << "\",\n"
                      << "  \"entity\": \"" << qa.mainEntity << "\",\n"
                      << "  \"confidence\": " << std::fixed << std::setprecision(2) << answer.confidence << ",\n"
                      << "  \"answer\": \"" << escaped << "\"\n"
                      << "}\n";
        } else {
            std::cout << "[" << answer_type_str(qa.answerType) << "] "
                      << "Entity: " << qa.mainEntity << "\n"
                      << "Confidence: " << std::fixed << std::setprecision(2)
                      << answer.confidence << "\n\n"
                      << answer.text << "\n";
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

        auto test = [&](const char* q) {
            auto qa = nqa.analyze(q);
            std::cerr << "  \"" << q << "\" -> "
                      << answer_type_str(qa.answerType)
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
