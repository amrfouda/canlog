#include "canlog/series.hpp"

namespace canlog {

const Signal* find_signal(const Database& db, const std::string& message, const std::string& signal) {
    for (const Message& msg : db.messages()) {
        if (msg.name != message) continue;
        for (const Signal& sig : msg.signal_defs) {
            if (sig.name == signal) return &sig;
        }
        return nullptr;
    }
    return nullptr;
}

std::vector<SeriesPoint> extract_series(const std::vector<CanFrame>& frames, const Database& db,
                                        const std::string& message, const std::string& signal) {
    std::vector<SeriesPoint> out;
    if (frames.empty()) return out;

    const Message* msg = nullptr;
    for (const Message& m : db.messages()) {
        if (m.name == message) {
            msg = &m;
            break;
        }
    }
    if (!msg) return out;

    const double t0 = frames.front().timestamp;
    for (const CanFrame& frame : frames) {
        if (frame.id != msg->id || frame.extended != msg->extended) continue;
        const auto decoded = decode_frame(db, frame);
        if (!decoded) continue;
        for (const DecodedSignal& s : decoded->signal_values) {
            if (s.name == signal) {
                out.push_back({frame.timestamp - t0, s.physical});
                break;
            }
        }
    }
    return out;
}

}  // namespace canlog
