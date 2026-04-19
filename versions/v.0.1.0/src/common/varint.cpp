#include "varint.h"

namespace varint {

void encode_u32(uint32_t v, std::vector<uint8_t>& out) {
    while (v >= 0x80) {
        out.push_back(static_cast<uint8_t>(v | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<uint8_t>(v));
}

uint32_t decode_u32(const std::vector<uint8_t>& in, size_t& pos) {
    uint32_t result = 0;
    int shift = 0;

    while (pos < in.size()) {
        uint8_t b = in[pos++];
        result |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return result;
}

}