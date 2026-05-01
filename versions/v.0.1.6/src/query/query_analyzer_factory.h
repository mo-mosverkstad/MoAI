#pragma once
#include "i_query_analyzer.h"
#include "query_analyzer.h"
#include "../common/config.h"
#include <memory>
#include <string>
#include <iostream>
#include <filesystem>

#ifdef HAS_TORCH
#include "neural_query_analyzer.h"
#include "../embedding/vocab.h"
#include "../answer/answer_scope.h"

// Adapter: wraps NeuralQueryAnalyzer (legacy QueryAnalysis) as IQueryAnalyzer
class NeuralQueryAnalyzerAdapter : public IQueryAnalyzer {
public:
    NeuralQueryAnalyzerAdapter(const std::string& model_path,
                                const std::string& vocab_path) {
        vocab_.load(vocab_path);
        nqa_ = std::make_unique<NeuralQueryAnalyzer>(vocab_, 128, 64);
        nqa_->load(model_path);
    }

    std::vector<InformationNeed> analyze(const std::string& query) override {
        auto qa = nqa_->analyze(query);
        InformationNeed need;
        need.entity = qa.mainEntity;
        need.keywords = qa.keywords;
        switch (qa.answerType) {
            case AnswerType::LOCATION:       need.property = Property::LOCATION; break;
            case AnswerType::DEFINITION:     need.property = Property::DEFINITION; break;
            case AnswerType::TEMPORAL:       need.property = Property::TIME; break;
            case AnswerType::COMPARISON:     need.property = Property::COMPARISON; break;
            case AnswerType::PROCEDURE:      need.property = Property::FUNCTION; break;
            case AnswerType::PERSON_PROFILE: need.property = Property::HISTORY; break;
            default:                         need.property = Property::GENERAL; break;
        }
        switch (qa.answerType) {
            case AnswerType::LOCATION:
            case AnswerType::TEMPORAL:       need.form = AnswerForm::SHORT_FACT; break;
            case AnswerType::COMPARISON:     need.form = AnswerForm::COMPARISON; break;
            case AnswerType::PROCEDURE:      need.form = AnswerForm::EXPLANATION; break;
            default:                         need.form = AnswerForm::SUMMARY; break;
        }
        need.scope = default_scope_for_property(need.property);
        return {need};
    }

    std::string name() const override { return "neural"; }

private:
    Vocabulary vocab_;
    std::unique_ptr<NeuralQueryAnalyzer> nqa_;
};
#endif

class QueryAnalyzerFactory {
public:
    static std::unique_ptr<IQueryAnalyzer> create(const std::string& embeddir) {
        auto& cfg = Config::instance();
        std::string type = cfg.get_string("query.analyzer", "auto");

        if (type == "rule") {
            std::cerr << "Using rule-based query analyzer (config)\n";
            return std::make_unique<RuleBasedQueryAnalyzer>();
        }

#ifdef HAS_TORCH
        std::string model_path = embeddir + "/qa_model.pt";
        std::string vocab_path = embeddir + "/vocab.txt";

        if (type == "neural") {
            if (!std::filesystem::exists(model_path) ||
                !std::filesystem::exists(vocab_path))
                throw std::runtime_error("Neural query analyzer requested but model not found");
            std::cerr << "Using neural query analyzer (config)\n";
            return std::make_unique<NeuralQueryAnalyzerAdapter>(model_path, vocab_path);
        }

        if (type == "auto") {
            if (std::filesystem::exists(model_path) &&
                std::filesystem::exists(vocab_path)) {
                std::cerr << "Using neural query analyzer (auto-detected)\n";
                return std::make_unique<NeuralQueryAnalyzerAdapter>(model_path, vocab_path);
            }
        }
#else
        if (type == "neural")
            throw std::runtime_error("Neural query analyzer requested but not compiled (USE_TORCH=OFF)");
#endif

        return std::make_unique<RuleBasedQueryAnalyzer>();
    }
};
