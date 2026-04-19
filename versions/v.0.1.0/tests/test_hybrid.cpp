#include <gtest/gtest.h>
#include <filesystem>
#include <cmath>
#include "embedding/vocab.h"
#include "embedding/embedding_model.h"
#include "storage/segment_writer.h"
#include "storage/segment_reader.h"
#include "hnsw/hnsw_index.h"
#include "hybrid/hybrid_builder.h"
#include "hybrid/hybrid_search.h"

// --- Vocabulary Tests ---

TEST(VocabTest, BuildAndLookup) {
    Vocabulary vocab;
    vocab.build_from_terms({"database", "algorithm", "network", "database"});

    EXPECT_EQ(vocab.size(), 3u);
    EXPECT_GE(vocab.id("database"), 0);
    EXPECT_GE(vocab.id("algorithm"), 0);
    EXPECT_GE(vocab.id("network"), 0);
    EXPECT_EQ(vocab.id("missing"), -1);
}

TEST(VocabTest, SaveAndLoad) {
    std::string path = "test_vocab_tmp.txt";

    {
        Vocabulary vocab;
        vocab.build_from_terms({"sql", "tcp", "firewall"});
        ASSERT_TRUE(vocab.save(path));
    }

    Vocabulary loaded;
    ASSERT_TRUE(loaded.load(path));
    EXPECT_EQ(loaded.size(), 3u);
    EXPECT_GE(loaded.id("sql"), 0);
    EXPECT_GE(loaded.id("tcp"), 0);
    EXPECT_GE(loaded.id("firewall"), 0);
    EXPECT_EQ(loaded.id("missing"), -1);

    std::filesystem::remove(path);
}

// --- Embedding Model Tests ---

TEST(EmbeddingModelTest, InitAndEmbed) {
    EmbeddingModel model;
    model.init_random(10, 8, 4, 42);

    EXPECT_EQ(model.dim(), 4u);

    std::vector<float> bow(10, 0.0f);
    bow[0] = 1.0f;
    bow[3] = 2.0f;

    auto emb = model.embed(bow);
    ASSERT_EQ(emb.size(), 4u);

    float norm = 0.0f;
    for (float x : emb) norm += x * x;
    EXPECT_NEAR(norm, 1.0f, 1e-5);
}

TEST(EmbeddingModelTest, ZeroInputProducesZeroOrNormalized) {
    EmbeddingModel model;
    model.init_random(5, 4, 3, 99);

    std::vector<float> bow(5, 0.0f);
    auto emb = model.embed(bow);
    ASSERT_EQ(emb.size(), 3u);

    float norm = 0.0f;
    for (float x : emb) norm += x * x;
    EXPECT_TRUE(norm < 1e-5 || std::abs(norm - 1.0f) < 1e-5);
}

TEST(EmbeddingModelTest, SaveAndLoad) {
    std::string path = "test_model_tmp.bin";

    EmbeddingModel original;
    original.init_random(10, 8, 4, 42);

    std::vector<float> bow(10, 0.0f);
    bow[2] = 1.0f;
    auto emb_orig = original.embed(bow);

    ASSERT_TRUE(original.save(path));

    EmbeddingModel loaded;
    ASSERT_TRUE(loaded.load(path, 10));
    EXPECT_EQ(loaded.dim(), 4u);

    auto emb_loaded = loaded.embed(bow);
    ASSERT_EQ(emb_loaded.size(), emb_orig.size());
    for (size_t i = 0; i < emb_orig.size(); i++) {
        EXPECT_NEAR(emb_loaded[i], emb_orig[i], 1e-6);
    }

    std::filesystem::remove(path);
}

