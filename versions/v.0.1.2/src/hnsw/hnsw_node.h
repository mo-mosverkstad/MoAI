#pragma once
#include <vector>
#include <cstdint>

struct HNSWNode {
    uint32_t id;          // 节点 ID
    uint8_t level;        // 节点所在最高层
    // 多层邻居结构：neighbors[level] = vector<neighbor_id>
    std::vector<std::vector<uint32_t>> neighbors;
};