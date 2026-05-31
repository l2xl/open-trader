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

#ifndef BYBIT_FEE_RATE_HPP
#define BYBIT_FEE_RATE_HPP

#include <string>
#include <optional>
#include "currency.hpp"

namespace scratcher::bybit {

using scratcher::currency;

// Fee rate information from ByBit Account/GetFeeRate API
struct FeeRate {
    std::string symbol;  // Symbol name (e.g., "BTCUSDT", empty for options)
    std::optional<std::string> baseCoin; // Base coin (e.g., "BTC", "ETH", empty for spot)
    currency<uint64_t> takerFeeRate;    // Taker fee rate (e.g., "0.0006")
    currency<uint64_t> makerFeeRate;    // Maker fee rate (e.g., "0.0001")
};



} // namespace scratcher::bybit

#endif // BYBIT_FEE_RATE_HPP
