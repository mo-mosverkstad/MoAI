#include <gtest/gtest.h>
#include "common/varint.h"

TEST(Varint, RoundTripZero) {
    std::vector<uint8_t> buf;
    varint::encode_u32(0, buf);
    size_t pos = 0;
    EXPECT_EQ(varint::decode_u32(buf, pos), 0u);
    EXPECT_EQ(pos, buf.size());
}

TEST(Varint, RoundTripSmall) {
    std::vector<uint8_t> buf;
    varint::encode_u32(42, buf);
    size_t pos = 0;
    EXPECT_EQ(varint::decode_u32(buf, pos), 42u);
}

TEST(Varint, RoundTripLarge) {
    std::vector<uint8_t> buf;
    varint::encode_u32(123456, buf);
    size_t pos = 0;
    EXPECT_EQ(varint::decode_u32(buf, pos), 123456u);
}

TEST(Varint, RoundTripMax) {
    std::vector<uint8_t> buf;
    varint::encode_u32(UINT32_MAX, buf);
    size_t pos = 0;
    EXPECT_EQ(varint::decode_u32(buf, pos), UINT32_MAX);
}

TEST(Varint, MultipleValuesSequential) {
    std::vector<uint8_t> buf;
    varint::encode_u32(1, buf);
    varint::encode_u32(300, buf);
    varint::encode_u32(100000, buf);

    size_t pos = 0;
    EXPECT_EQ(varint::decode_u32(buf, pos), 1u);
    EXPECT_EQ(varint::decode_u32(buf, pos), 300u);
    EXPECT_EQ(varint::decode_u32(buf, pos), 100000u);
    EXPECT_EQ(pos, buf.size());
}

TEST(Varint, SingleByteEncoding) {
    // Values < 128 should encode to exactly 1 byte
    std::vector<uint8_t> buf;
    varint::encode_u32(127, buf);
    EXPECT_EQ(buf.size(), 1u);
}

TEST(Varint, TwoByteEncoding) {
    // 128 requires 2 bytes in varint
    std::vector<uint8_t> buf;
    varint::encode_u32(128, buf);
    EXPECT_EQ(buf.size(), 2u);
    size_t pos = 0;
    EXPECT_EQ(varint::decode_u32(buf, pos), 128u);
}
