#include <gtest/gtest.h>
#include "inverted/query_parser.h"

TEST(QueryParser, SingleTerm) {
    QueryParser qp;
    Expr e = qp.parse("hello");
    ASSERT_TRUE(std::holds_alternative<TermNode>(e.node));
    EXPECT_EQ(std::get<TermNode>(e.node).term, "hello");
}

TEST(QueryParser, PhraseQuery) {
    QueryParser qp;
    Expr e = qp.parse("\"quick brown\"");
    ASSERT_TRUE(std::holds_alternative<Phrase>(e.node));
    auto& p = std::get<Phrase>(e.node);
    ASSERT_EQ(p.terms.size(), 2u);
    EXPECT_EQ(p.terms[0], "quick");
    EXPECT_EQ(p.terms[1], "brown");
}

TEST(QueryParser, BooleanAND) {
    QueryParser qp;
    Expr e = qp.parse("fox AND quick");
    ASSERT_TRUE(std::holds_alternative<BoolExpr*>(e.node));

    auto* be = std::get<BoolExpr*>(e.node);
    EXPECT_EQ(be->op, BoolOp::AND);

    ASSERT_TRUE(std::holds_alternative<TermNode>(be->left->node));
    EXPECT_EQ(std::get<TermNode>(be->left->node).term, "fox");

    ASSERT_TRUE(std::holds_alternative<TermNode>(be->right->node));
    EXPECT_EQ(std::get<TermNode>(be->right->node).term, "quick");
}

TEST(QueryParser, BooleanOR) {
    QueryParser qp;
    Expr e = qp.parse("fox OR quick");
    ASSERT_TRUE(std::holds_alternative<BoolExpr*>(e.node));
    EXPECT_EQ(std::get<BoolExpr*>(e.node)->op, BoolOp::OR);
}

TEST(QueryParser, BooleanNOT) {
    QueryParser qp;
    Expr e = qp.parse("fox NOT quick");
    ASSERT_TRUE(std::holds_alternative<BoolExpr*>(e.node));
    EXPECT_EQ(std::get<BoolExpr*>(e.node)->op, BoolOp::NOT);
}

TEST(QueryParser, PhraseWithBoolean) {
    QueryParser qp;
    Expr e = qp.parse("\"quick brown\" OR testing");
    ASSERT_TRUE(std::holds_alternative<BoolExpr*>(e.node));

    auto* be = std::get<BoolExpr*>(e.node);
    EXPECT_EQ(be->op, BoolOp::OR);

    // Left should be a phrase
    ASSERT_TRUE(std::holds_alternative<Phrase>(be->left->node));
    auto& p = std::get<Phrase>(be->left->node);
    ASSERT_EQ(p.terms.size(), 2u);
    EXPECT_EQ(p.terms[0], "quick");
    EXPECT_EQ(p.terms[1], "brown");

    // Right should be a term
    ASSERT_TRUE(std::holds_alternative<TermNode>(be->right->node));
    EXPECT_EQ(std::get<TermNode>(be->right->node).term, "testing");
}

TEST(QueryParser, EmptyQuery) {
    QueryParser qp;
    Expr e = qp.parse("");
    ASSERT_TRUE(std::holds_alternative<TermNode>(e.node));
}

TEST(QueryParser, ChainedBoolean) {
    QueryParser qp;
    Expr e = qp.parse("a AND b OR c");
    // Should be ((a AND b) OR c)
    ASSERT_TRUE(std::holds_alternative<BoolExpr*>(e.node));

    auto* outer = std::get<BoolExpr*>(e.node);
    EXPECT_EQ(outer->op, BoolOp::OR);

    // Left of OR should be (a AND b)
    ASSERT_TRUE(std::holds_alternative<BoolExpr*>(outer->left->node));
    auto* inner = std::get<BoolExpr*>(outer->left->node);
    EXPECT_EQ(inner->op, BoolOp::AND);
    EXPECT_EQ(std::get<TermNode>(inner->left->node).term, "a");
    EXPECT_EQ(std::get<TermNode>(inner->right->node).term, "b");

    // Right of OR should be c
    EXPECT_EQ(std::get<TermNode>(outer->right->node).term, "c");
}
