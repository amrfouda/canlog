#pragma once

#include <optional>
#include <vector>

#include "canlog/frame.hpp"

namespace canlog {

// Anything that produces CAN frames one at a time: a log file today, a live
// SocketCAN interface later. The GUI and the CLI only depend on this interface.
class CanSource {
public:
    virtual ~CanSource() = default;

    // Next frame, or nullopt when the source is exhausted (or closed).
    virtual std::optional<CanFrame> next() = 0;
};

// Drain a finite source (e.g. a log file) into a vector.
inline std::vector<CanFrame> read_all(CanSource& source) {
    std::vector<CanFrame> frames;
    while (auto frame = source.next()) frames.push_back(*frame);
    return frames;
}

}  // namespace canlog
