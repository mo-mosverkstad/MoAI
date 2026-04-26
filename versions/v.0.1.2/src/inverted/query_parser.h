#pragma once
#include <string>
#include <vector>
#include <variant>

enum class BoolOp { AND, OR, NOT };

struct Phrase {
    std::vector<std::string> terms;
};

struct TermNode {
    std::string term;
};

struct Expr {
    // a node can be: Term, Phrase, or a Boolean expression
    std::variant<TermNode, Phrase, struct BoolExpr*> node;
};

struct BoolExpr {
    Expr* left;
    BoolOp op;
    Expr* right;
};

class QueryParser {
public:
    Expr parse(const std::string& q);

private:
    std::vector<std::string> tokenize_query(const std::string& q);
    bool is_phrase(const std::string& tok);
    Phrase parse_phrase(const std::string& tok);
};