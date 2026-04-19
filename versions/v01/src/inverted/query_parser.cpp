#include "query_parser.h"
#include <sstream>
#include <iostream>

std::vector<std::string> QueryParser::tokenize_query(const std::string& q) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < q.size()) {
        // skip whitespace
        while (i < q.size() && q[i] == ' ') i++;
        if (i >= q.size()) break;

        if (q[i] == '"') {
            // collect everything up to and including the closing quote
            size_t end = q.find('"', i + 1);
            if (end == std::string::npos) end = q.size();
            else end++; // include closing quote
            out.push_back(q.substr(i, end - i));
            i = end;
        } else {
            size_t start = i;
            while (i < q.size() && q[i] != ' ') i++;
            out.push_back(q.substr(start, i - start));
        }
    }
    return out;
}

bool QueryParser::is_phrase(const std::string& tok) {
    return tok.size() > 2 && tok.front() == '"' && tok.back() == '"';
}

Phrase QueryParser::parse_phrase(const std::string& tok) {
    Phrase p;
    std::string inner = tok.substr(1, tok.size()-2);

    std::stringstream ss(inner);
    std::string w;
    while (ss >> w) p.terms.push_back(w);

    return p;
}

Expr QueryParser::parse(const std::string& q) {
    // MVP: only support: TERM | "phrase" [AND|OR|NOT TERM|PHRASE ...]
    auto toks = tokenize_query(q);

    if (toks.empty()) {
        return Expr{ TermNode{" "} };
    }

    // Build left-associative Boolean AST
    Expr* left = new Expr;

    if (is_phrase(toks[0])) {
        left->node = parse_phrase(toks[0]);
    } else {
        left->node = TermNode{ toks[0] };
    }

    size_t i = 1;
    while (i < toks.size()) {
        std::string op = toks[i++];
        if (i >= toks.size()) break;

        BoolOp bop = BoolOp::AND;
        if (op == "AND") bop = BoolOp::AND;
        else if (op == "OR") bop = BoolOp::OR;
        else if (op == "NOT") bop = BoolOp::NOT;

        Expr* right = new Expr;

        if (is_phrase(toks[i])) {
            right->node = parse_phrase(toks[i]);
        } else {
            right->node = TermNode{ toks[i] };
        }
        i++;

        BoolExpr* be = new BoolExpr{ left, bop, right };
        left = new Expr;
        left->node = be;
    }
    return *left;
}