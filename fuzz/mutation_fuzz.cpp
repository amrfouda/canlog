// Standalone mutation fuzzer: no libFuzzer needed, works with GCC, Clang and MSVC.
//
//   canlog_mutation_fuzz [--iterations N] [--seed S] [--target dbc|log|all]
//                        [--dbc seed.dbc] [--log seed.log]
//
// Start from seed inputs (a DBC file, lines of a log), apply random mutations
// (byte flips, inserts, deletes, splices, huge numbers, very long lines) and feed
// the result to the same entry points the libFuzzer binaries use. Build with
// -fsanitize=address,undefined to turn silent memory errors into failures.
//
// The run is deterministic for a given --seed. If something crashes under GCC or
// Clang, the input that caused it is saved to crash-input.bin in the working directory.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "fuzz_targets.hpp"

#if defined(__GNUC__) || defined(__clang__)
// Provided by the sanitizer runtimes; null (weak) when no sanitizer is linked in.
extern "C" void __sanitizer_set_death_callback(void (*)(void)) __attribute__((weak));
#define CANLOG_HAVE_DEATH_CALLBACK 1
#endif

namespace {

std::string g_current;   // the input being tested right now

void save_crash_input() {
    std::ofstream out("crash-input.bin", std::ios::binary);
    out.write(g_current.data(), static_cast<std::streamsize>(g_current.size()));
    std::fprintf(stderr, "\ncrash input (%zu bytes) saved to crash-input.bin\n", g_current.size());
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "error: cannot read %s\n", path.c_str());
        std::exit(2);
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const std::vector<std::string>& dictionary() {
    static const std::vector<std::string> words = {
        "18446744073709551615", "18446744073709551616", "9223372036854775807", "4294967295",
        "4294967296", "2147483648", "-2147483649", "-1", "0", "64", "65", "63", "1e999", "-1e999",
        "1e-999", "nan", "inf", "-inf", "0x7FF", "99999999999999999999999999",
        "|", "@", "@0+", "@1-", "(1e308,1e308)", "(0,0)", "[0|0]", "\"\"", "\"", ":", ";", "#", "##",
        "BO_ ", "SG_ ", "VAL_ ", "m0", "m99999999999", "M", "\r\n", "\n", "\t", std::string("\0", 1), "R", "R9",
        "3221225472", "2147483648", "SG_MUL_VAL_ ",
    };
    return words;
}

void mutate(std::string& s, std::mt19937_64& rng, const std::vector<std::string>& pool) {
    const int edits = 1 + static_cast<int>(rng() % 6);
    for (int e = 0; e < edits; ++e) {
        const size_t pos = s.empty() ? 0 : rng() % (s.size() + 1);
        switch (rng() % 9) {
        case 0:   // flip one bit
            if (!s.empty()) s[pos % s.size()] ^= static_cast<char>(1u << (rng() % 8));
            break;
        case 1:   // random byte
            if (!s.empty()) s[pos % s.size()] = static_cast<char>(rng() & 0xFF);
            break;
        case 2:   // insert random byte
            s.insert(pos, 1, static_cast<char>(rng() & 0xFF));
            break;
        case 3:   // delete a short run
            if (!s.empty()) s.erase(pos % s.size(), 1 + rng() % 8);
            break;
        case 4:   // insert a dictionary word (huge numbers, keywords, separators)
            s.insert(pos, dictionary()[rng() % dictionary().size()]);
            break;
        case 5:   // duplicate a chunk
            if (!s.empty()) {
                const size_t from = rng() % s.size();
                const size_t len = 1 + rng() % 64;
                s.insert(pos, s.substr(from, len));
            }
            break;
        case 6:   // splice in part of another seed
            if (!pool.empty()) {
                const std::string& other = pool[rng() % pool.size()];
                if (!other.empty()) {
                    const size_t from = rng() % other.size();
                    s.insert(pos, other.substr(from, 1 + rng() % 128));
                }
            }
            break;
        case 7:   // very long run of one character (stress regex/stack depth)
            s.insert(pos, std::string(1 + rng() % 20000, "0 9a#,|\n"[rng() % 8]));
            break;
        default:  // truncate
            if (!s.empty()) s.resize(pos % s.size());
            break;
        }
    }
    if (s.size() > (1u << 16)) s.resize(1u << 16);
}

std::vector<std::string> log_seeds_from(const std::string& text) {
    std::vector<std::string> seeds;
    std::istringstream in(text);
    std::string line, joined;
    int n = 0;
    while (std::getline(in, line)) {
        seeds.push_back(line);
        joined += line + "\n";
        if (++n % 3 == 0) {
            seeds.push_back(joined);
            joined.clear();
        }
    }
    for (const char* s : {"(1.0) can0 123#R", "(1.0) can0 123#R4", "(1.0) can0 123#",
                          "(2.5) vcan1 0CF00400#FFFFFF00", "(3.5) can0 7ff#0102030405060708\r",
                          "(1.0) can0 123##1AABB", "  (1.0)\tcan0   123#AA  trailing junk"}) {
        seeds.emplace_back(s);
    }
    return seeds;
}

}  // namespace

