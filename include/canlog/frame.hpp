#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace canlog {

// One classic CAN frame (up to 8 data bytes). CAN FD is on the roadmap.
struct CanFrame {
    double timestamp = 0.0;   // seconds, as written by `candump -L`
    std::string iface;        // "can0", "vcan0", ...
    uint32_t id = 0;          // 11- or 29-bit identifier, flag bits stripped
    bool extended = false;    // true for 29-bit identifiers
    bool rtr = false;         // remote transmission request (no payload)
    uint8_t dlc = 0;          // number of valid bytes in `data`
    std::array<uint8_t, 8> data{};
};

}  // namespace canlog
