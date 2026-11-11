#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>

#include "canlog/frame.hpp"
#include "canlog/source.hpp"

namespace canlog {

// Parse one line of `candump -L` output, for example
//   (1700000000.123456) can0 123#11223344
//   (1700000000.123456) can0 0CF00400#FFFFFF00
//   (1700000000.123456) can0 123#R
// Returns nullopt for blank, malformed or unsupported (CAN FD) lines.
std::optional<CanFrame> parse_candump_line(const std::string& line);

// Streams frames out of a candump -L log. Unparsable lines are skipped and
// counted; blank lines are ignored silently.
class LogReader : public CanSource {
public:
    explicit LogReader(std::istream& in) : in_(in) {}

    std::optional<CanFrame> next() override;
    size_t skipped_lines() const { return skipped_; }

private:
    std::istream& in_;
    size_t skipped_ = 0;
};

}  // namespace canlog
