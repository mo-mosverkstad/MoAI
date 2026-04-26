#pragma once
#include <cstdint>
#include <vector>

namespace varint {

void encode_u32(uint32_t v, std::vector<uint8_t>& out);
uint32_t decode_u32(const std::vector<uint8_t>& in, size_t& pos);

}