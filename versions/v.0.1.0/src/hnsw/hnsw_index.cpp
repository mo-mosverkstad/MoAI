#include "hnsw_index.h"
#include <algorithm>
#include <cmath>

HNSWIndex::HNSWIndex(uint32_t dim, uint32_t M, uint32_t efConstruction,
                     uint32_t efSearch)
    : dim_(dim), M_(M), M0_(2 * M), efConstruction_(efConstruction),
      efSearch_(efSearch), enterpoint_(-1), maxLevel_(0)
{
    std::random_device rd;
    rng_.seed(rd());
}

float HNSWIndex::distance(const std::vector<float>& a,
                          const std::vector<float>& b) const
{
    float s = 0.0f;
    for (uint32_t i = 0; i < dim_; i++) {
        float d = a[i] - b[i];
        s += d * d;
    }
    return s; // squared L2 (avoids sqrt for comparison)
}

uint8_t HNSWIndex::sample_level() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float r = dist(rng_);
    float mL = 1.0f / std::log(static_cast<float>(M_));
    uint8_t lvl = static_cast<uint8_t>(-std::log(r) * mL);
    return lvl;
}

uint32_t HNSWIndex::add_point(const std::vector<float>& vec) {
    uint32_t pid = static_cast<uint32_t>(data_.size());
    data_.push_back(vec);

    uint8_t level = sample_level();

    HNSWNode node;
    node.id = pid;
    node.level = level;
    node.neighbors.resize(level + 1);
    nodes_.push_back(node);

    if (enterpoint_ == -1) {
        enterpoint_ = pid;
        maxLevel_ = level;
        return pid;
    }

    connect_new_point(pid, level);

    if (level > maxLevel_) {
        maxLevel_ = level;
        enterpoint_ = pid;
    }

    return pid;
}

void HNSWIndex::connect_new_point(uint32_t pid, uint8_t level) {
    uint32_t ep = static_cast<uint32_t>(enterpoint_);

    // Greedy descent from maxLevel_ down to level+1
    for (int l = static_cast<int>(maxLevel_); l > static_cast<int>(level); l--) {
        if (l > static_cast<int>(nodes_[ep].level)) continue;
        auto res = search_layer(data_[pid], ep, static_cast<uint8_t>(l), 1);
        if (!res.empty()) ep = res.front();
    }

    // Insert at layers [min(level, maxLevel_) ... 0]
    uint8_t start = std::min(level, maxLevel_);
    for (int l = static_cast<int>(start); l >= 0; l--) {
        if (l > static_cast<int>(nodes_[ep].level)) continue;

        auto neighbors = search_layer(data_[pid], ep, static_cast<uint8_t>(l), efConstruction_);
        size_t maxM = (l == 0) ? M0_ : M_;
        auto selected = select_neighbors_heuristic(data_[pid], neighbors, maxM);

        // Bidirectional links
        for (uint32_t n : selected) {
            nodes_[pid].neighbors[l].push_back(n);

            if (static_cast<int>(nodes_[n].level) >= l) {
                nodes_[n].neighbors[l].push_back(pid);

                // Prune if over capacity
                if (nodes_[n].neighbors[l].size() > maxM) {
                    auto pruned = select_neighbors_heuristic(
                        data_[n], nodes_[n].neighbors[l], maxM);
                    nodes_[n].neighbors[l] = std::move(pruned);
                }
            }
        }

        if (!neighbors.empty()) ep = neighbors.front();
    }
}

std::vector<uint32_t> HNSWIndex::search_layer(
    const std::vector<float>& q, uint32_t enter,
    uint8_t level, uint32_t ef) const
{
    struct Cand {
        float d;
        uint32_t id;
    };
    auto cmp_max = [](const Cand& a, const Cand& b) { return a.d < b.d; };
    auto cmp_min = [](const Cand& a, const Cand& b) { return a.d > b.d; };

    // top = max-heap (farthest on top, for bounding)
    std::priority_queue<Cand, std::vector<Cand>, decltype(cmp_max)> top(cmp_max);
    // cand = min-heap (closest on top, for expansion)
    std::priority_queue<Cand, std::vector<Cand>, decltype(cmp_min)> cand(cmp_min);

    float d0 = distance(q, data_[enter]);
    top.push({d0, enter});
    cand.push({d0, enter});

    std::vector<char> visited(data_.size(), 0);
    visited[enter] = 1;

    while (!cand.empty()) {
        auto c = cand.top();
        if (c.d > top.top().d) break;
        cand.pop();

        if (level > nodes_[c.id].level) continue;

        for (uint32_t nb : nodes_[c.id].neighbors[level]) {
            if (visited[nb]) continue;
            visited[nb] = 1;

            float d = distance(q, data_[nb]);
            if (top.size() < ef || d < top.top().d) {
                cand.push({d, nb});
                top.push({d, nb});
                if (top.size() > ef) top.pop();
            }
        }
    }

    // Extract results sorted by distance (closest first)
    std::vector<std::pair<float, uint32_t>> sorted;
    sorted.reserve(top.size());
    while (!top.empty()) {
        sorted.push_back({top.top().d, top.top().id});
        top.pop();
    }
    std::sort(sorted.begin(), sorted.end(),
              [](auto& a, auto& b) { return a.first < b.first; });

    std::vector<uint32_t> result;
    result.reserve(sorted.size());
    for (auto& [d, id] : sorted) result.push_back(id);
    return result;
}

std::vector<uint32_t>
HNSWIndex::select_neighbors_heuristic(
    const std::vector<float>& query,
    const std::vector<uint32_t>& candidates,
    size_t M) const
{
    if (candidates.size() <= M) return candidates;

    std::vector<std::pair<float, uint32_t>> scored;
    scored.reserve(candidates.size());
    for (uint32_t c : candidates) {
        scored.push_back({distance(query, data_[c]), c});
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.first < b.first; });

    std::vector<uint32_t> result;
    result.reserve(M);
    for (size_t i = 0; i < M && i < scored.size(); i++) {
        result.push_back(scored[i].second);
    }
    return result;
}

std::vector<uint32_t> HNSWIndex::search(const std::vector<float>& q, size_t topK) const {
    if (enterpoint_ == -1) return {};

    uint32_t ep = static_cast<uint32_t>(enterpoint_);

    // Greedy descent through upper layers
    for (int l = static_cast<int>(maxLevel_); l > 0; l--) {
        if (l > static_cast<int>(nodes_[ep].level)) continue;
        auto res = search_layer(q, ep, static_cast<uint8_t>(l), 1);
        if (!res.empty()) ep = res.front();
    }

    // Beam search at layer 0
    uint32_t ef = std::max(static_cast<uint32_t>(topK), efSearch_);
    auto results = search_layer(q, ep, 0, ef);
    if (results.size() > topK) results.resize(topK);
    return results;
}
