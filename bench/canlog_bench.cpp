// canlog_bench: throughput of log parsing and signal decoding.
//
//   canlog_bench [--dbc <file.dbc>] [--frames <N>] [--repeats <R>]
//
// Always benchmark an optimized build (Release); the program warns otherwise.
// Frames are synthetic (deterministic pseudo-random payloads cycling through the
// five messages of data/sample.dbc), so results are comparable between runs and
// machines. Each stage is run R times and the fastest run is reported, which is
// the least noisy estimate of what the code can do.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "canlog/dbc.hpp"
#include "canlog/decoder.hpp"
#include "canlog/logreader.hpp"

#ifndef CANLOG_DATA_DIR
#define CANLOG_DATA_DIR "data"
#endif

namespace {

using Clock = std::chrono::steady_clock;

// xorshift64: tiny, deterministic, good enough for payload bytes.
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
};

std::vector<std::string> make_lines(size_t n) {
    static const struct {
        uint32_t id;
        bool ext;
        int dlc;
    } kMessages[] = {
        {0x100, false, 8}, {0x200, false, 8}, {0x300, false, 4}, {0x400, false, 8},
        {0x0CF00400, true, 8},
    };

    Rng rng;
    std::vector<std::string> lines;
    lines.reserve(n);
    char buf[96];
    for (size_t i = 0; i < n; ++i) {
        const auto& m = kMessages[i % 5];
        char payload[17] = {};
        for (int b = 0; b < m.dlc; ++b) {
            std::snprintf(payload + 2 * b, 3, "%02X", static_cast<unsigned>(rng.next() & 0xFF));
        }
        std::snprintf(buf, sizeof buf, "(%.6f) can0 %0*X#%s", 1700000000.0 + static_cast<double>(i) * 0.001,
                      m.ext ? 8 : 3, static_cast<unsigned>(m.id), payload);
        lines.emplace_back(buf);
    }
    return lines;
}

double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

void report(const char* what, size_t items, const char* unit, double best_seconds) {
    std::printf("%-22s %9.2f M %s/s   %8.1f ns/%s\n", what, static_cast<double>(items) / best_seconds / 1e6,
                unit, best_seconds / static_cast<double>(items) * 1e9, unit);
}

}  // namespace

int main(int argc, char** argv) {
    std::string dbc_path = std::string(CANLOG_DATA_DIR) + "/sample.dbc";
    size_t frames_n = 1000000;
    int repeats = 5;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dbc" && i + 1 < argc) {
            dbc_path = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            frames_n = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--repeats" && i + 1 < argc) {
            repeats = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "usage: %s [--dbc file.dbc] [--frames N] [--repeats R]\n", argv[0]);
            return 2;
        }
    }
    if (frames_n == 0 || repeats < 1) {
        std::fprintf(stderr, "error: --frames and --repeats must be positive\n");
        return 2;
    }

    canlog::Database db;
    try {
        db = canlog::Database::parse_file(dbc_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }

#ifndef NDEBUG
    std::printf("WARNING: assertions are on (not a Release build); numbers will be far too low.\n");
#endif

    const std::vector<std::string> lines = make_lines(frames_n);

    // ---- stage 1: parse candump -L text into CanFrame
    std::vector<canlog::CanFrame> frames;
    frames.reserve(frames_n);
    double best_parse = 1e30;
    uint64_t parse_check = 0;
    for (int r = 0; r < repeats; ++r) {
        frames.clear();
        const auto t0 = Clock::now();
        for (const std::string& line : lines) {
            if (auto f = canlog::parse_candump_line(line)) frames.push_back(*f);
        }
        best_parse = std::min(best_parse, seconds_since(t0));
        parse_check = frames.size();
    }
    if (parse_check != frames_n) {
        std::fprintf(stderr, "error: only %llu of %llu lines parsed\n",
                     static_cast<unsigned long long>(parse_check), static_cast<unsigned long long>(frames_n));
        return 1;
    }

    // ---- stage 2: decode frames with the DBC
    double best_decode = 1e30;
    uint64_t signal_count = 0;
    double checksum = 0.0;   // consumed below so the optimizer cannot drop the work
    for (int r = 0; r < repeats; ++r) {
        signal_count = 0;
        checksum = 0.0;
        const auto t0 = Clock::now();
        for (const canlog::CanFrame& f : frames) {
            if (const auto d = canlog::decode_frame(db, f)) {
                signal_count += d->signal_values.size();
                for (const auto& s : d->signal_values) checksum += s.physical;
            }
        }
        best_decode = std::min(best_decode, seconds_since(t0));
    }

    std::printf("frames: %zu   repeats: %d (fastest run reported)   dbc: %s\n", frames_n, repeats,
                dbc_path.c_str());
    report("parse (text -> frame)", frames_n, "frame", best_parse);
    report("decode (frame -> sigs)", frames_n, "frame", best_decode);
    std::printf("%-22s %9.2f M signals/s\n", "", static_cast<double>(signal_count) / best_decode / 1e6);
    std::printf("(decoded %llu signals, checksum %.6g)\n", static_cast<unsigned long long>(signal_count),
                checksum);
    return 0;
}
