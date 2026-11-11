// canlog_cli: decode a candump -L log with a DBC file and print the signals.
//
//   canlog_cli --dbc data/sample.dbc --log data/sample.log
//   canlog_cli --dbc data/sample.dbc --log data/sample.log --message EngineData --csv out.csv

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "canlog/dbc.hpp"
#include "canlog/decoder.hpp"
#include "canlog/logreader.hpp"

namespace {

struct Options {
    std::string dbc;
    std::string log;
    std::string csv;
    std::string message;   // optional filter by message name
};

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0
              << " --dbc <file.dbc> --log <candump.log> [--message <NAME>] [--csv <out.csv>]\n"
                 "\n"
                 "  --dbc      DBC database describing the messages and signals\n"
                 "  --log      log written by `candump -L <iface>`\n"
                 "  --message  only print this message (by DBC name)\n"
                 "  --csv      additionally write every decoded signal to a CSV file\n";
}

bool parse_args(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        if (arg == "--dbc") {
            if (!value(opt.dbc)) return false;
        } else if (arg == "--log") {
            if (!value(opt.log)) return false;
        } else if (arg == "--csv") {
            if (!value(opt.csv)) return false;
        } else if (arg == "--message") {
            if (!value(opt.message)) return false;
        } else {
            return false;
        }
    }
    return !opt.dbc.empty() && !opt.log.empty();
}

std::string format_signal(const canlog::DecodedSignal& s) {
    std::ostringstream os;
    os << std::setprecision(8) << s.name << '=';
    if (!s.label.empty()) {
        os << s.label;
    } else {
        os << s.physical;
        if (!s.unit.empty()) os << ' ' << s.unit;
    }
    return os.str();
}

std::string format_timestamp(double t) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6f", t);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        usage(argv[0]);
        return 2;
    }

    canlog::Database db;
    try {
        db = canlog::Database::parse_file(opt.dbc);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }

    std::ifstream log_in(opt.log);
    if (!log_in) {
        std::cerr << "error: cannot open log file: " << opt.log << '\n';
        return 1;
    }

    std::ofstream csv;
    if (!opt.csv.empty()) {
        csv.open(opt.csv);
        if (!csv) {
            std::cerr << "error: cannot write CSV file: " << opt.csv << '\n';
            return 1;
        }
        csv << std::setprecision(10) << "timestamp,message,signal,value,unit\n";
    }

    canlog::LogReader reader(log_in);
    std::map<std::pair<uint32_t, bool>, size_t> unknown_ids;   // (id, extended) -> count
    size_t total = 0, decoded = 0, rtr = 0;
    std::optional<double> t0;

    while (auto frame = reader.next()) {
        ++total;
        if (!t0) t0 = frame->timestamp;

        if (frame->rtr) {
            ++rtr;
            continue;
        }
        const auto result = canlog::decode_frame(db, *frame);
        if (!result) {
            ++unknown_ids[{frame->id, frame->extended}];
            continue;
        }
        if (!opt.message.empty() && result->message->name != opt.message) continue;
        ++decoded;

        char head[128];
        std::snprintf(head, sizeof head, "%9.3fs  %-6s 0x%-8X %-16s",
                      frame->timestamp - *t0, frame->iface.c_str(), frame->id,
                      result->message->name.c_str());
        std::cout << head;
        for (const auto& s : result->signal_values) std::cout << "  " << format_signal(s);
        std::cout << '\n';

        if (csv.is_open()) {
            for (const auto& s : result->signal_values) {
                csv << format_timestamp(frame->timestamp) << ',' << result->message->name << ','
                    << s.name << ',' << s.physical << ',' << s.unit << '\n';
            }
        }
    }

    std::cout << "\n--- summary ---\n"
              << "frames read      : " << total << '\n'
              << "frames decoded   : " << decoded << '\n'
              << "remote frames    : " << rtr << '\n'
              << "unparsable lines : " << reader.skipped_lines() << '\n';

    size_t unknown_total = 0;
    for (const auto& kv : unknown_ids) unknown_total += kv.second;
    std::cout << "unknown ids      : " << unknown_total << " frames";
    if (!unknown_ids.empty()) {
        std::cout << "  (";
        bool first = true;
        for (const auto& kv : unknown_ids) {
            if (!first) std::cout << ", ";
            first = false;
            std::cout << "0x" << std::hex << std::uppercase << kv.first.first << std::dec
                      << (kv.first.second ? " ext" : "") << " x" << kv.second;
        }
        std::cout << ')';
    }
    std::cout << '\n';
    return 0;
}
