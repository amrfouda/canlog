#pragma once

#include <cstddef>
#include <cstdint>

// Entry points shared by the libFuzzer binaries (entry_*.cpp) and the standalone
// mutation fuzzer (mutation_fuzz.cpp). Each one must never crash, hang, trigger a
// sanitizer report, or let an exception other than canlog::ParseError escape,
// whatever bytes it is given.
namespace canlog_fuzz {

// DBC text -> Database::parse_string, then decode a frame for every parsed message,
// so arbitrary signal layouts (huge start bits, odd lengths, NaN factors...) hit the decoder.
void fuzz_dbc(const uint8_t* data, size_t size);

// candump -L text -> parse_candump_line and LogReader, then decode the frames found.
void fuzz_log(const uint8_t* data, size_t size);

}  // namespace canlog_fuzz
