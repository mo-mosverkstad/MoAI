#include "commands.h"
#include "../tools/sandbox.h"
#include "../storage/segment_reader.h"
#include "../inverted/tokenizer.h"
#include "../inverted/bm25.h"
#include "../inverted/index_builder.h"
#include "../inverted/search_engine.h"
#include "../hybrid/hybrid_builder.h"
#include "../hybrid/hybrid_search.h"
#include "../query/query_analyzer_factory.h"
#include "../chunk/chunker.h"
#include "../answer/answer_synthesizer.h"
#include "../answer/answer_validator.h"
#include "../answer/answer_scope.h"
#include "../answer/answer_compressor.h"
#include "../common/config.h"
#include "../retrieval/retriever_factory.h"
#include "../answer/question_planner.h"
#include "../answer/self_ask.h"
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
            std::cerr << "Usage: mysearch ask \"query\" [--json] [--brief] [--detailed]\n";
            return 1;
        }

        std::string query = argv[2];
        bool json = false;
        bool force_brief = false;
        bool force_detailed = false;
        for (int i = 3; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--json") json = true;
            else if (arg == "--brief") force_brief = true;
            else if (arg == "--detailed") force_detailed = true;
        }
        std::string segdir = "../segments/seg_000001";
        std::string embeddir = "../embeddings";

        SegmentReader reader(segdir);

        // Load config
        auto& cfg = Config::instance();
        size_t cfg_max_evidence = cfg.get_size("retrieval.max_evidence", 15);
        size_t cfg_chunks_per_doc = cfg.get_size("chunk.max_per_doc", 8);
        double cfg_support_thr = cfg.get_double("validator.support_coverage_threshold", 0.5);

        // 1. Analyze query
        auto analyzer = QueryAnalyzerFactory::create(embeddir);
        auto needs = analyzer->analyze(query);

        // Apply conversation memory
        std::string conv_path = segdir + "/../.conversation";
        ConversationState conversation;
        conversation.load(conv_path);
        conversation.apply(needs);

        // 2. Create retriever from config
        auto retriever = RetrieverFactory::create(reader, embeddir);

        Chunker chunker;
        AnswerSynthesizer synthesizer;
        CompositeAnswer composite;

        // 3. Self-Ask: expand needs with support sub-needs
        SelfAskModule self_ask;
        std::vector<InformationNeed> expanded = needs;
        for (auto& n : needs) {
            auto support = self_ask.expand(n);
            // Only add support needs that aren't already in the list
            for (auto& s : support) {
                bool dup = false;
                for (auto& e : expanded)
                    if (e.property == s.property && e.entity == s.entity)
                        { dup = true; break; }
                if (!dup) {
                    s.is_support = true;
                    expanded.push_back(s);
                }
            }
        }

        // Apply CLI scope override (--brief / --detailed)
        if (force_brief || force_detailed) {
            AnswerScope forced = force_brief ? AnswerScope::STRICT : AnswerScope::EXPANDED;
            for (auto& n : expanded)
                if (!n.is_support) n.scope = forced;
        }

        // 4. Plan question order (dependency-aware topological sort)
        QuestionPlanner planner;
        QuestionPlan plan = planner.build(expanded);
        needs = plan.needs;

        // 5. For each need: retrieve → chunk select → synthesize → validate
        AnswerValidator validator;
        std::string prior_answer_context;

        for (auto& need : needs) {
            // --- Retrieval ---
            auto ranked_docs = retriever->search(need.keywords);

            // --- Chunk & Select ---
            std::vector<Evidence> evidence;
            for (auto& sd : ranked_docs) {
                std::string text = reader.get_document_text(sd.docId);
                if (text.empty()) continue;
                auto all_chunks = chunker.chunk_document(sd.docId, text);
                auto selected = Chunker::select_chunks(all_chunks, need.property, need.keywords, cfg_chunks_per_doc);
                for (auto& c : selected)
                    evidence.push_back({c.docId, c.type, c.text, sd.score});
            }

            if (evidence.size() > cfg_max_evidence) evidence.resize(cfg_max_evidence);

            // --- Self-Ask: check if evidence covers support sub-questions ---
            auto support_needs = self_ask.expand(need);
            std::vector<std::string> ev_texts;
            for (auto& e : evidence) ev_texts.push_back(e.text);
            double support_coverage = self_ask.check_support_coverage(support_needs, ev_texts);

            // --- Synthesize ---
            Answer answer = synthesizer.synthesize(need, evidence);

            // Phase 3: Refined confidence using agreement + contradiction analysis
            auto ev_analysis = validator.analyze_evidence(evidence, need.entity);
            answer.confidence = validator.compute_refined_confidence(
                evidence, need.entity, need.keywords);

            if (ev_analysis.contradiction_pairs > 0) {
                std::string note = std::to_string(ev_analysis.contradiction_pairs) +
                    " contradiction(s) detected.";
                if (ev_analysis.agreement_pairs > 0)
                    note += " " + std::to_string(ev_analysis.agreement_pairs) + " agreement(s).";
                answer.validation_note = note;
            }

            // Agreement-based compression
            AgreementCompressor compressor;
            CompressionContext comp_ctx{
                need.scope, answer.confidence,
                ev_analysis.agreement, evidence.size()};
            answer.text = compressor.compress(answer.text, comp_ctx);
            answer.compression = compressor.decide_level(comp_ctx);

            // Dependent planning: attach prior answer as context
            if (!prior_answer_context.empty())
                answer.prior_context = prior_answer_context;

            // Confidence-based scope adjustment
            need.scope = adjust_scope_by_confidence(need.scope, answer.confidence);
            answer.scope = need.scope;
            // Re-apply scope truncation after adjustment
            size_t max_chars = max_answer_chars(need.scope);
            if (answer.text.size() > max_chars) {
                size_t cut = answer.text.rfind('.', max_chars);
                if (cut != std::string::npos && cut > max_chars / 2)
                    answer.text.resize(cut + 1);
                else
                    answer.text.resize(max_chars);
            }

            // 6.2: Self-ask validation — does the answer address the property?
            validator.validate(answer, need);

            // Apply self-ask support coverage to confidence
            if (support_coverage < cfg_support_thr && !support_needs.empty()) {
                answer.confidence *= (0.5 + support_coverage);
                std::string sc_note = "Self-ask coverage: " +
                    std::to_string((int)(support_coverage * 100)) + "%.";
                if (answer.validation_note.empty())
                    answer.validation_note = sc_note;
                else
                    answer.validation_note += " " + sc_note;
            }

            // 6.2b: If validation failed and we used hybrid, retry with BM25-only
            if (!answer.validated && retriever->supports_fallback()) {
                std::vector<Evidence> bm25_evidence;
                for (auto& sd : retriever->fallback_search(need.keywords)) {
                    std::string text = reader.get_document_text(sd.docId);
                    if (text.empty()) continue;
                    auto all_chunks = chunker.chunk_document(sd.docId, text);
                    auto selected = Chunker::select_chunks(
                        all_chunks, need.property, need.keywords, 5);
                    for (auto& c : selected)
                        bm25_evidence.push_back({c.docId, c.type, c.text, sd.score});
                }
                if (bm25_evidence.size() > cfg_max_evidence) bm25_evidence.resize(cfg_max_evidence);

                Answer retry = synthesizer.synthesize(need, bm25_evidence);
                validator.validate(retry, need);

                if (retry.validated || retry.confidence > answer.confidence) {
                    answer = retry;
                    answer.validation_note += " (retried with BM25-only)";
                    if (!prior_answer_context.empty())
                        answer.prior_context = prior_answer_context;
                }
            }

            composite.parts.push_back(answer);

            // 6.1: Store this answer as context for the next need
            prior_answer_context = answer.text;

            // Update conversation memory
            conversation.update(need);
        }

        // 6. Save conversation memory for next invocation
        if (!needs.empty())
            conversation.save(conv_path);

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
                      << "  \"retrieval\": \"" << retriever->name().c_str() << "\",\n"
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
            if (retriever->name() != "bm25")
                std::cerr << ("Retrieval: " + retriever->name() + "\n").c_str();
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
