#include "pipeline.h"
#include "../profiling/scope_timer.h"
#include <chrono>

PipelineResult Pipeline::run(const std::string& query, const PipelineOptions& opts) {
    PipelineResult result;
    result.retriever_name = retriever_->name();
    auto total_start = std::chrono::high_resolution_clock::now();

    auto& profiler = Profiler::instance();
    profiler.begin_query(query);
    profiler.record_algorithm("analyzer", analyzer_->name());
    profiler.record_algorithm("retriever", retriever_->name());
    profiler.record_rss_before();

    // 1. Analyze query
    std::vector<InformationNeed> needs;
    {
        ScopeTimer t("QueryAnalyzer");
        needs = analyzer_->analyze(query);
    }

    // 2. Conversation memory
    ConversationState conversation;
    conversation.load(conv_path_);
    conversation.apply(needs);

    // 3. Self-ask expansion
    std::vector<InformationNeed> expanded = needs;
    for (auto& n : needs) {
        auto support = self_ask_.expand(n);
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

    // 4. CLI scope override
    if (opts.force_brief || opts.force_detailed) {
        AnswerScope forced = opts.force_brief ? AnswerScope::STRICT : AnswerScope::EXPANDED;
        for (auto& n : expanded)
            if (!n.is_support) n.scope = forced;
    }

    // 5. Plan question order
    {
        ScopeTimer t("SelfAsk+Planning");
        QuestionPlan plan = planner_.build(expanded);
        result.needs = plan.needs;
    }

    // 6. Process each need
    std::string prior_answer_context;

    for (auto& need : result.needs) {
        // Retrieve
        std::vector<ScoredDoc> ranked_docs;
        {
            ScopeTimer t("Retriever");
            ranked_docs = retriever_->search(need.keywords);
        }

        // Chunk & select
        std::vector<Evidence> evidence;
        {
            ScopeTimer t("Chunker");
            for (auto& sd : ranked_docs) {
                std::string text = reader_.get_document_text(sd.docId);
                if (text.empty()) continue;
                auto all_chunks = chunker_.chunk_document(sd.docId, text);
                auto selected = Chunker::select_chunks(
                    all_chunks, need.property, need.keywords, chunks_per_doc_);
                for (auto& c : selected)
                    evidence.push_back({c.docId, c.type, c.text, sd.score});
            }
            if (evidence.size() > max_evidence_) evidence.resize(max_evidence_);
        }

        // Self-ask coverage check
        auto support_needs = self_ask_.expand(need);
        std::vector<std::string> ev_texts;
        for (auto& e : evidence) ev_texts.push_back(e.text);
        double support_coverage = self_ask_.check_support_coverage(support_needs, ev_texts);

        // Synthesize
        Answer answer;
        {
            ScopeTimer t("Synthesizer");
            answer = synthesizer_.synthesize(need, evidence);
        }

        // Refined confidence
        auto ev_analysis = validator_.analyze_evidence(evidence, need.entity);
        answer.confidence = validator_.compute_refined_confidence(
            evidence, need.entity, need.keywords);

        if (ev_analysis.contradiction_pairs > 0) {
            std::string note = std::to_string(ev_analysis.contradiction_pairs) +
                " contradiction(s) detected.";
            if (ev_analysis.agreement_pairs > 0)
                note += " " + std::to_string(ev_analysis.agreement_pairs) + " agreement(s).";
            answer.validation_note = note;
        }

        // Compression
        CompressionContext comp_ctx{
            need.scope, answer.confidence,
            ev_analysis.agreement, evidence.size()};
        answer.text = compressor_.compress(answer.text, comp_ctx);
        answer.compression = compressor_.decide_level(comp_ctx);

        // Prior context
        if (!prior_answer_context.empty())
            answer.prior_context = prior_answer_context;

        // Scope adjustment + truncation
        need.scope = adjust_scope_by_confidence(need.scope, answer.confidence);
        answer.scope = need.scope;
        size_t max_chars = max_answer_chars(need.scope);
        if (answer.text.size() > max_chars) {
            size_t cut = answer.text.rfind('.', max_chars);
            if (cut != std::string::npos && cut > max_chars / 2)
                answer.text.resize(cut + 1);
            else
                answer.text.resize(max_chars);
        }

        // Validate
        validator_.validate(answer, need);

        // Support coverage penalty
        if (support_coverage < support_thr_ && !support_needs.empty()) {
            answer.confidence *= (0.5 + support_coverage);
            std::string sc_note = "Self-ask coverage: " +
                std::to_string((int)(support_coverage * 100)) + "%.";
            if (answer.validation_note.empty())
                answer.validation_note = sc_note;
            else
                answer.validation_note += " " + sc_note;
        }

        // Fallback retry
        if (!answer.validated && retriever_->supports_fallback()) {
            std::vector<Evidence> fb_evidence;
            for (auto& sd : retriever_->fallback_search(need.keywords)) {
                std::string text = reader_.get_document_text(sd.docId);
                if (text.empty()) continue;
                auto all_chunks = chunker_.chunk_document(sd.docId, text);
                auto selected = Chunker::select_chunks(
                    all_chunks, need.property, need.keywords, 5);
                for (auto& c : selected)
                    fb_evidence.push_back({c.docId, c.type, c.text, sd.score});
            }
            if (fb_evidence.size() > max_evidence_) fb_evidence.resize(max_evidence_);

            Answer retry = synthesizer_.synthesize(need, fb_evidence);
            validator_.validate(retry, need);

            if (retry.validated || retry.confidence > answer.confidence) {
                answer = retry;
                answer.validation_note += " (retried with fallback)";
                if (!prior_answer_context.empty())
                    answer.prior_context = prior_answer_context;
            }
        }

        result.composite.parts.push_back(answer);
        prior_answer_context = answer.text;
        conversation.update(need);
    }

    // Save conversation
    if (!result.needs.empty())
        conversation.save(conv_path_);

    // Record quality metrics for profiling
    {
        QualityMetrics q;
        q.total_count = static_cast<int>(result.composite.parts.size());
        double conf_sum = 0.0;
        for (auto& a : result.composite.parts) {
            conf_sum += a.confidence;
            if (a.validated) q.validated_count++;
            if (a.validation_note.find("fallback") != std::string::npos)
                q.fallback_used = true;
            if (a.compression != CompressionLevel::NONE)
                q.compression = compression_str(a.compression);
        }
        q.avg_confidence = q.total_count > 0 ? conf_sum / q.total_count : 0.0;
        // Agreement from last evidence analysis (approximation)
        q.avg_agreement = result.composite.overall_confidence();
        profiler.record_quality(q);
    }

    profiler.record_needs_count(static_cast<int>(result.needs.size()));
    auto total_end = std::chrono::high_resolution_clock::now();
    profiler.record("Total",
        std::chrono::duration<double, std::milli>(total_end - total_start).count());
    profiler.record_rss_after();
    profiler.end_query();

    return result;
}
