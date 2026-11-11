#include "canlog/decoder.hpp"

#include <algorithm>

namespace canlog {
namespace {

int64_t sign_extend(uint64_t raw, unsigned length) {
    if (length < 64 && ((raw >> (length - 1)) & 1u)) {
        raw |= ~uint64_t{0} << length;   // fill everything above the sign bit with 1s
    }
    return static_cast<int64_t>(raw);
}

}  // namespace

std::optional<uint64_t> extract_raw(const Signal& sig, const uint8_t* data, size_t size) {
    if (sig.length == 0 || sig.length > 64) return std::nullopt;

    uint64_t raw = 0;

    if (sig.little_endian) {
        // Intel (@1): start_bit is the LSB. Bit numbering simply continues
        // upward: bit 8 is bit 0 of byte 1, and so on.
        for (unsigned i = 0; i < sig.length; ++i) {
            const unsigned bit = sig.start_bit + i;
            const size_t byte = bit / 8;
            if (byte >= size) return std::nullopt;
            raw |= static_cast<uint64_t>((data[byte] >> (bit % 8)) & 1u) << i;
        }
    } else {
        // Motorola (@0): start_bit is the MSB, in the "sawtooth" numbering.
        // Walk from MSB to LSB: inside a byte move down towards bit 0, then
        // jump to bit 7 of the *next* byte (bit + 15 from bit 0 of this byte).
        unsigned bit = sig.start_bit;
        for (unsigned i = 0; i < sig.length; ++i) {
            const size_t byte = bit / 8;
            if (byte >= size) return std::nullopt;
            raw = (raw << 1) | ((data[byte] >> (bit % 8)) & 1u);
            bit = (bit % 8 == 0) ? bit + 15 : bit - 1;
        }
    }
    return raw;
}

std::optional<DecodedSignal> decode_signal(const Signal& sig, const uint8_t* data, size_t size) {
    const auto bits = extract_raw(sig, data, size);
    if (!bits) return std::nullopt;

    DecodedSignal out;
    out.name = sig.name;
    out.unit = sig.unit;

    double numeric = 0.0;
    if (sig.is_signed) {
        const int64_t v = sign_extend(*bits, sig.length);
        out.raw = v;
        numeric = static_cast<double>(v);
    } else {
        out.raw = static_cast<int64_t>(*bits);   // 64-bit unsigned > INT64_MAX wraps; acceptable here
        numeric = static_cast<double>(*bits);
    }
    out.physical = numeric * sig.factor + sig.offset;

    const auto label = sig.value_table.find(out.raw);
    if (label != sig.value_table.end()) out.label = label->second;
    return out;
}

std::optional<DecodedMessage> decode_frame(const Database& db, const CanFrame& frame) {
    if (frame.rtr) return std::nullopt;
    const Message* msg = db.find(frame.id, frame.extended);
    if (!msg) return std::nullopt;

    const size_t size = std::min<size_t>(frame.dlc, frame.data.size());
    const uint8_t* data = frame.data.data();

    // Multiplexed messages: read the selector first, then only decode the
    // signals whose "m<N>" matches the selector's current value.
    std::optional<uint64_t> selector;
    for (const Signal& sig : msg->signal_defs) {
        if (sig.mux_role == MuxRole::Selector) {
            selector = extract_raw(sig, data, size);
            break;
        }
    }

    DecodedMessage out;
    out.message = msg;
    for (const Signal& sig : msg->signal_defs) {
        if (sig.mux_role == MuxRole::Muxed && (!selector || *selector != sig.mux_value)) continue;
        if (auto decoded = decode_signal(sig, data, size)) {
            out.signal_values.push_back(std::move(*decoded));
        }
    }
    return out;
}

}  // namespace canlog
