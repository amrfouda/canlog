// Minimal dependency-free test runner (swap for GoogleTest/Catch2 later if you like).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "canlog/dbc.hpp"
#include "canlog/decoder.hpp"
#include "canlog/logreader.hpp"

#ifndef CANLOG_DATA_DIR
#error "CANLOG_DATA_DIR must be defined by the build system"
#endif

namespace {

int g_checks = 0;
int g_failures = 0;

}  // namespace

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) {                                                               \
            ++g_failures;                                                            \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        ++g_checks;                                                                  \
        const double va_ = (a), vb_ = (b);                                           \
        if (!(std::fabs(va_ - vb_) <= (eps))) {                                      \
            ++g_failures;                                                            \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_NEAR failed: " #a   \
                      << " = " << va_ << ", expected " << vb_ << "\n";                \
        }                                                                            \
    } while (0)

namespace {

using namespace canlog;

Signal make_signal(unsigned start, unsigned len, bool little, bool is_signed,
                   double factor = 1.0, double offset = 0.0) {
    Signal s;
    s.name = "S";
    s.start_bit = start;
    s.length = len;
    s.little_endian = little;
    s.is_signed = is_signed;
    s.factor = factor;
    s.offset = offset;
    return s;
}

// ---------------------------------------------------------------- bit extraction

void test_intel_extraction() {
    {   // 16 bit, byte aligned: bytes 40 1F -> 0x1F40
        const uint8_t d[8] = {0x40, 0x1F, 0, 0, 0, 0, 0, 0};
        const auto r = extract_raw(make_signal(0, 16, true, false), d, 8);
        CHECK(r.has_value());
        CHECK(r && *r == 0x1F40u);
    }
    {   // unaligned: bits 4..11 = high nibble of byte0 (F) + low nibble of byte1 (A)
        const uint8_t d[8] = {0xF0, 0x0A, 0, 0, 0, 0, 0, 0};
        const auto r = extract_raw(make_signal(4, 8, true, false), d, 8);
        CHECK(r && *r == 0xAFu);
    }
    {   // single bit
        const uint8_t d[8] = {0x00, 0x10, 0, 0, 0, 0, 0, 0};
        const auto r = extract_raw(make_signal(12, 1, true, false), d, 8);
        CHECK(r && *r == 1u);
    }
    {   // full 64 bits
        const uint8_t d[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        const auto r = extract_raw(make_signal(0, 64, true, false), d, 8);
        CHECK(r && *r == 0x0807060504030201ull);
    }
}

void test_motorola_extraction() {
    {   // start bit 7 = MSB of byte 0; 16 bits -> plain big-endian 0x1234
        const uint8_t d[8] = {0x12, 0x34, 0, 0, 0, 0, 0, 0};
        const auto r = extract_raw(make_signal(7, 16, false, false), d, 8);
        CHECK(r && *r == 0x1234u);
    }
    {   // start bit 3, 4 bits: low nibble of byte 0
        const uint8_t d[8] = {0xAB, 0, 0, 0, 0, 0, 0, 0};
        const auto r = extract_raw(make_signal(3, 4, false, false), d, 8);
        CHECK(r && *r == 0xBu);
    }
    {   // crosses a byte boundary: low nibble of byte0 (B) then high nibble of byte1 (C)
        const uint8_t d[8] = {0xAB, 0xCD, 0, 0, 0, 0, 0, 0};
        const auto r = extract_raw(make_signal(3, 8, false, false), d, 8);
        CHECK(r && *r == 0xBCu);
    }
    {   // whole second byte: MSB position is bit 15
        const uint8_t d[8] = {0x11, 0x5A, 0, 0, 0, 0, 0, 0};
        const auto r = extract_raw(make_signal(15, 8, false, false), d, 8);
        CHECK(r && *r == 0x5Au);
    }
}

void test_bounds() {
    const uint8_t d[8] = {};
    CHECK(!extract_raw(make_signal(60, 8, true, false), d, 8));    // runs past byte 7
    CHECK(!extract_raw(make_signal(24, 8, true, false), d, 2));    // beyond a 2-byte payload
    CHECK(!extract_raw(make_signal(7, 24, false, false), d, 2));   // Motorola, too long for payload
    CHECK(!extract_raw(make_signal(0, 0, true, false), d, 8));     // zero length
    CHECK(!extract_raw(make_signal(0, 65, true, false), d, 8));    // too long
}

// ---------------------------------------------------------------- scaling & sign

void test_scaling_and_sign() {
    {   // Intel unsigned with factor: raw 0x1F40 = 8000 -> 2000 rpm
        const uint8_t d[8] = {0x40, 0x1F, 0, 0, 0, 0, 0, 0};
        const auto s = decode_signal(make_signal(0, 16, true, false, 0.25), d, 8);
        CHECK(s.has_value());
        CHECK_NEAR(s->physical, 2000.0, 1e-9);
        CHECK(s->raw == 8000);
    }
    {   // offset: raw 90 -> 50 degC
        const uint8_t d[8] = {90, 0, 0, 0, 0, 0, 0, 0};
        const auto s = decode_signal(make_signal(0, 8, true, false, 1.0, -40.0), d, 8);
        CHECK_NEAR(s->physical, 50.0, 1e-9);
    }
    {   // signed 8 bit Intel: 0xFE = -2, factor 0.1 -> -0.2
        const uint8_t d[8] = {0, 0, 0xFE, 0, 0, 0, 0, 0};
        const auto s = decode_signal(make_signal(16, 8, true, true, 0.1), d, 8);
        CHECK(s->raw == -2);
        CHECK_NEAR(s->physical, -0.2, 1e-9);
    }
    {   // signed 16 bit Motorola: 0xFF9C = -100, factor 0.1 -> -10
        const uint8_t d[8] = {0xFF, 0x9C, 0, 0, 0, 0, 0, 0};
        const auto s = decode_signal(make_signal(7, 16, false, true, 0.1), d, 8);
        CHECK(s->raw == -100);
        CHECK_NEAR(s->physical, -10.0, 1e-9);
    }
    {   // signed 12 bit: 0x800 is the most negative value
        const uint8_t d[8] = {0x00, 0x08, 0, 0, 0, 0, 0, 0};
        const auto s = decode_signal(make_signal(0, 12, true, true), d, 8);
        CHECK(s->raw == -2048);
    }
}

// ---------------------------------------------------------------- DBC parsing

const char* kInlineDbc = R"DBC(VERSION ""

BU_: A B

BO_ 256 Msg1: 8 A
 SG_ Speed : 0|16@1+ (0.5,10) [0|100] "km/h" B
 SG_ State : 16|2@1+ (1,0) [0|3] "" B
 SG_ Angle : 39|16@0- (0.1,0) [-100|100] "deg" B

BO_ 2364539904 Ext1: 8 A
 SG_ X : 0|8@1- (1,0) [0|0] "" B

BO_ 1024 Diag: 8 A
 SG_ Sel M : 0|8@1+ (1,0) [0|255] "" B
 SG_ T m0 : 8|8@1+ (1,-40) [-40|215] "degC" B
 SG_ P m1 : 8|8@1+ (1,0) [0|255] "kPa" B

VAL_ 256 State 0 "Off" 2 "On" ;
)DBC";

void test_dbc_parse() {
    const Database db = Database::parse_string(kInlineDbc);
    CHECK(db.messages().size() == 3);

    const Message* m = db.find(256, false);
    CHECK(m != nullptr);
    if (m) {
        CHECK(m->name == "Msg1");
        CHECK(m->dlc == 8);
        CHECK(m->sender == "A");
        CHECK(m->signal_defs.size() == 3);

        const Signal& speed = m->signal_defs[0];
        CHECK(speed.name == "Speed");
        CHECK(speed.start_bit == 0 && speed.length == 16);
        CHECK(speed.little_endian && !speed.is_signed);
        CHECK_NEAR(speed.factor, 0.5, 1e-12);
        CHECK_NEAR(speed.offset, 10.0, 1e-12);
        CHECK_NEAR(speed.maximum, 100.0, 1e-12);
        CHECK(speed.unit == "km/h");

        const Signal& state = m->signal_defs[1];
        CHECK(state.value_table.size() == 2);
        CHECK(state.value_table.at(2) == "On");

        const Signal& angle = m->signal_defs[2];
        CHECK(!angle.little_endian && angle.is_signed);   // "@0-"
    }

    CHECK(db.find(256, true) == nullptr);        // standard and extended ids are separate spaces
    const Message* e = db.find(0x0CF00400, true);
    CHECK(e != nullptr && e->name == "Ext1" && e->extended);
    CHECK(db.find(0x0CF00400, false) == nullptr);
    CHECK(db.find(999, false) == nullptr);

    const Message* d = db.find(1024, false);
    CHECK(d && d->signal_defs[0].mux_role == MuxRole::Selector);
    CHECK(d && d->signal_defs[1].mux_role == MuxRole::Muxed && d->signal_defs[1].mux_value == 0);
    CHECK(d && d->signal_defs[2].mux_role == MuxRole::Muxed && d->signal_defs[2].mux_value == 1);
}

void test_dbc_errors() {
    bool threw = false;
    try {
        Database::parse_string("BO_ 1 M: 8 A\n SG_ Broken : nonsense\n");
    } catch (const ParseError&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        Database::parse_file("/nonexistent/file.dbc");
    } catch (const ParseError&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        Database::parse_string("BO_ 5 A: 8 X\nBO_ 5 B: 8 X\n");   // duplicate id
    } catch (const ParseError&) {
        threw = true;
    }
    CHECK(threw);

    // CRLF line endings (common for DBCs saved on Windows) must work
    const Database db = Database::parse_string(
        "BO_ 7 M: 8 A\r\n SG_ S : 0|8@1+ (1,0) [0|255] \"\" B\r\n");
    CHECK(db.find(7, false) && db.find(7, false)->signal_defs.size() == 1);
}

// ---------------------------------------------------------------- frame decoding

void test_decode_frame() {
    const Database db = Database::parse_string(kInlineDbc);

    CanFrame f;
    f.id = 256;
    f.dlc = 8;
    f.data = {0x10, 0x00, 0x02, 0x00, 0xFF, 0x9C, 0, 0};   // Speed raw 16, State=2, Angle in bytes 4-5
    const auto r = decode_frame(db, f);
    CHECK(r.has_value());
    if (r) {
        CHECK(r->message->name == "Msg1");
        CHECK(r->signal_values.size() == 3);
        CHECK_NEAR(r->signal_values[0].physical, 16 * 0.5 + 10, 1e-9);   // 18
        CHECK(r->signal_values[1].label == "On");
        CHECK_NEAR(r->signal_values[2].physical, -10.0, 1e-9);
    }

    f.id = 12345;   // unknown
    CHECK(!decode_frame(db, f));

    CanFrame rtr;
    rtr.id = 256;
    rtr.rtr = true;
    CHECK(!decode_frame(db, rtr));

    // Short frame: only the signals that fit are returned
    CanFrame shortf;
    shortf.id = 256;
    shortf.dlc = 3;
    shortf.data = {0x10, 0x00, 0x02, 0, 0, 0, 0, 0};
    const auto sr = decode_frame(db, shortf);
    CHECK(sr && sr->signal_values.size() == 2);
}

void test_multiplexing() {
    const Database db = Database::parse_string(kInlineDbc);

    CanFrame f;
    f.id = 1024;
    f.dlc = 8;
    f.data = {0, 90, 0, 0, 0, 0, 0, 0};   // selector 0 -> T = 90 - 40 = 50
    auto r = decode_frame(db, f);
    CHECK(r && r->signal_values.size() == 2);
    if (r && r->signal_values.size() == 2) {
        CHECK(r->signal_values[0].name == "Sel");
        CHECK(r->signal_values[1].name == "T");
        CHECK_NEAR(r->signal_values[1].physical, 50.0, 1e-9);
    }

    f.data = {1, 77, 0, 0, 0, 0, 0, 0};   // selector 1 -> P = 77
    r = decode_frame(db, f);
    CHECK(r && r->signal_values.size() == 2);
    if (r && r->signal_values.size() == 2) {
        CHECK(r->signal_values[1].name == "P");
        CHECK_NEAR(r->signal_values[1].physical, 77.0, 1e-9);
    }

    f.data = {5, 77, 0, 0, 0, 0, 0, 0};   // selector with no matching signal
    r = decode_frame(db, f);
    CHECK(r && r->signal_values.size() == 1);
}

// ---------------------------------------------------------------- log parsing

void test_log_line_parsing() {
    {
        const auto f = parse_candump_line("(1700000000.123456) can0 123#11223344");
        CHECK(f.has_value());
        if (f) {
            CHECK_NEAR(f->timestamp, 1700000000.123456, 1e-6);
            CHECK(f->iface == "can0");
            CHECK(f->id == 0x123 && !f->extended && !f->rtr);
            CHECK(f->dlc == 4);
            CHECK(f->data[0] == 0x11 && f->data[3] == 0x44);
        }
    }
    {
        const auto f = parse_candump_line("(1.0) vcan1 0CF00400#FFFFFF00");
        CHECK(f && f->extended && f->id == 0x0CF00400 && f->dlc == 4 && f->iface == "vcan1");
    }
    {
        const auto f = parse_candump_line("(2.0) can0 123#R");
        CHECK(f && f->rtr && f->dlc == 0);
    }
    {
        const auto f = parse_candump_line("(2.0) can0 123#R4");
        CHECK(f && f->rtr && f->dlc == 4);
    }
    {
        const auto f = parse_candump_line("(2.0) can0 123#");   // DLC 0, valid
        CHECK(f && !f->rtr && f->dlc == 0);
    }
    {
        const auto f = parse_candump_line("(3.5) can0 7ff#0102030405060708\r");   // CRLF, max std id
        CHECK(f && f->id == 0x7FF && f->dlc == 8 && f->data[7] == 0x08);
    }

    CHECK(!parse_candump_line(""));
    CHECK(!parse_candump_line("garbage"));
    CHECK(!parse_candump_line("(1.0) can0 12#00"));         // 2-digit id
    CHECK(!parse_candump_line("(1.0) can0 123#ZZ"));        // not hex
    CHECK(!parse_candump_line("(1.0) can0 123#123"));       // odd number of nibbles
    CHECK(!parse_candump_line("(1.0) can0 123#001122334455667788"));   // 9 bytes
    CHECK(!parse_candump_line("(1.0) can0 123##1AABB"));    // CAN FD (unsupported)
    CHECK(!parse_candump_line("(abc) can0 123#00"));        // bad timestamp
    CHECK(!parse_candump_line("1.0 can0 123#00"));          // missing parentheses
}

void test_log_reader() {
    std::istringstream in(
        "(1.0) can0 100#01\n"
        "\n"
        "this is not a frame\n"
        "(2.0) can0 200#0203\n");
    LogReader reader(in);
    const auto a = reader.next();
    const auto b = reader.next();
    const auto c = reader.next();
    CHECK(a && a->id == 0x100);
    CHECK(b && b->id == 0x200 && b->dlc == 2);
    CHECK(!c);
    CHECK(reader.skipped_lines() == 1);   // the blank line is not counted
}

// ---------------------------------------------------------------- end to end on the sample data

void test_sample_data() {
    const std::string dir = CANLOG_DATA_DIR;
    const Database db = Database::parse_file(dir + "/sample.dbc");
    CHECK(db.messages().size() == 5);

    std::ifstream log(dir + "/sample.log");
    CHECK(static_cast<bool>(log));
    LogReader reader(log);

    size_t total = 0, decoded = 0, unknown = 0;
    bool seen_first_engine = false;
    double max_speed = 0, prev_speed = -1;
    double min_steer = 1e9, max_steer = -1e9;
    bool speed_monotonic = true;
    bool saw_eec1 = false, saw_oil_temp = false, saw_oil_pressure = false, saw_abs = false;

    while (auto frame = reader.next()) {
        ++total;
        const auto r = decode_frame(db, *frame);
        if (!r) {
            ++unknown;
            CHECK(frame->id == 0x7E0);
            continue;
        }
        ++decoded;
        CHECK(!r->signal_values.empty());

        for (const auto& s : r->signal_values) {
            if (s.name == "VehicleSpeed") {
                max_speed = std::max(max_speed, s.physical);
                if (s.physical < prev_speed) speed_monotonic = false;
                prev_speed = s.physical;
            } else if (s.name == "SteeringAngle") {
                min_steer = std::min(min_steer, s.physical);
                max_steer = std::max(max_steer, s.physical);
            } else if (s.name == "EngineSpeedJ1939") {
                saw_eec1 = true;
            } else if (s.name == "OilTemp") {
                saw_oil_temp = true;
            } else if (s.name == "OilPressure") {
                saw_oil_pressure = true;
            } else if (s.name == "AbsActive" && s.physical == 1.0) {
                saw_abs = true;
            }
        }

        if (!seen_first_engine && r->message->name == "EngineData") {
            seen_first_engine = true;
            CHECK_NEAR(r->signal_values[0].physical, 800.0, 1e-9);    // EngineSpeed
            CHECK_NEAR(r->signal_values[1].physical, 50.0, 1e-9);     // CoolantTemp
            CHECK_NEAR(r->signal_values[2].physical, 0.0, 1e-9);      // ThrottlePos
            CHECK(r->signal_values[3].label == "Cranking");           // EngineState
        }
    }

    CHECK(reader.skipped_lines() == 0);
    CHECK(total == 103);
    CHECK(decoded == 100);
    CHECK(unknown == 3);
    CHECK(speed_monotonic);                       // Motorola unsigned decode
    CHECK_NEAR(max_speed, 60.0, 0.011);
    CHECK(min_steer < -29.0 && max_steer > 29.0); // Motorola signed decode: both signs present
    CHECK(saw_eec1 && saw_oil_temp && saw_oil_pressure && saw_abs);
}

}  // namespace

int main() {
    test_intel_extraction();
    test_motorola_extraction();
    test_bounds();
    test_scaling_and_sign();
    test_dbc_parse();
    test_dbc_errors();
    test_decode_frame();
    test_multiplexing();
    test_log_line_parsing();
    test_log_reader();
    test_sample_data();

    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
