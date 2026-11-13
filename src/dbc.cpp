#include "canlog/dbc.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>

// Hand-written line parser. An earlier version used std::regex, which recurses
// once per input character and overflowed the stack on long lines (for example
// a VAL_ table with thousands of entries). Everything here is iterative.

namespace canlog {
namespace {

constexpr uint64_t kExtendedFlag = 0x80000000ull;   // bit 31 of a DBC id marks a 29-bit frame
constexpr uint64_t kIdMask = 0x1FFFFFFFull;
constexpr uint64_t kIndependentSigMsg = 3221225472ull;   // Vector's pseudo-message for orphan signals

[[noreturn]] void fail(size_t line_no, const std::string& what) {
    throw ParseError("DBC line " + std::to_string(line_no) + ": " + what);
}

uint64_t index_key(uint32_t id, bool extended) {
    return (uint64_t{extended} << 32) | id;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_word(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

// ---- number conversion (all overflow-checked)

bool to_u64(std::string_view digits, uint64_t& out) {
    if (digits.empty()) return false;
    uint64_t value = 0;
    for (char c : digits) {
        if (!is_digit(c)) return false;
        const uint64_t d = static_cast<uint64_t>(c - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - d) / 10) return false;
        value = value * 10 + d;
    }
    out = value;
    return true;
}

bool to_u32(std::string_view digits, uint32_t& out) {
    uint64_t v = 0;
    if (!to_u64(digits, v) || v > std::numeric_limits<uint32_t>::max()) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool to_i64(std::string_view text, int64_t& out) {
    const bool negative = !text.empty() && text.front() == '-';
    uint64_t magnitude = 0;
    if (!to_u64(negative ? text.substr(1) : text, magnitude)) return false;
    const uint64_t limit = uint64_t{1} << 63;   // |INT64_MIN|
    if (negative ? magnitude > limit : magnitude >= limit) return false;
    out = negative ? static_cast<int64_t>(~magnitude + 1) : static_cast<int64_t>(magnitude);
    return true;
}

// Accepts what strtod accepts at the start of the token (decimal, exponent, inf/nan);
// rejects tokens with no number or whose value is out of the double range.
bool to_double(std::string_view token, double& out) {
    char buf[64];
    if (token.empty() || token.size() >= sizeof buf) return false;
    std::memcpy(buf, token.data(), token.size());
    buf[token.size()] = '\0';
    char* parsed_end = nullptr;
    errno = 0;
    const double v = std::strtod(buf, &parsed_end);
    if (parsed_end == buf || errno == ERANGE) return false;
    out = v;
    return true;
}

// ---- tiny cursor over one line

class Cursor {
public:
    explicit Cursor(std::string_view s) : s_(s) {}

    bool at_end() const { return pos_ >= s_.size(); }
    char peek() const { return s_[pos_]; }   // only call when !at_end()

    void skip_ws() {
        while (!at_end() && is_space(s_[pos_])) ++pos_;
    }
    // At least one whitespace character.
    bool skip_ws1() {
        const size_t before = pos_;
        skip_ws();
        return pos_ > before;
    }
    bool consume(char c) {
        if (at_end() || s_[pos_] != c) return false;
        ++pos_;
        return true;
    }
    template <typename Pred>
    std::string_view take_while(Pred pred) {
        const size_t start = pos_;
        while (!at_end() && pred(s_[pos_])) ++pos_;
        return s_.substr(start, pos_ - start);
    }
    std::string_view token() {   // run of non-whitespace
        return take_while([](char c) { return !is_space(c); });
    }
    std::string_view digits() { return take_while(is_digit); }
    std::string_view word() { return take_while(is_word); }
    std::string_view rest() const { return s_.substr(pos_); }

private:
    std::string_view s_;
    size_t pos_ = 0;
};

// BO_ <id> <name>: <dlc> <sender>
Message parse_message_line(size_t line_no, std::string_view line, uint64_t& raw_id_out) {
    auto bad = [&]() { fail(line_no, "cannot parse message: " + std::string(line)); };

    Cursor c(line);
    c.skip_ws();
    c.token();   // "BO_"
    if (!c.skip_ws1()) bad();
    const std::string_view id = c.digits();
    if (id.empty() || !c.skip_ws1()) bad();
    const std::string_view name = c.word();
    if (name.empty()) bad();
    c.skip_ws();
    if (!c.consume(':')) bad();
    c.skip_ws();
    const std::string_view dlc = c.digits();
    if (dlc.empty() || !c.skip_ws1()) bad();
    const std::string_view sender = c.word();
    if (sender.empty()) bad();
    c.skip_ws();
    if (!c.at_end()) bad();

    uint64_t raw_id = 0;
    uint32_t dlc_value = 0;
    if (!to_u64(id, raw_id) || !to_u32(dlc, dlc_value)) fail(line_no, "bad number in message definition");
    raw_id_out = raw_id;

    Message msg;
    msg.extended = (raw_id & kExtendedFlag) != 0;
    msg.id = static_cast<uint32_t>(raw_id & kIdMask);
    msg.name = std::string(name);
    msg.dlc = dlc_value;
    msg.sender = std::string(sender);
    return msg;
}

// SG_ <name> [M|m<N>] : <start>|<len>@<endian><sign> (<factor>,<offset>) [<min>|<max>] "<unit>" <receivers>
Signal parse_signal_line(size_t line_no, std::string_view line) {
    auto bad = [&]() { fail(line_no, "cannot parse signal: " + std::string(line)); };
    auto bad_number = [&]() { fail(line_no, "bad number in signal definition: " + std::string(line)); };

    Cursor c(line);
    c.skip_ws();
    if (c.token() != "SG_") bad();   // e.g. "SG_7name" is damaged, not a signal
    if (!c.skip_ws1()) bad();

    Signal sig;
    const std::string_view name = c.word();
    if (name.empty()) bad();
    sig.name = std::string(name);

    c.skip_ws();
    if (!c.at_end() && c.peek() == 'M') {
        c.consume('M');
        sig.mux_role = MuxRole::Selector;
    } else if (!c.at_end() && c.peek() == 'm') {
        c.consume('m');
        const std::string_view n = c.digits();
        if (!n.empty()) {
            sig.mux_role = MuxRole::Muxed;
            if (!to_u32(n, sig.mux_value)) bad_number();
        }
        // A bare "m" without a number is not a valid multiplexer marker, but real files
        // contain it (opendbc's vw_pq.dbc). cantools reads such a signal as an ordinary,
        // non-multiplexed one, and so do we.
    }
    c.skip_ws();
    if (!c.consume(':')) bad();
    c.skip_ws();

    const std::string_view start = c.digits();
    if (start.empty() || !c.consume('|')) bad();
    const std::string_view length = c.digits();
    if (length.empty() || !c.consume('@')) bad();
    if (c.at_end() || (c.peek() != '0' && c.peek() != '1')) bad();
    sig.little_endian = (c.peek() == '1');
    c.consume(c.peek());
    if (c.at_end() || (c.peek() != '+' && c.peek() != '-')) bad();
    sig.is_signed = (c.peek() == '-');
    c.consume(c.peek());

    c.skip_ws();
    if (!c.consume('(')) bad();
    c.skip_ws();
    const std::string_view factor = c.take_while([](char ch) { return ch != ',' && !is_space(ch); });
    c.skip_ws();
    if (factor.empty() || !c.consume(',')) bad();
    c.skip_ws();
    const std::string_view offset = c.take_while([](char ch) { return ch != ')' && !is_space(ch); });
    c.skip_ws();
    if (offset.empty() || !c.consume(')')) bad();

    c.skip_ws();
    if (!c.consume('[')) bad();
    c.skip_ws();
    const std::string_view minimum = c.take_while([](char ch) { return ch != '|' && !is_space(ch); });
    c.skip_ws();
    if (minimum.empty() || !c.consume('|')) bad();
    c.skip_ws();
    const std::string_view maximum = c.take_while([](char ch) { return ch != ']' && !is_space(ch); });
    c.skip_ws();
    if (maximum.empty() || !c.consume(']')) bad();

    c.skip_ws();
    if (!c.consume('"')) bad();
    const std::string_view unit = c.take_while([](char ch) { return ch != '"'; });
    if (!c.consume('"')) bad();
    // Anything after the unit (the receiver list) is ignored.

    if (!to_u32(start, sig.start_bit) || !to_u32(length, sig.length)) bad_number();
    if (!to_double(factor, sig.factor) || !to_double(offset, sig.offset) ||
        !to_double(minimum, sig.minimum) || !to_double(maximum, sig.maximum)) {
        bad_number();
    }
    sig.unit = std::string(unit);
    return sig;
}

struct ValueTableLine {
    uint64_t raw_id = 0;
    std::string signal;
    std::vector<std::pair<int64_t, std::string>> entries;
    bool number_out_of_range = false;   // reported only if the table is actually used
};

// VAL_ <id> <signal> <value> "<text>" ... ;
// Returns nullopt for lines that are not a message-signal value table (for example
// environment-variable tables), which are ignored.
std::optional<ValueTableLine> parse_value_table_line(size_t line_no, std::string_view line) {
    Cursor c(line);
    c.skip_ws();
    c.token();   // "VAL_"
    if (!c.skip_ws1()) return std::nullopt;
    const std::string_view id = c.digits();
    if (id.empty() || !c.skip_ws1()) return std::nullopt;
    const std::string_view signal = c.word();
    if (signal.empty() || !c.skip_ws1()) return std::nullopt;

    std::string_view body = c.rest();
    while (!body.empty() && is_space(body.back())) body.remove_suffix(1);
    if (body.empty() || body.back() != ';') return std::nullopt;
    body.remove_suffix(1);

    ValueTableLine out;
    if (!to_u64(id, out.raw_id)) fail(line_no, "bad number in value table: " + std::string(line));
    out.signal = std::string(signal);

    // Find every  <integer> "<text>"  pair, skipping anything else.
    size_t pos = 0;
    while (pos < body.size()) {
        const char ch = body[pos];
        if (!is_digit(ch) && ch != '-') {
            ++pos;
            continue;
        }
        size_t i = pos + 1;
        while (i < body.size() && is_digit(body[i])) ++i;
        const std::string_view number = body.substr(pos, i - pos);
        size_t j = i;
        while (j < body.size() && is_space(body[j])) ++j;
        const bool pair_ok = number != "-" && j > i && j < body.size() && body[j] == '"';
        if (!pair_ok) {
            pos = i > pos ? i : pos + 1;
            continue;
        }
        const size_t close = body.find('"', j + 1);
        if (close == std::string_view::npos) break;   // unterminated text: nothing more to read

        int64_t value = 0;
        if (!to_i64(number, value)) {
            out.number_out_of_range = true;
            break;
        }
        out.entries.emplace_back(value, std::string(body.substr(j + 1, close - j - 1)));
        pos = close + 1;
    }
    return out;
}

}  // namespace

Database Database::parse_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ParseError("cannot open DBC file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_string(ss.str());
}

Database Database::parse_string(const std::string& text) {
    Database db;
    std::optional<size_t> current;   // index of the message that SG_ lines belong to

    const std::string_view all(text);
    size_t pos = 0;
    size_t line_no = 0;

    while (pos <= all.size()) {
        const size_t newline = all.find('\n', pos);
        const size_t stop = (newline == std::string_view::npos) ? all.size() : newline;
        std::string_view line = all.substr(pos, stop - pos);
        pos = (newline == std::string_view::npos) ? all.size() + 1 : newline + 1;
        ++line_no;

        while (!line.empty() && is_space(line.back())) line.remove_suffix(1);   // also strips CR
        size_t first = 0;
        while (first < line.size() && is_space(line[first])) ++first;
        if (first == line.size()) continue;

        size_t word_end = first;
        while (word_end < line.size() && !is_space(line[word_end])) ++word_end;
        const std::string_view keyword = line.substr(first, word_end - first);

        if (keyword == "BO_") {   // not BO_TX_BU_, which is a different section
            current.reset();
            uint64_t raw_id = 0;
            Message msg = parse_message_line(line_no, line, raw_id);
            if (raw_id == kIndependentSigMsg) continue;   // Vector's pseudo-message for orphan signals

            const uint64_t key = index_key(msg.id, msg.extended);
            if (db.index_.count(key)) fail(line_no, "duplicate message id " + std::to_string(msg.id));
            db.index_[key] = db.messages_.size();
            current = db.messages_.size();
            db.messages_.push_back(std::move(msg));
        } else if (keyword.substr(0, 3) == "SG_" && keyword.substr(0, 7) != "SG_MUL_") {
            // Anything starting with SG_ is a signal line; a damaged one is an error, not skipped.
            // (SG_MUL_VAL_ is the extended-multiplexing section, which is not supported.)
            if (!current) continue;   // signal outside any message
            db.messages_[*current].signal_defs.push_back(parse_signal_line(line_no, line));
        } else if (keyword == "VAL_") {
            const auto table = parse_value_table_line(line_no, line);
            if (!table) continue;
            const auto it = db.index_.find(index_key(static_cast<uint32_t>(table->raw_id & kIdMask),
                                                     (table->raw_id & kExtendedFlag) != 0));
            if (it == db.index_.end()) continue;
            for (Signal& sig : db.messages_[it->second].signal_defs) {
                if (sig.name != table->signal) continue;
                if (table->number_out_of_range) fail(line_no, "bad number in value table: " + std::string(line));
                for (const auto& entry : table->entries) sig.value_table[entry.first] = entry.second;
                break;
            }
        }
    }
    return db;
}

const Message* Database::find(uint32_t id, bool extended) const {
    const auto it = index_.find(index_key(id, extended));
    return it == index_.end() ? nullptr : &messages_[it->second];
}

}  // namespace canlog
