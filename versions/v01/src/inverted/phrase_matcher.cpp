#include "phrase_matcher.h"
#include <algorithm>

bool PhraseMatcher::match(
    const std::vector<std::vector<uint32_t>>& pos_lists
) {
    if (pos_lists.empty()) return false;

    // For phrase [t1, t2, t3], we need:
    // exists p in pos_lists[0] such that:
    //   (p+1) in pos_lists[1] and (p+2) in pos_lists[2]
    const auto& first = pos_lists[0];

    for (uint32_t p : first) {
        bool ok = true;
        for (size_t i = 1; i < pos_lists.size(); i++) {
            uint32_t needed = p + i;
            const auto& plist = pos_lists[i];

            if (!std::binary_search(plist.begin(), plist.end(), needed)) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }

    return false;
}