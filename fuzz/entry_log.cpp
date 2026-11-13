// libFuzzer entry point for the candump log parser (clang only; see CMakeLists.txt).
#include <cstddef>
#include <cstdint>

#include "fuzz_targets.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    canlog_fuzz::fuzz_log(data, size);
    return 0;
}
