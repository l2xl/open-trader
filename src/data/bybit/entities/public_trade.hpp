// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BYBIT_PUBLIC_TRADE_HPP
#define BYBIT_PUBLIC_TRADE_HPP

#include <string>
#include <optional>
#include "enums.hpp"
#include "currency.hpp"
#include "timedef.hpp"
#include <glaze/glaze.hpp>
#include <glaze/core/common.hpp>
#include <glaze/core/wrappers.hpp>

namespace scratcher::bybit {

using scratcher::currency;

// PublicTrade uses REST API field naming convention
// This is the canonical representation used throughout the application. price/size
// are currency<uint64_t> parsed once from the wire quoted-string; time/seq stay
// strings (a millisecond timestamp and a sequence id, not fractional values).
struct PublicTrade {
    std::string execId;                // Execution ID
    std::string symbol;                 // Symbol name
    currency<uint64_t> price;           // Execution price
    currency<uint64_t> size;            // Execution size
    OrderSide side;                     // Trade side (Buy/Sell)
    time_point time;                    // Execution timestamp; parsed from the wire ms (REST string / WS number)
    bool isBlockTrade{false};         // Whether it's a block trade
    bool isRPITrade{false};           // Whether it's RPI trade
    std::string seq;                    // Sequence number
    std::optional<currency<uint64_t>> mP;     // Mark price (for options)
    std::optional<currency<uint64_t>> iP;     // Index price (for options)
    std::optional<currency<uint64_t>> mIv;    // Mark IV (for options)
    std::optional<currency<uint64_t>> iv;      // IV (for options)
};

// WsPublicTrade is a derived struct that acts as a name alias for WebSocket deserialization
// It contains no additional fields - glz::meta provides the mapping from WebSocket field names
// to the inherited PublicTrade field names
struct WsPublicTrade : PublicTrade {
};

} // namespace scratcher::bybit

// Glaze codec for the project time_point (system_clock): REST trade feeds carry the execution
// timestamp as a quoted millisecond count ("1700000000000"); read it into a time_point and write
// it back the same way. The WS form (a bare number) is handled per-field in WsPublicTrade's meta.
template <>
struct glz::meta<scratcher::time_point> {
    static constexpr auto value = glz::custom<
        [](scratcher::time_point& tp, const std::string& s) {
            tp = s.empty() ? scratcher::time_point{} : scratcher::time_point{scratcher::milliseconds{std::stoll(s)}};
        },
        [](const scratcher::time_point& tp) -> std::string {
            return std::to_string(scratcher::get_timestamp(tp));
        }
    >;
};

// Glaze metadata for WsPublicTrade - maps WebSocket field names to PublicTrade members
// WebSocket uses abbreviated field names: i, T, p, v, S, s, BT, RPI, seq
// These are mapped to: execId, time, price, size, side, symbol, isBlockTrade, isRPITrade, seq
// NOTE: WebSocket sends "T" (timestamp) and "seq" as numbers, but REST API sends them as strings
// We use custom converters to handle this difference
template <>
struct glz::meta<scratcher::bybit::WsPublicTrade> {
    using T = scratcher::bybit::WsPublicTrade;

    static constexpr auto value = glz::object(
        "i", &T::execId,           // Trade ID -> execId
        "T", glz::custom<          // WS sends the timestamp as a bare ms number -> time_point
            [](scratcher::bybit::WsPublicTrade& self, int64_t ms) { self.time = scratcher::time_point{scratcher::milliseconds{ms}}; },
            [](const scratcher::bybit::WsPublicTrade& self) { return static_cast<int64_t>(scratcher::get_timestamp(self.time)); }
        >,
        "p", &T::price,            // Price -> price
        "v", &T::size,             // Volume -> size
        "S", &T::side,             // Side -> side
        "s", &T::symbol,           // Symbol -> symbol
        "BT", &T::isBlockTrade,    // Block trade flag -> isBlockTrade
        "RPI", &T::isRPITrade,     // RPI trade flag -> isRPITrade
        "seq", glz::number<&scratcher::bybit::WsPublicTrade::seq>,  // Sequence as number -> seq as string
        "mP", &T::mP,              // Mark price (optional)
        "iP", &T::iP,              // Index price (optional)
        "mIv", &T::mIv,            // Mark IV (optional)
        "iv", &T::iv               // IV (optional)
    );
};

#endif // BYBIT_PUBLIC_TRADE_HPP
