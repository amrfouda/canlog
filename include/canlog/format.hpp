#pragma once

#include <string>

#include "canlog/frame.hpp"

namespace canlog {

// Payload as upper-case hex bytes separated by spaces, e.g. "80 0C 5A".
// Only the first `dlc` bytes are printed; an empty payload gives "".
std::string frame_data_hex(const CanFrame& frame);

}  // namespace canlog
