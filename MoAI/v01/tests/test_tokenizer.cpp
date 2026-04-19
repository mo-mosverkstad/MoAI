#include <gtest/gtest.h>
#include "inverted/tokenizer.h"

class TokenizerTest : public ::testing::Test {
protected:
    Tokenizer tok;
};

TEST_F(TokenizerTest, BasicWords) {
    auto tokens = tok.tokenize("hello world");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
}

TEST_F(TokenizerTest, LowercaseConversion) {
    auto tokens = tok.tokenize("Hello WORLD FoO");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
    EXPECT_EQ(tokens[2], "foo");
}

TEST_F(TokenizerTest, PunctuationRemoval) {
    auto tokens = tok.tokenize("hello, world! foo.");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
    EXPECT_EQ(tokens[2], "foo");
}

TEST_F(TokenizerTest, NumbersPreserved) {
    auto tokens = tok.tokenize("test123 456abc");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "test123");
    EXPECT_EQ(tokens[1], "456abc");
}

TEST_F(TokenizerTest, EmptyString) {
    auto tokens = tok.tokenize("");
    EXPECT_TRUE(tokens.empty());
}

TEST_F(TokenizerTest, OnlyPunctuation) {
    auto tokens = tok.tokenize("!@#$%^&*()");
    EXPECT_TRUE(tokens.empty());
}

TEST_F(TokenizerTest, MultipleSpaces) {
    auto tokens = tok.tokenize("  hello   world  ");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
}

TEST_F(TokenizerTest, MixedDelimiters) {
    auto tokens = tok.tokenize("The quick-brown fox's tail");
    ASSERT_EQ(tokens.size(), 6u);
    EXPECT_EQ(tokens[0], "the");
    EXPECT_EQ(tokens[1], "quick");
    EXPECT_EQ(tokens[2], "brown");
    EXPECT_EQ(tokens[3], "fox");
    EXPECT_EQ(tokens[4], "s");
    EXPECT_EQ(tokens[5], "tail");
}
