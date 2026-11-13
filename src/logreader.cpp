#include "canlog/logreader.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <istream>
#include <string>
#include <string_view>

namespace canlog {
namespace {

// Same whitespace set as the C locale's isspace().
bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool all_hex(std::string_view s) {
    for (char c : s) {
        if (hex_value(c) < 0) return false;
    }
    return true;
}

// Next whitespace-delimited token starting at `p`; advances `p` past it.
bool next_token(const char*& p, const char* end, std::string_view& token) {
    while (p != end && is_space(*p)) ++p;
    if (p == end) return false;
    const char* start = p;
    while (p != end && !is_space(*p)) ++p;
    token = std::string_view(start, static_cast<size_t>(p - start));
    return true;
}

// "(1700000000.123456)" -> 1700000000.123456
bool parse_timestamp(std::string_view token, double& out) {
    if (token.size() < 3 || token.front() != '(' || token.back() != ')') return false;
    const std::string_view inner = token.substr(1, token.size() - 2);

    char buf[64];   // real timestamps are ~17 characters
    if (inner.size() >= sizeof buf) return false;
    std::memcpy(buf, inner.data(), inner.size());
    buf[inner.size()] = '\0';

    char* parsed_end = nullptr;
    errno = 0;
    const double value = std::strtod(buf, &parsed_end);
    if (parsed_end == buf || errno == ERANGE) return false;
    out = value;
    return true;
}

}  // namespace

std::optional<CanFrame> parse_candump_line(std::string_view line) {
    const char* p = line.data();
    const char* const end = p + line.size();

    std::string_view ts, iface, frame_str;
    if (!next_token(p, end, ts) || !next_token(p, end, iface) || !next_token(p, end, frame_str)) {
        return std::nullopt;   // anything after the third token is ignored
    }

    CanFrame f;
    if (!parse_timestamp(ts, f.timestamp)) return std::nullopt;
    f.iface.assign(iface.data(), iface.size());

    // "<id>#<payload>"
    const size_t hash = frame_str.find('#');
    if (hash == std::string_view::npos) return std::nullopt;
    const std::string_view id_str = frame_str.substr(0, hash);
    const std::string_view payload = frame_str.substr(hash + 1);

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
    uint32_t id = 0;
    for (char c : id_str) id = (id << 4) | static_cast<uint32_t>(hex_value(c));
    f.id = id;
    if (!f.extended && f.id > 0x7FF) return std::nullopt;
    if (f.extended && f.id > 0x1FFFFFFF) return std::nullopt;

    // Remote frame: "R" or "R<dlc>".
    if (!payload.empty() && payload[0] == 'R') {
        f.rtr = true;
        if (payload.size() == 2) {
            if (payload[1] < '0' || payload[1] > '9') return std::nullopt;
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
        f.data[i] = static_cast<uint8_t>((hex_value(payload[2 * i]) << 4) | hex_value(payload[2 * i + 1]));
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
