#ifndef LEAVR_MEDIA_FRAME_UTILS_H
#define LEAVR_MEDIA_FRAME_UTILS_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace leavr {

struct NaluView {
    const uint8_t* data;
    size_t size;
};

std::vector<NaluView> SplitAnnexBNalus(const uint8_t* data, size_t size);

} // namespace leavr

#endif