int main(int argc, char** argv) {
    size_t iterations = 200000;
    uint64_t seed = 1;
    std::string target = "all";
    std::string dbc_path, log_path;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", arg.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--iterations") iterations = static_cast<size_t>(std::strtoull(value().c_str(), nullptr, 10));
        else if (arg == "--seed") seed = std::strtoull(value().c_str(), nullptr, 10);
        else if (arg == "--target") target = value();
        else if (arg == "--dbc") dbc_path = value();
        else if (arg == "--log") log_path = value();
        else {
            std::fprintf(stderr,
                         "usage: %s [--iterations N] [--seed S] [--target dbc|log|all] "
                         "[--dbc seed.dbc] [--log seed.log]\n",
                         argv[0]);
            return 2;
        }
    }
    if (target != "dbc" && target != "log" && target != "all") {
        std::fprintf(stderr, "error: --target must be dbc, log or all\n");
        return 2;
    }

#ifdef CANLOG_HAVE_DEATH_CALLBACK
    if (__sanitizer_set_death_callback) __sanitizer_set_death_callback(save_crash_input);
#endif

    std::vector<std::string> dbc_seeds;
    if (!dbc_path.empty()) dbc_seeds.push_back(read_file(dbc_path));
    dbc_seeds.push_back("BO_ 256 M: 8 A\n SG_ S : 0|16@1+ (0.5,10) [0|100] \"km/h\" B\nVAL_ 256 S 0 \"Off\" ;\n");
    dbc_seeds.push_back("BO_ 2364539904 E: 8 A\n SG_ X : 7|16@0- (1,0) [0|0] \"\" B\n");
    const std::vector<std::string> log_seeds =
        log_seeds_from(log_path.empty() ? std::string() : read_file(log_path));

    std::mt19937_64 rng(seed);
    const bool do_dbc = target != "log";
    const bool do_log = target != "dbc";
    double slowest = 0.0;
    const auto start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < iterations; ++i) {
        const bool use_dbc = do_dbc && (!do_log || i % 2 == 0);
        const std::vector<std::string>& pool = use_dbc ? dbc_seeds : log_seeds;

        g_current = pool[rng() % pool.size()];
        mutate(g_current, rng, pool);

        const auto t0 = std::chrono::steady_clock::now();
        const auto* bytes = reinterpret_cast<const uint8_t*>(g_current.data());
        if (use_dbc) canlog_fuzz::fuzz_dbc(bytes, g_current.size());
        else canlog_fuzz::fuzz_log(bytes, g_current.size());
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        if (took > slowest) {
            slowest = took;
            if (took > 1.0) {
                std::fprintf(stderr, "slow input at iteration %zu: %.2f s for %zu bytes (%s)\n", i, took,
                             g_current.size(), use_dbc ? "dbc" : "log");
                save_crash_input();   // keep it so the slowdown can be reproduced
            }
        }
        if (iterations >= 10 && (i + 1) % (iterations / 10) == 0) {
            std::fprintf(stderr, "  %zu / %zu iterations\n", i + 1, iterations);
        }
    }

    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("ok: %zu iterations (seed %llu, target %s) in %.1f s, slowest single input %.3f s\n", iterations,
                static_cast<unsigned long long>(seed), target.c_str(), total, slowest);
    return 0;
}
