#include "canlog/format.hpp"

#include <algorithm>

namespace canlog {

std::string frame_data_hex(const CanFrame& frame) {
    static const char kDigits[] = "0123456789ABCDEF";
    const size_t n = std::min<size_t>(frame.dlc, frame.data.size());

    std::string out;
    out.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        if (i != 0) out += ' ';
        out += kDigits[frame.data[i] >> 4];
        out += kDigits[frame.data[i] & 0x0F];
    }
    return out;
}

}  // namespace canlog
