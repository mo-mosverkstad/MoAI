#include <gtest/gtest.h>
#include <filesystem>
#include "storage/segment_writer.h"
#include "storage/segment_reader.h"
#include "inverted/search_engine.h"

class SearchEngineTest : public ::testing::Test {
protected:
    std::string test_dir = "test_search_tmp";

    void SetUp() override {
        std::filesystem::remove_all(test_dir);

        SegmentWriter writer(test_dir);
        // doc1: databases topic
        writer.add_document("database sql relational tables index query",
                            {"database", "sql", "relational", "tables", "index", "query"});
        // doc2: algorithms topic
        writer.add_document("algorithm sorting quicksort mergesort graph dijkstra",
                            {"algorithm", "sorting", "quicksort", "mergesort", "graph", "dijkstra"});
        // doc3: networking topic
        writer.add_document("network tcp ip router firewall protocol",
                            {"network", "tcp", "ip", "router", "firewall", "protocol"});
        // doc4: security topic
        writer.add_document("security encryption malware firewall authentication",
                            {"security", "encryption", "malware", "firewall", "authentication"});
        // doc5: stockholm
        writer.add_document("stockholm sweden capital archipelago spotify",
                            {"stockholm", "sweden", "capital", "archipelago", "spotify"});
        // doc6: AI topic
        writer.add_document("artificial intelligence machine learning deep neural",
                            {"artificial", "intelligence", "machine", "learning", "deep", "neural"});
        writer.finalize();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(SearchEngineTest, SingleTermSearch) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    auto results = engine.search("database", 10);
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(results[0].first, 1u);
}

TEST_F(SearchEngineTest, ANDQuery) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    // "firewall" is in doc3 (networking) and doc4 (security)
    // "security" is only in doc4
    auto results = engine.search("firewall AND security", 10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].first, 4u);
}

TEST_F(SearchEngineTest, ORQuery) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    // "database" in doc1, "algorithm" in doc2
    auto results = engine.search("database OR algorithm", 10);
    ASSERT_EQ(results.size(), 2u);

    std::vector<DocID> docs;
    for (auto& [d, s] : results) docs.push_back(d);
    EXPECT_NE(std::find(docs.begin(), docs.end(), 1u), docs.end());
    EXPECT_NE(std::find(docs.begin(), docs.end(), 2u), docs.end());
}

TEST_F(SearchEngineTest, NOTQuery) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    // "firewall" in doc3 and doc4; "security" only in doc4
    auto results = engine.search("firewall NOT security", 10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].first, 3u);
}

TEST_F(SearchEngineTest, PhraseQueryPositionAware) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    // "machine learning" as phrase: machine at pos 3, learning at pos 4 in doc6
    auto results = engine.search("\"machine learning\"", 10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].first, 6u);
}

TEST_F(SearchEngineTest, PhraseQueryNoMatch) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    // "learning machine" reversed order - should NOT match doc6
    // doc6 has: artificial(0) intelligence(1) machine(2) learning(3) deep(4) neural(5)
    // Wait - "learning machine" needs learning at pos N, machine at N+1
    // doc6: learning=3, machine=2 -> no match (machine before learning)
    auto results = engine.search("\"learning machine\"", 10);
    EXPECT_TRUE(results.empty());
}

TEST_F(SearchEngineTest, PhraseORTerm) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    // "machine learning" phrase matches doc6; "database" matches doc1
    auto results = engine.search("\"machine learning\" OR database", 10);
    ASSERT_EQ(results.size(), 2u);

    std::vector<DocID> docs;
    for (auto& [d, s] : results) docs.push_back(d);
    EXPECT_NE(std::find(docs.begin(), docs.end(), 1u), docs.end());
    EXPECT_NE(std::find(docs.begin(), docs.end(), 6u), docs.end());
}

TEST_F(SearchEngineTest, NonexistentTerm) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    auto results = engine.search("zzzzz", 10);
    EXPECT_TRUE(results.empty());
}

TEST_F(SearchEngineTest, TopKLimit) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    // "firewall" in doc3 and doc4
    auto results = engine.search("firewall", 1);
    EXPECT_EQ(results.size(), 1u);
}

TEST_F(SearchEngineTest, ScoresDescending) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    auto results = engine.search("firewall OR database OR algorithm", 10);
    for (size_t i = 1; i < results.size(); i++) {
        EXPECT_GE(results[i - 1].second, results[i].second);
    }
}

TEST_F(SearchEngineTest, CrossTopicAND) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    // "tcp" only in doc3, "sql" only in doc1 -> AND should be empty
    auto results = engine.search("tcp AND sql", 10);
    EXPECT_TRUE(results.empty());
}

TEST_F(SearchEngineTest, SwedishCitySearch) {
    SegmentReader reader(test_dir);
    SearchEngine engine(reader);

    auto results = engine.search("stockholm", 10);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].first, 5u);
}
