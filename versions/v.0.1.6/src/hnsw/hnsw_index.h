#pragma once
#include <vector>
#include <queue>
#include <random>
#include <limits>
#include "hnsw_node.h"

/**
 * Full HNSW implementation (L2 distance).
 * Supports:
 *  - multilayer graph
 *  - efConstruction
 *  - efSearch
 *  - M and M0 (M0 = 2*M)
 *  - Greedy search at upper layers
 *  - Beam search at layer 0
 */
class HNSWIndex {
public:
    HNSWIndex(uint32_t dim, uint32_t M = 16, uint32_t efConstruction = 200,
              uint32_t efSearch = 100);

    uint32_t add_point(const std::vector<float>& vec);
    std::vector<uint32_t> search(const std::vector<float>& q, size_t topK) const;

    size_t size() const { return data_.size(); }
    const std::vector<float>& get_vector(uint32_t id) const { return data_[id]; }

private:
    float distance(const std::vector<float>& a,
                   const std::vector<float>& b) const;

    uint8_t sample_level();
    void connect_new_point(uint32_t pid, uint8_t level);
    std::vector<uint32_t> search_layer(const std::vector<float>& q,
                                       uint32_t enter,
                                       uint8_t level,
                                       uint32_t ef) const;
    std::vector<uint32_t> select_neighbors_heuristic(
                                const std::vector<float>& query,
                                const std::vector<uint32_t>& candidates,
                                size_t M) const;

private:
    uint32_t dim_;
    uint32_t M_;
    uint32_t M0_;
    uint32_t efConstruction_;
    uint32_t efSearch_;

    int enterpoint_;
    uint8_t maxLevel_;

    std::vector<std::vector<float>> data_;
    std::vector<HNSWNode> nodes_;

    mutable std::mt19937 rng_;
};
