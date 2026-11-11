#include "canlog/dbc.hpp"

#include <fstream>
#include <optional>
#include <regex>
#include <sstream>

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

}  // namespace

Database Database::parse_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ParseError("cannot open DBC file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_string(ss.str());
}

Database Database::parse_string(const std::string& text) {
    // BO_ <id> <name>: <dlc> <sender>
    static const std::regex bo_re(
        R"RE(^\s*BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+)\s*$)RE");
    // SG_ <name> [M|m<N>] : <start>|<len>@<endian><sign> (<factor>,<offset>) [<min>|<max>] "<unit>" <receivers>
    static const std::regex sg_re(
        R"RE(^\s*SG_\s+(\w+)\s*(M|m\d+)?\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*\(\s*([^,\s]+)\s*,\s*([^)\s]+)\s*\)\s*\[\s*([^|\s]+)\s*\|\s*([^\]\s]+)\s*\]\s*"([^"]*)".*$)RE");
    // VAL_ <id> <signal> <value> "<text>" ... ;
    static const std::regex val_re(R"RE(^\s*VAL_\s+(\d+)\s+(\w+)\s+(.*);\s*$)RE");
    static const std::regex val_pair_re(R"RE((-?\d+)\s+"([^"]*)")RE");

    Database db;
    std::optional<size_t> current;   // index of the message that SG_ lines belong to

    std::istringstream in(text);
    std::string line;
    size_t line_no = 0;

    while (std::getline(in, line)) {
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::smatch m;

        if (line.find("BO_") != std::string::npos && std::regex_match(line, m, bo_re)) {
            current.reset();
            try {
                const uint64_t raw_id = std::stoull(m[1].str());
                if (raw_id == kIndependentSigMsg) continue;   // orphan signals: skip them

                Message msg;
                msg.extended = (raw_id & kExtendedFlag) != 0;
                msg.id = static_cast<uint32_t>(raw_id & kIdMask);
                msg.name = m[2].str();
                msg.dlc = static_cast<unsigned>(std::stoul(m[3].str()));
                msg.sender = m[4].str();

                const uint64_t key = index_key(msg.id, msg.extended);
                if (db.index_.count(key)) fail(line_no, "duplicate message id " + m[1].str());
                db.index_[key] = db.messages_.size();
                current = db.messages_.size();
                db.messages_.push_back(std::move(msg));
            } catch (const std::logic_error&) {   // stoull/stoul: invalid_argument, out_of_range
                fail(line_no, "bad number in message definition");
            }
            continue;
        }

        // Only look at lines that start with SG_ / VAL_ so unrelated sections
        // (comments, attributes, ...) are ignored cheaply.
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;

        if (line.compare(first, 3, "SG_") == 0 && line.compare(first, 7, "SG_MUL_") != 0) {
            if (!current) continue;   // signal outside any message
            if (!std::regex_match(line, m, sg_re)) fail(line_no, "cannot parse signal: " + line);
            try {
                Signal sig;
                sig.name = m[1].str();
                const std::string mux = m[2].str();
                if (mux == "M") {
                    sig.mux_role = MuxRole::Selector;
                } else if (!mux.empty()) {
                    sig.mux_role = MuxRole::Muxed;
                    sig.mux_value = static_cast<uint32_t>(std::stoul(mux.substr(1)));
                }
                sig.start_bit = static_cast<unsigned>(std::stoul(m[3].str()));
                sig.length = static_cast<unsigned>(std::stoul(m[4].str()));
                sig.little_endian = (m[5].str() == "1");
                sig.is_signed = (m[6].str() == "-");
                sig.factor = std::stod(m[7].str());
                sig.offset = std::stod(m[8].str());
                sig.minimum = std::stod(m[9].str());
                sig.maximum = std::stod(m[10].str());
                sig.unit = m[11].str();
                db.messages_[*current].signal_defs.push_back(std::move(sig));
            } catch (const std::logic_error&) {
                fail(line_no, "bad number in signal definition: " + line);
            }
            continue;
        }

        if (line.compare(first, 4, "VAL_") == 0 && std::regex_match(line, m, val_re)) {
            try {
                const uint64_t raw_id = std::stoull(m[1].str());
                const auto it = db.index_.find(index_key(static_cast<uint32_t>(raw_id & kIdMask),
                                                         (raw_id & kExtendedFlag) != 0));
                if (it == db.index_.end()) continue;
                const std::string signal_name = m[2].str();
                const std::string pairs = m[3].str();   // must outlive the regex iterator

                for (Signal& sig : db.messages_[it->second].signal_defs) {
                    if (sig.name != signal_name) continue;
                    for (auto p = std::sregex_iterator(pairs.begin(), pairs.end(), val_pair_re);
                         p != std::sregex_iterator(); ++p) {
                        sig.value_table[std::stoll((*p)[1].str())] = (*p)[2].str();
                    }
                    break;
                }
            } catch (const std::logic_error&) {
                fail(line_no, "bad number in value table: " + line);
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
