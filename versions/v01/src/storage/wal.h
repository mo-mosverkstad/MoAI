#pragma once
#include <string>

/**
 * Write‑ahead log (WAL) for durability.
 * MVP version simply appends text lines.
 */
class WAL {
public:
    explicit WAL(const std::string& path);

    /** Append a single log line (automatically ends with '\n') */
    void log(const std::string& entry);

private:
    std::string path_;
};