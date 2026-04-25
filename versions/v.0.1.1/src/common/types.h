#pragma once
#include <cstdint>

using DocID = uint32_t;      // 单 segment 内 docID
using TermID = uint32_t;
using Offset = uint64_t;     // 文件偏移