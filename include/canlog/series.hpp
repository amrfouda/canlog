#pragma once

#include <string>
#include <vector>

#include "canlog/dbc.hpp"
#include "canlog/decoder.hpp"
#include "canlog/frame.hpp"

namespace canlog {

struct SeriesPoint {
    double t = 0.0;       // seconds since the first frame of the input vector
    double value = 0.0;   // physical value (after factor/offset)
};

// Look up a signal definition by message name and signal name; nullptr if absent.
const Signal* find_signal(const Database& db, const std::string& message, const std::string& signal);

// Decode one signal of one message across a whole capture, for plotting/export.
// Multiplexed signals only produce points for frames whose selector matches.
// Returns an empty vector if the message/signal is unknown or `frames` is empty.
std::vector<SeriesPoint> extract_series(const std::vector<CanFrame>& frames, const Database& db,
                                        const std::string& message, const std::string& signal);

}  // namespace canlog
