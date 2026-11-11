#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "canlog/dbc.hpp"
#include "canlog/frame.hpp"

namespace canlog {

struct DecodedSignal {
    std::string name;
    int64_t raw = 0;          // raw integer (sign-extended for signed signals)
    double physical = 0.0;    // raw * factor + offset
    std::string unit;
    std::string label;        // value-table text ("Running"), empty if none
};

struct DecodedMessage {
    const Message* message = nullptr;   // points into the Database that was used
    std::vector<DecodedSignal> signal_values;
};

// Extract the raw, unsigned bit pattern of `sig` from a payload.
// Returns nullopt if the signal does not fit inside `size` bytes.
std::optional<uint64_t> extract_raw(const Signal& sig, const uint8_t* data, size_t size);

// extract_raw + sign extension + factor/offset + value-table lookup.
std::optional<DecodedSignal> decode_signal(const Signal& sig, const uint8_t* data, size_t size);

// Decode every signal of the matching message. Returns nullopt when the frame
// is an RTR frame or its identifier is not in the database. Signals that do not
// fit in the frame's DLC, or whose multiplexer value does not match, are skipped.
std::optional<DecodedMessage> decode_frame(const Database& db, const CanFrame& frame);

}  // namespace canlog
