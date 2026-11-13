#include "fuzz_targets.hpp"

#include <sstream>
#include <string>
#include <string_view>

#include "canlog/dbc.hpp"
#include "canlog/decoder.hpp"
#include "canlog/logreader.hpp"

namespace canlog_fuzz {
namespace {

// Keeps the optimizer from deleting decode results nobody reads.
volatile double g_sink = 0.0;

void decode_and_consume(const canlog::Database& db, const canlog::CanFrame& frame) {
    if (const auto decoded = canlog::decode_frame(db, frame)) {
        for (const auto& s : decoded->signal_values) g_sink = g_sink + s.physical;
    }
}

const canlog::Database& reference_db() {
    static const canlog::Database db = canlog::Database::parse_string(R"DBC(
BO_ 256 EngineData: 8 Engine
 SG_ EngineSpeed : 0|16@1+ (0.25,0) [0|16383.75] "rpm" Gateway
 SG_ CoolantTemp : 16|8@1+ (1,-40) [-40|215] "degC" Gateway
BO_ 512 VehicleDynamics: 8 Gateway
 SG_ VehicleSpeed : 7|16@0+ (0.01,0) [0|250] "km/h" Engine
 SG_ SteeringAngle : 23|16@0- (0.1,0) [-780|780] "deg" Engine
BO_ 2364539904 EEC1: 8 Engine
 SG_ EngineTorque : 16|8@1+ (1,-125) [-125|125] "%" Gateway
)DBC");
    return db;
}

}  // namespace

void fuzz_dbc(const uint8_t* data, size_t size) {
    const std::string text(reinterpret_cast<const char*>(data), size);

    canlog::Database db;
    try {
        db = canlog::Database::parse_string(text);
    } catch (const canlog::ParseError&) {
        return;   // the documented way to reject bad input
    }

    size_t visited = 0;
    for (const canlog::Message& msg : db.messages()) {
        if (++visited > 256) break;

        canlog::CanFrame frame;
        frame.id = msg.id;
        frame.extended = msg.extended;
        for (size_t i = 0; i < frame.data.size(); ++i) frame.data[i] = static_cast<uint8_t>(0xA5 ^ (i * 37));

        for (const uint8_t dlc : {uint8_t{0}, uint8_t{3}, uint8_t{8}}) {
            frame.dlc = dlc;
            decode_and_consume(db, frame);
        }
        (void)db.find(msg.id, msg.extended);
    }
}

void fuzz_log(const uint8_t* data, size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    // One line at a time...
    if (const auto frame = canlog::parse_candump_line(text)) {
        decode_and_consume(reference_db(), *frame);
    }

    // ...and as a multi-line stream, which also covers the getline/skip bookkeeping.
    std::istringstream in{std::string(text)};
    canlog::LogReader reader(in);
    size_t frames = 0;
    while (const auto frame = reader.next()) {
        decode_and_consume(reference_db(), *frame);
        if (++frames > 10000) break;
    }
}

}  // namespace canlog_fuzz
