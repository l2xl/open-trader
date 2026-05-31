// Scratcher project
// Copyright (c) 2025 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b25tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#ifndef BYBIT_PUBLIC_TRADE_HPP
#define BYBIT_PUBLIC_TRADE_HPP

#include <string>
#include <optional>
#include "enums.hpp"
#include "currency.hpp"
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
    std::string time;                   // Execution timestamp (ms) - JSON sends as string
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
        "T", glz::number<&scratcher::bybit::WsPublicTrade::time>,  // Timestamp as number -> time as string
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
