#include <gtest/gtest.h>
#include <filesystem>
#include <cmath>
#include "storage/segment_writer.h"
#include "storage/segment_reader.h"
#include "inverted/bm25.h"

class BM25Test : public ::testing::Test {
protected:
    std::string test_dir = "test_bm25_tmp";

    void SetUp() override {
        std::filesystem::remove_all(test_dir);

        SegmentWriter writer(test_dir);
        // doc1: "the quick brown fox" (len=4)
        writer.add_document("the quick brown fox",
                            {"the", "quick", "brown", "fox"});
        // doc2: "the lazy dog" (len=3)
        writer.add_document("the lazy dog",
                            {"the", "lazy", "dog"});
        // doc3: "quick quick fox" (len=3, quick tf=2)
        writer.add_document("quick quick fox",
                            {"quick", "quick", "fox"});
        writer.finalize();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(BM25Test, SearchReturnsResults) {
    SegmentReader reader(test_dir);
    BM25 scorer(reader);

    auto results = scorer.search({"quick"}, 10);
    ASSERT_FALSE(results.empty());

    // "quick" appears in doc1 and doc3
    EXPECT_GE(results.size(), 2u);
}

TEST_F(BM25Test, HigherTFScoresHigher) {
    SegmentReader reader(test_dir);
    BM25 scorer(reader);

    auto results = scorer.search({"quick"}, 10);
    ASSERT_GE(results.size(), 2u);

    // doc3 has tf=2 for "quick", doc1 has tf=1
    // Find scores for each
    double score_doc1 = 0, score_doc3 = 0;
    for (auto& [doc, score] : results) {
        if (doc == 1) score_doc1 = score;
        if (doc == 3) score_doc3 = score;
    }
    EXPECT_GT(score_doc3, score_doc1);
}

TEST_F(BM25Test, NonexistentTermReturnsEmpty) {
    SegmentReader reader(test_dir);
    BM25 scorer(reader);

    auto results = scorer.search({"zzzzz"}, 10);
    EXPECT_TRUE(results.empty());
}

TEST_F(BM25Test, EmptyTokensReturnsEmpty) {
    SegmentReader reader(test_dir);
    BM25 scorer(reader);

    auto results = scorer.search({}, 10);
    EXPECT_TRUE(results.empty());
}

TEST_F(BM25Test, MultiTermSearch) {
    SegmentReader reader(test_dir);
    BM25 scorer(reader);

    auto results = scorer.search({"quick", "brown"}, 10);
    ASSERT_FALSE(results.empty());

    // doc1 contains both terms, should score highest
    EXPECT_EQ(results[0].first, 1u);
}

TEST_F(BM25Test, ScoreDocsFiltersCorrectly) {
    SegmentReader reader(test_dir);
    BM25 scorer(reader);

    // Only score doc 1
    auto results = scorer.score_docs({"quick"}, {1});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].first, 1u);
    EXPECT_GT(results[0].second, 0.0);
}

TEST_F(BM25Test, ScoreDocsEmptyInput) {
    SegmentReader reader(test_dir);
    BM25 scorer(reader);

    auto r1 = scorer.score_docs({}, {1});
    EXPECT_TRUE(r1.empty());

    auto r2 = scorer.score_docs({"quick"}, {});
    EXPECT_TRUE(r2.empty());
}

TEST_F(BM25Test, ScoresArePositive) {
    SegmentReader reader(test_dir);
    BM25 scorer(reader);

    auto results = scorer.search({"fox"}, 10);
    for (auto& [doc, score] : results) {
        EXPECT_GT(score, 0.0);
    }
}

TEST_F(BM25Test, TopKLimitsResults) {
    SegmentReader reader(test_dir);
    BM25 scorer(reader);

    // "the" appears in doc1 and doc2
    auto results = scorer.search({"the"}, 1);
    EXPECT_LE(results.size(), 1u);
}
