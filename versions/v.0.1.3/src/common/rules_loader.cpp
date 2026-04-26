#include "rules_loader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static Property parse_property(const std::string& s) {
    static const std::unordered_map<std::string, Property> m = {
        {"LOCATION", Property::LOCATION}, {"DEFINITION", Property::DEFINITION},
        {"FUNCTION", Property::FUNCTION}, {"COMPOSITION", Property::COMPOSITION},
        {"HISTORY", Property::HISTORY}, {"TIME", Property::TIME},
        {"COMPARISON", Property::COMPARISON}, {"ADVANTAGES", Property::ADVANTAGES},
        {"LIMITATIONS", Property::LIMITATIONS}, {"USAGE", Property::USAGE},
        {"GENERAL", Property::GENERAL},
    };
    auto it = m.find(s);
    return (it != m.end()) ? it->second : Property::GENERAL;
}

static AnswerForm parse_form(const std::string& s) {
    static const std::unordered_map<std::string, AnswerForm> m = {
        {"SHORT_FACT", AnswerForm::SHORT_FACT}, {"EXPLANATION", AnswerForm::EXPLANATION},
        {"LIST", AnswerForm::LIST}, {"COMPARISON", AnswerForm::COMPARISON},
        {"SUMMARY", AnswerForm::SUMMARY},
    };
    auto it = m.find(s);
    return (it != m.end()) ? it->second : AnswerForm::SHORT_FACT;
}

static int parse_chunk_type(const std::string& s) {
    static const std::unordered_map<std::string, ChunkType> m = {
        {"LOCATION", ChunkType::LOCATION}, {"DEFINITION", ChunkType::DEFINITION},
        {"FUNCTION", ChunkType::FUNCTION}, {"USAGE", ChunkType::USAGE},
        {"HISTORY", ChunkType::HISTORY}, {"TEMPORAL", ChunkType::TEMPORAL},
        {"ADVANTAGES", ChunkType::ADVANTAGES}, {"LIMITATIONS", ChunkType::LIMITATIONS},
        {"PERSON", ChunkType::PERSON}, {"PROCEDURE", ChunkType::PROCEDURE},
        {"GENERAL", ChunkType::GENERAL},
    };
    auto it = m.find(s);
    return (it != m.end()) ? static_cast<int>(it->second) : -1;
}

static QueryIntent parse_intent(const std::string& s) {
    static const std::unordered_map<std::string, QueryIntent> m = {
        {"FACTUAL", QueryIntent::FACTUAL}, {"EXPLANATION", QueryIntent::EXPLANATION},
        {"PROCEDURAL", QueryIntent::PROCEDURAL}, {"COMPARISON", QueryIntent::COMPARISON},
        {"GENERAL", QueryIntent::GENERAL},
    };
    auto it = m.find(s);
    return (it != m.end()) ? it->second : QueryIntent::GENERAL;
}

static AnswerType parse_answer_type(const std::string& s) {
    static const std::unordered_map<std::string, AnswerType> m = {
        {"LOCATION", AnswerType::LOCATION}, {"DEFINITION", AnswerType::DEFINITION},
        {"PERSON_PROFILE", AnswerType::PERSON_PROFILE}, {"TEMPORAL", AnswerType::TEMPORAL},
        {"PROCEDURE", AnswerType::PROCEDURE}, {"COMPARISON", AnswerType::COMPARISON},
        {"SUMMARY", AnswerType::SUMMARY},
    };
    auto it = m.find(s);
    return (it != m.end()) ? it->second : AnswerType::SUMMARY;
}

const PlanningRules& PlanningRules::get() {
    static PlanningRules rules = []() {
        PlanningRules r;
        std::ifstream f("../config/vocabularies/planning_rules.conf");
        if (!f.is_open()) {
            std::cerr << "[WARN] Planning rules file not found: ../config/vocabularies/planning_rules.conf\n";
            return r;
        }
        std::string section, line;
        while (std::getline(f, line)) {
            std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            if (t.front() == '[' && t.back() == ']') {
                section = t.substr(1, t.size() - 2);
                continue;
            }
            auto arrow = t.find("->");
            if (arrow == std::string::npos) continue;
            std::string left = trim(t.substr(0, arrow));
            std::string right = trim(t.substr(arrow + 2));

            if (section == "SELF_ASK") {
                // Format: TRIGGER -> SUB_PROPERTY : FORM : kw1, kw2
                auto colon1 = right.find(':');
                if (colon1 == std::string::npos) continue;
                std::string sub_prop = trim(right.substr(0, colon1));
                std::string rest = right.substr(colon1 + 1);
                auto colon2 = rest.find(':');
                if (colon2 == std::string::npos) continue;
                std::string form_str = trim(rest.substr(0, colon2));
                std::string kw_str = rest.substr(colon2 + 1);

                SelfAskRule rule;
                rule.trigger = parse_property(left);
                rule.sub_property = parse_property(sub_prop);
                rule.form = parse_form(form_str);
                std::istringstream kss(kw_str);
                std::string kw;
                while (std::getline(kss, kw, ',')) {
                    std::string w = trim(kw);
                    if (!w.empty()) rule.keywords.push_back(w);
                }
                r.self_ask.push_back(rule);
            } else if (section == "DEPENDENCIES") {
                r.dependencies.push_back({parse_property(left), parse_property(right)});
            } else if (section == "PREFERRED_CHUNKS") {
                // Format: PROPERTY -> CHUNK1, CHUNK2, ...
                int prop = static_cast<int>(parse_property(left));
                std::istringstream css(right);
                std::string ct;
                while (std::getline(css, ct, ',')) {
                    int c = parse_chunk_type(trim(ct));
                    if (c >= 0) r.preferred_chunks[prop].push_back(c);
                }
            }
        }

        // Load query templates from separate file
        std::ifstream tf("../config/vocabularies/query_templates.conf");
        if (tf.is_open()) {
            std::string tsec, tline;
            while (std::getline(tf, tline)) {
                std::string tt = trim(tline);
                if (tt.empty() || tt[0] == '#') continue;
                if (tt.front() == '[' && tt.back() == ']') {
                    tsec = tt.substr(1, tt.size() - 2);
                    continue;
                }
                if (tsec != "TEMPLATES") continue;
                // Format: prefix | suffix | intent | answer_type
                std::vector<std::string> parts;
                std::istringstream pss(tt);
                std::string part;
                while (std::getline(pss, part, '|'))
                    parts.push_back(trim(part));
                if (parts.size() < 4) continue;
                QueryTemplate qt;
                qt.prefix = parts[0].empty() ? "" : parts[0] + " ";
                qt.suffix = parts[1].empty() ? "" : " " + parts[1];
                qt.intent = parse_intent(parts[2]);
                qt.answer_type = parse_answer_type(parts[3]);
                r.query_templates.push_back(qt);
            }
        }

        return r;
    }();
    return rules;
}
