#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace canlog {

enum class MuxRole { None, Selector, Muxed };

struct Signal {
    std::string name;
    unsigned start_bit = 0;       // DBC start bit: LSB for Intel, MSB for Motorola
    unsigned length = 0;          // in bits
    bool little_endian = true;    // "@1" = Intel, "@0" = Motorola
    bool is_signed = false;       // "-" = signed, "+" = unsigned
    double factor = 1.0;
    double offset = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    std::string unit;
    MuxRole mux_role = MuxRole::None;
    uint32_t mux_value = 0;       // only meaningful when mux_role == Muxed
    std::map<int64_t, std::string> value_table;   // from VAL_ lines
};

struct Message {
    uint32_t id = 0;              // identifier without the DBC extended-frame flag
    bool extended = false;
    std::string name;
    unsigned dlc = 0;
    std::string sender;
    // NOTE: deliberately not called `signals` -- Qt defines that as a macro
    // (`#define signals public`) and it would break this header in the GUI.
    std::vector<Signal> signal_defs;
};

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Database {
public:
    // Throw ParseError on I/O problems or malformed BO_/SG_ lines.
    static Database parse_file(const std::string& path);
    static Database parse_string(const std::string& text);

    // nullptr if the identifier is not in the database.
    const Message* find(uint32_t id, bool extended) const;

    const std::vector<Message>& messages() const { return messages_; }

private:
    std::vector<Message> messages_;
    std::map<uint64_t, size_t> index_;   // (extended << 32 | id) -> index in messages_
};

}  // namespace canlog
