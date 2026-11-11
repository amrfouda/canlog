#include "canlog/logreader.hpp"

#include <cctype>
#include <istream>
#include <sstream>

namespace canlog {
namespace {

bool all_hex(const std::string& s) {
    for (unsigned char c : s) {
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

}  // namespace

std::optional<CanFrame> parse_candump_line(const std::string& line) {
    std::istringstream ss(line);
    std::string ts, iface, frame_str;
    if (!(ss >> ts >> iface >> frame_str)) return std::nullopt;

    // "(1700000000.123456)"
    if (ts.size() < 3 || ts.front() != '(' || ts.back() != ')') return std::nullopt;

    CanFrame f;
    try {
        f.timestamp = std::stod(ts.substr(1, ts.size() - 2));
    } catch (const std::logic_error&) {
        return std::nullopt;
    }
    f.iface = iface;

    // "<id>#<payload>"
    const size_t hash = frame_str.find('#');
    if (hash == std::string::npos) return std::nullopt;
    const std::string id_str = frame_str.substr(0, hash);
    const std::string payload = frame_str.substr(hash + 1);

    if (!payload.empty() && payload[0] == '#') return std::nullopt;   // "123##..." = CAN FD, not supported yet

    // candump -L writes 3 hex digits for 11-bit ids and 8 for 29-bit ids.
    if (id_str.size() == 3) {
        f.extended = false;
    } else if (id_str.size() == 8) {
        f.extended = true;
    } else {
        return std::nullopt;
    }
    if (!all_hex(id_str)) return std::nullopt;
    f.id = static_cast<uint32_t>(std::stoul(id_str, nullptr, 16));
    if (!f.extended && f.id > 0x7FF) return std::nullopt;
    if (f.extended && f.id > 0x1FFFFFFF) return std::nullopt;

    // Remote frame: "R" or "R<dlc>".
    if (!payload.empty() && payload[0] == 'R') {
        f.rtr = true;
        if (payload.size() == 2) {
            if (!std::isdigit(static_cast<unsigned char>(payload[1]))) return std::nullopt;
            const int dlc = payload[1] - '0';
            if (dlc > 8) return std::nullopt;
            f.dlc = static_cast<uint8_t>(dlc);
        } else if (payload.size() != 1) {
            return std::nullopt;
        }
        return f;
    }

    if (payload.size() % 2 != 0 || payload.size() > 16 || !all_hex(payload)) return std::nullopt;
    f.dlc = static_cast<uint8_t>(payload.size() / 2);
    for (size_t i = 0; i < f.dlc; ++i) {
        f.data[i] = static_cast<uint8_t>(std::stoul(payload.substr(2 * i, 2), nullptr, 16));
    }
    return f;
}

std::optional<CanFrame> LogReader::next() {
    std::string line;
    while (std::getline(in_, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        if (auto frame = parse_candump_line(line)) return frame;
        ++skipped_;
    }
    return std::nullopt;
}

}  // namespace canlog