TEST(EmbeddingModelTest, DifferentInputsDifferentOutputs) {
    EmbeddingModel model;
    model.init_random(10, 8, 4, 42);

    std::vector<float> bow1(10, 0.0f);
    bow1[0] = 1.0f;
    std::vector<float> bow2(10, 0.0f);
    bow2[5] = 1.0f;

    auto emb1 = model.embed(bow1);
    auto emb2 = model.embed(bow2);

    bool different = false;
    for (size_t i = 0; i < emb1.size(); i++) {
        if (std::abs(emb1[i] - emb2[i]) > 1e-6) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
}

// --- Hybrid Builder Tests ---

class HybridBuilderTest : public ::testing::Test {
protected:
    std::string seg_dir = "test_hybrid_seg_tmp";

    void SetUp() override {
        std::filesystem::remove_all(seg_dir);

        SegmentWriter writer(seg_dir);
        writer.add_document("database sql relational query index",
                            {"database", "sql", "relational", "query", "index"});
        writer.add_document("algorithm sorting quicksort graph dijkstra",
                            {"algorithm", "sorting", "quicksort", "graph", "dijkstra"});
        writer.add_document("network tcp ip router firewall",
                            {"network", "tcp", "ip", "router", "firewall"});
        writer.add_document("stockholm sweden capital archipelago",
                            {"stockholm", "sweden", "capital", "archipelago"});
        writer.finalize();
    }

    void TearDown() override {
        std::filesystem::remove_all(seg_dir);
    }
};

TEST_F(HybridBuilderTest, BuildCreatesEmbeddings) {
    SegmentReader reader(seg_dir);

    Vocabulary vocab;
    vocab.build_from_terms(reader.all_terms());

    EmbeddingModel model;
    model.init_random(vocab.size(), 8, 4, 42);

    HNSWIndex hnsw(4, 4, 50, 50);

    HybridBuilder builder(reader, model, vocab, hnsw);
    builder.build();

    EXPECT_EQ(hnsw.size(), 4u);
}

TEST_F(HybridBuilderTest, EmbeddingsAreSearchable) {
    SegmentReader reader(seg_dir);

    Vocabulary vocab;
    vocab.build_from_terms(reader.all_terms());

    EmbeddingModel model;
    model.init_random(vocab.size(), 8, 4, 42);

    HNSWIndex hnsw(4, 4, 50, 50);

    HybridBuilder builder(reader, model, vocab, hnsw);
    builder.build();

    auto& vec = hnsw.get_vector(0);
    auto results = hnsw.search(vec, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 0u);
}

// --- Hybrid Search Tests ---

class HybridSearchTest : public ::testing::Test {
protected:
    std::string seg_dir = "test_hybridsearch_tmp";

    void SetUp() override {
        std::filesystem::remove_all(seg_dir);

        SegmentWriter writer(seg_dir);
        writer.add_document("database sql relational query optimization",
                            {"database", "sql", "relational", "query", "optimization"});
        writer.add_document("algorithm sorting quicksort mergesort complexity",
                            {"algorithm", "sorting", "quicksort", "mergesort", "complexity"});
        writer.add_document("network tcp ip router firewall protocol",
                            {"network", "tcp", "ip", "router", "firewall", "protocol"});
        writer.add_document("security encryption malware authentication",
                            {"security", "encryption", "malware", "authentication"});
        writer.add_document("stockholm sweden capital spotify",
                            {"stockholm", "sweden", "capital", "spotify"});
        writer.finalize();
    }

    void TearDown() override {
        std::filesystem::remove_all(seg_dir);
    }
};

TEST_F(HybridSearchTest, ReturnsResults) {
    SegmentReader reader(seg_dir);

    Vocabulary vocab;
    vocab.build_from_terms(reader.all_terms());

    EmbeddingModel model;
    model.init_random(vocab.size(), 8, 4, 42);

    HNSWIndex hnsw(4, 4, 50, 50);
    HybridBuilder builder(reader, model, vocab, hnsw);
    builder.build();

    HybridSearch hs(reader, model, vocab, hnsw);
    auto results = hs.search("database sql", 10, 10, false);

    ASSERT_FALSE(results.empty());
    bool has_bm25 = false;
    for (auto& r : results) {
        if (r.bm25 > 0.0) has_bm25 = true;
    }
    EXPECT_TRUE(has_bm25);
}

TEST_F(HybridSearchTest, CombinesBM25AndANN) {
    SegmentReader reader(seg_dir);

    Vocabulary vocab;
    vocab.build_from_terms(reader.all_terms());

    EmbeddingModel model;
    model.init_random(vocab.size(), 8, 4, 42);

    HNSWIndex hnsw(4, 4, 50, 50);
    HybridBuilder builder(reader, model, vocab, hnsw);
    builder.build();

    HybridSearch hs(reader, model, vocab, hnsw);
    auto results = hs.search("algorithm sorting", 10, 10, false);

    bool has_ann = false;
    for (auto& r : results) {
        if (r.ann > 0.0) has_ann = true;
    }
    EXPECT_TRUE(has_ann);
}

TEST_F(HybridSearchTest, ScoresDescending) {
    SegmentReader reader(seg_dir);

    Vocabulary vocab;
    vocab.build_from_terms(reader.all_terms());

    EmbeddingModel model;
    model.init_random(vocab.size(), 8, 4, 42);

    HNSWIndex hnsw(4, 4, 50, 50);
    HybridBuilder builder(reader, model, vocab, hnsw);
    builder.build();

    HybridSearch hs(reader, model, vocab, hnsw);
    auto results = hs.search("network firewall security", 10, 10, false);

    for (size_t i = 1; i < results.size(); i++) {
        EXPECT_GE(results[i - 1].score, results[i].score);
    }
}

TEST_F(HybridSearchTest, NonexistentQuery) {
    SegmentReader reader(seg_dir);

    Vocabulary vocab;
    vocab.build_from_terms(reader.all_terms());

    EmbeddingModel model;
    model.init_random(vocab.size(), 8, 4, 42);

    HNSWIndex hnsw(4, 4, 50, 50);
    HybridBuilder builder(reader, model, vocab, hnsw);
    builder.build();

    HybridSearch hs(reader, model, vocab, hnsw);
    auto results = hs.search("zzzzz", 10, 10, false);

    for (auto& r : results) {
        EXPECT_DOUBLE_EQ(r.bm25, 0.0);
    }
}

TEST_F(HybridSearchTest, CrossTopicSearch) {
    SegmentReader reader(seg_dir);

    Vocabulary vocab;
    vocab.build_from_terms(reader.all_terms());

    EmbeddingModel model;
    model.init_random(vocab.size(), 8, 4, 42);

    HNSWIndex hnsw(4, 4, 50, 50);
    HybridBuilder builder(reader, model, vocab, hnsw);
    builder.build();

    HybridSearch hs(reader, model, vocab, hnsw);
    // "stockholm" only in doc5 via BM25, but ANN may surface others
    auto results = hs.search("stockholm", 10, 10, false);

    ASSERT_FALSE(results.empty());
    // The top BM25 result should be doc5
    bool found_stockholm = false;
    for (auto& r : results) {
        if (r.doc == 5 && r.bm25 > 0.0) found_stockholm = true;
    }
    EXPECT_TRUE(found_stockholm);
}
