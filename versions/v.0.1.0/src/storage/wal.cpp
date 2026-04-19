#include "wal.h"
#include "../common/file_utils.h"
#include <vector>

WAL::WAL(const std::string& path)
    : path_(path)
{}

void WAL::log(const std::string& entry) {
    std::vector<uint8_t> buf(entry.begin(), entry.end());
    buf.push_back('\n');
    fileutils::append_file(path_, buf);
}