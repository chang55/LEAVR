#include "media/frame_utils.h"

namespace leavr {
namespace {

size_t StartCodeSize(const uint8_t* data, size_t size, size_t pos) {
    if (pos + 3 <= size && data[pos] == 0 && data[pos + 1] == 0 && data[pos + 2] == 1) {
        return 3;
    }
    if (pos + 4 <= size && data[pos] == 0 && data[pos + 1] == 0 &&
        data[pos + 2] == 0 && data[pos + 3] == 1) {
        return 4;
    }
    return 0;
}

} // namespace

std::vector<NaluView> SplitAnnexBNalus(const uint8_t* data, size_t size) {
    std::vector<NaluView> nalus;
    if (!data || size == 0) return nalus;

    size_t pos = 0;
    while (pos < size && StartCodeSize(data, size, pos) == 0) ++pos;
    while (pos < size) {
        const size_t prefix = StartCodeSize(data, size, pos);
        if (prefix == 0) break;
        const size_t start = pos + prefix;
        size_t next = start;
        while (next < size && StartCodeSize(data, size, next) == 0) ++next;
        if (next > start) nalus.push_back({data + start, next - start});
        pos = next;
    }
    return nalus;
}

} // namespace leavr
