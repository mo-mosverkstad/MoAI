#include <gtest/gtest.h>
#include <cmath>
#include <numeric>
#include <algorithm>
#include "hnsw/hnsw_index.h"

class HNSWTest : public ::testing::Test {
protected:
    // Brute-force kNN for ground truth
    std::vector<uint32_t> brute_force_knn(
        const std::vector<std::vector<float>>& data,
        const std::vector<float>& q, size_t k)
    {
        std::vector<std::pair<float, uint32_t>> dists;
        for (uint32_t i = 0; i < data.size(); i++) {
            float d = 0;
            for (size_t j = 0; j < q.size(); j++) {
                float diff = q[j] - data[i][j];
                d += diff * diff;
            }
            dists.push_back({d, i});
        }
        std::sort(dists.begin(), dists.end());
        std::vector<uint32_t> result;
        for (size_t i = 0; i < k && i < dists.size(); i++)
            result.push_back(dists[i].second);
        return result;
    }
};

TEST_F(HNSWTest, EmptyIndex) {
    HNSWIndex idx(3);
    auto results = idx.search({1.0f, 2.0f, 3.0f}, 5);
    EXPECT_TRUE(results.empty());
}

TEST_F(HNSWTest, SinglePoint) {
    HNSWIndex idx(3);
    idx.add_point({1.0f, 2.0f, 3.0f});

    auto results = idx.search({1.0f, 2.0f, 3.0f}, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 0u);
}

TEST_F(HNSWTest, ExactMatch) {
    HNSWIndex idx(2, 4, 50, 50);
    idx.add_point({0.0f, 0.0f});
    idx.add_point({1.0f, 0.0f});
    idx.add_point({0.0f, 1.0f});
    idx.add_point({1.0f, 1.0f});

    auto results = idx.search({1.0f, 0.0f}, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 1u);
}

TEST_F(HNSWTest, TopKOrdering) {
    HNSWIndex idx(2, 4, 50, 50);
    // Points at increasing distance from origin
    idx.add_point({0.0f, 0.0f});  // 0: dist=0
    idx.add_point({1.0f, 0.0f});  // 1: dist=1
    idx.add_point({2.0f, 0.0f});  // 2: dist=4
    idx.add_point({3.0f, 0.0f});  // 3: dist=9
    idx.add_point({10.0f, 0.0f}); // 4: dist=100

    auto results = idx.search({0.0f, 0.0f}, 3);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0], 0u);
    EXPECT_EQ(results[1], 1u);
    EXPECT_EQ(results[2], 2u);
}

TEST_F(HNSWTest, TopKLargerThanDataset) {
    HNSWIndex idx(2, 4, 50, 50);
    idx.add_point({0.0f, 0.0f});
    idx.add_point({1.0f, 1.0f});

    auto results = idx.search({0.0f, 0.0f}, 10);
    EXPECT_EQ(results.size(), 2u);
}

TEST_F(HNSWTest, SizeTracking) {
    HNSWIndex idx(3);
    EXPECT_EQ(idx.size(), 0u);
    idx.add_point({1.0f, 2.0f, 3.0f});
    EXPECT_EQ(idx.size(), 1u);
    idx.add_point({4.0f, 5.0f, 6.0f});
    EXPECT_EQ(idx.size(), 2u);
}

TEST_F(HNSWTest, RecallOnRandomData) {
    const uint32_t dim = 8;
    const uint32_t N = 500;
    const size_t topK = 10;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    std::vector<std::vector<float>> data;
    HNSWIndex idx(dim, 16, 200, 100);

    for (uint32_t i = 0; i < N; i++) {
        std::vector<float> v(dim);
        for (auto& x : v) x = dist(rng);
        data.push_back(v);
        idx.add_point(v);
    }

    // Run multiple queries and check recall
    int total_correct = 0;
    int total_queries = 20;

    for (int q = 0; q < total_queries; q++) {
        std::vector<float> query(dim);
        for (auto& x : query) x = dist(rng);

        auto hnsw_results = idx.search(query, topK);
        auto bf_results = brute_force_knn(data, query, topK);

        // Count how many HNSW results are in the true top-K
        for (uint32_t id : hnsw_results) {
            if (std::find(bf_results.begin(), bf_results.end(), id) != bf_results.end())
                total_correct++;
        }
    }

    double recall = static_cast<double>(total_correct) / (total_queries * topK);
    // HNSW should achieve at least 80% recall with these parameters
    EXPECT_GE(recall, 0.80) << "Recall too low: " << recall;
}

TEST_F(HNSWTest, NearestNeighborIsExact) {
    const uint32_t dim = 4;
    HNSWIndex idx(dim, 8, 100, 100);

    std::mt19937 rng(123);
    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);

    std::vector<std::vector<float>> data;
    for (int i = 0; i < 200; i++) {
        std::vector<float> v(dim);
        for (auto& x : v) x = dist(rng);
        data.push_back(v);
        idx.add_point(v);
    }

    // For 10 random queries, check that the nearest neighbor is correct
    int correct = 0;
    for (int q = 0; q < 10; q++) {
        std::vector<float> query(dim);
        for (auto& x : query) x = dist(rng);

        auto hnsw_result = idx.search(query, 1);
        auto bf_result = brute_force_knn(data, query, 1);

        if (!hnsw_result.empty() && !bf_result.empty() &&
            hnsw_result[0] == bf_result[0])
            correct++;
    }
    // At least 8/10 nearest neighbors should be exact
    EXPECT_GE(correct, 8) << "Only " << correct << "/10 exact NN matches";
}

TEST_F(HNSWTest, DuplicatePoints) {
    HNSWIndex idx(2, 4, 50, 50);
    for (int i = 0; i < 10; i++) {
        idx.add_point({1.0f, 1.0f});
    }
    idx.add_point({100.0f, 100.0f});

    auto results = idx.search({1.0f, 1.0f}, 3);
    ASSERT_EQ(results.size(), 3u);
    // All 3 results should be one of the duplicate points (ids 0-9)
    for (auto id : results) {
        EXPECT_LE(id, 9u);
    }
}

TEST_F(HNSWTest, HighDimensional) {
    const uint32_t dim = 128;
    HNSWIndex idx(dim, 16, 100, 50);

    std::mt19937 rng(99);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < 100; i++) {
        std::vector<float> v(dim);
        for (auto& x : v) x = dist(rng);
        idx.add_point(v);
    }

    std::vector<float> query(dim, 0.5f);
    auto results = idx.search(query, 5);
    ASSERT_EQ(results.size(), 5u);

    // Results should be unique
    std::sort(results.begin(), results.end());
    auto it = std::unique(results.begin(), results.end());
    EXPECT_EQ(it, results.end()) << "Duplicate IDs in results";
}

TEST_F(HNSWTest, IncrementalInsert) {
    HNSWIndex idx(2, 4, 50, 50);

    // Insert points one by one and search after each
    idx.add_point({0.0f, 0.0f});
    auto r1 = idx.search({0.0f, 0.0f}, 1);
    ASSERT_EQ(r1.size(), 1u);
    EXPECT_EQ(r1[0], 0u);

    idx.add_point({10.0f, 10.0f});
    auto r2 = idx.search({9.0f, 9.0f}, 1);
    ASSERT_EQ(r2.size(), 1u);
    EXPECT_EQ(r2[0], 1u);

    idx.add_point({5.0f, 5.0f});
    auto r3 = idx.search({5.0f, 5.0f}, 1);
    ASSERT_EQ(r3.size(), 1u);
    EXPECT_EQ(r3[0], 2u);
}
