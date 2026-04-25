#include <gtest/gtest.h>
#include <filesystem>
#include "storage/segment_writer.h"
#include "storage/segment_reader.h"

class SegmentIOTest : public ::testing::Test {
protected:
    std::string test_dir = "test_seg_tmp";

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(SegmentIOTest, WriteAndReadBack) {
    {
        SegmentWriter writer(test_dir);
        writer.add_document("the quick brown fox",
                            {"the", "quick", "brown", "fox"});
        writer.add_document("the lazy dog",
                            {"the", "lazy", "dog"});
        writer.finalize();
    }

    SegmentReader reader(test_dir);
    EXPECT_EQ(reader.doc_count(), 2u);
    EXPECT_EQ(reader.doc_length(1), 4u);
    EXPECT_EQ(reader.doc_length(2), 3u);
}

TEST_F(SegmentIOTest, PostingsRetrieval) {
    {
        SegmentWriter writer(test_dir);
        writer.add_document("hello world",
                            {"hello", "world"});
        writer.add_document("hello again",
                            {"hello", "again"});
        writer.finalize();
    }

    SegmentReader reader(test_dir);

    auto postings = reader.get_postings("hello");
    ASSERT_EQ(postings.size(), 2u);
    EXPECT_EQ(postings[0].tf, 1u);
    EXPECT_EQ(postings[1].tf, 1u);

    auto postings2 = reader.get_postings("world");
    ASSERT_EQ(postings2.size(), 1u);
    EXPECT_EQ(postings2[0].doc, 1u);

    auto postings3 = reader.get_postings("nonexistent");
    EXPECT_TRUE(postings3.empty());
}

TEST_F(SegmentIOTest, TermFrequency) {
    {
        SegmentWriter writer(test_dir);
        writer.add_document("fox fox dog",
                            {"fox", "fox", "dog"});
        writer.finalize();
    }

    SegmentReader reader(test_dir);
    auto postings = reader.get_postings("fox");
    ASSERT_EQ(postings.size(), 1u);
    EXPECT_EQ(postings[0].tf, 2u);
}

TEST_F(SegmentIOTest, DocLengthOutOfRange) {
    {
        SegmentWriter writer(test_dir);
        writer.add_document("a b", {"a", "b"});
        writer.finalize();
    }

    SegmentReader reader(test_dir);
    EXPECT_EQ(reader.doc_length(0), 0u);
    EXPECT_EQ(reader.doc_length(99), 0u);
}

TEST_F(SegmentIOTest, AverageDocLength) {
    {
        SegmentWriter writer(test_dir);
        writer.add_document("a b c d", {"a", "b", "c", "d"});
        writer.add_document("e f", {"e", "f"});
        writer.finalize();
    }

    SegmentReader reader(test_dir);
    EXPECT_DOUBLE_EQ(reader.average_doc_length(), 3.0);
}

TEST_F(SegmentIOTest, PositionsStored) {
    {
        SegmentWriter writer(test_dir);
        // "the quick brown fox" → positions: the=0, quick=1, brown=2, fox=3
        writer.add_document("the quick brown fox",
                            {"the", "quick", "brown", "fox"});
        writer.finalize();
    }

    SegmentReader reader(test_dir);

    auto postings = reader.get_postings("quick");
    ASSERT_EQ(postings.size(), 1u);
    ASSERT_EQ(postings[0].positions.size(), 1u);
    EXPECT_EQ(postings[0].positions[0], 1u);

    auto postings2 = reader.get_postings("fox");
    ASSERT_EQ(postings2.size(), 1u);
    ASSERT_EQ(postings2[0].positions.size(), 1u);
    EXPECT_EQ(postings2[0].positions[0], 3u);
}

TEST_F(SegmentIOTest, MultiplePositionsForSameTerm) {
    {
        SegmentWriter writer(test_dir);
        // "the fox and the fox" → "the" at 0,3; "fox" at 1,4
        writer.add_document("the fox and the fox",
                            {"the", "fox", "and", "the", "fox"});
        writer.finalize();
    }

    SegmentReader reader(test_dir);

    auto postings = reader.get_postings("the");
    ASSERT_EQ(postings.size(), 1u);
    EXPECT_EQ(postings[0].tf, 2u);
    ASSERT_EQ(postings[0].positions.size(), 2u);
    EXPECT_EQ(postings[0].positions[0], 0u);
    EXPECT_EQ(postings[0].positions[1], 3u);

    auto postings2 = reader.get_postings("fox");
    ASSERT_EQ(postings2.size(), 1u);
    EXPECT_EQ(postings2[0].tf, 2u);
    ASSERT_EQ(postings2[0].positions.size(), 2u);
    EXPECT_EQ(postings2[0].positions[0], 1u);
    EXPECT_EQ(postings2[0].positions[1], 4u);
}

TEST_F(SegmentIOTest, GetPositionsForDoc) {
    {
        SegmentWriter writer(test_dir);
        writer.add_document("quick brown fox",
                            {"quick", "brown", "fox"});
        writer.add_document("lazy brown dog",
                            {"lazy", "brown", "dog"});
        writer.finalize();
    }

    SegmentReader reader(test_dir);

    auto pos1 = reader.get_positions_for_doc("brown", 1);
    ASSERT_EQ(pos1.size(), 1u);
    EXPECT_EQ(pos1[0], 1u);

    auto pos2 = reader.get_positions_for_doc("brown", 2);
    ASSERT_EQ(pos2.size(), 1u);
    EXPECT_EQ(pos2[0], 1u);

    auto pos3 = reader.get_positions_for_doc("brown", 99);
    EXPECT_TRUE(pos3.empty());
}
