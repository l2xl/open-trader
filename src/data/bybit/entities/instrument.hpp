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

#ifndef BYBIT_INSTRUMENT_HPP
#define BYBIT_INSTRUMENT_HPP

#include <string>
#include <optional>

#include "enums.hpp"
#include "currency.hpp"

namespace scratcher::bybit {

using scratcher::currency;

// InstrumentInfo - canonical flattened representation for DAO storage
// This is the single source of truth for instrument data storage
// Uses C++23 auto-reflection for DAO - no glz::meta needed. Every fractional field
// is a currency<uint64_t>: parsed once from the wire quoted-string and persisted as
// its canonical decimal string (TEXT) by the DAO.
struct InstrumentInfo {
    // Basic fields
    std::string symbol;                 // Symbol name (primary key)
    std::string baseCoin;               // Base coin
    std::string quoteCoin;              // Quote coin
    std::string symbolType;             // Geographic region classification (replaces innovation)
    std::string innovation;             // Innovation zone token ("0" or "1") - deprecated
    InstrumentStatus status;            // Instrument status
    std::string marginTrading;          // Margin trading support ("utaOnly", etc.)
    std::string stTag;                  // Special treatment label ("0" or "1")

    // Flattened priceFilter fields
    currency<uint64_t> tickSize;        // Tick size for price

    // Flattened lotSizeFilter fields
    currency<uint64_t> basePrecision;   // Base coin precision
    currency<uint64_t> quotePrecision;  // Quote coin precision
    currency<uint64_t> minOrderQty;     // Minimum order quantity (deprecated)
    currency<uint64_t> maxOrderQty;     // Maximum order quantity (deprecated)
    currency<uint64_t> minOrderAmt;     // Minimum order amount
    currency<uint64_t> maxOrderAmt;     // Maximum order amount (deprecated)
    currency<uint64_t> maxLimitOrderQty;     // Maximum limit order quantity
    currency<uint64_t> maxMarketOrderQty;    // Maximum market order quantity
    currency<uint64_t> postOnlyMaxLimitOrderSize;   // Maximum post-only/RPI order size

    // Flattened riskParameters fields
    currency<uint64_t> priceLimitRatioX;       // Price limit ratio X
    currency<uint64_t> priceLimitRatioY;       // Price limit ratio Y
};

// Helper structures for nested JSON deserialization
// These match the ByBit API nested structure
namespace detail {
    struct PriceFilter {
        currency<uint64_t> tickSize;
        std::optional<currency<uint64_t>> minPrice;
        std::optional<currency<uint64_t>> maxPrice;
    };

    struct LotSizeFilter {
        currency<uint64_t> basePrecision;
        currency<uint64_t> quotePrecision;
        currency<uint64_t> minOrderQty;
        currency<uint64_t> maxOrderQty;
        currency<uint64_t> minOrderAmt;
        currency<uint64_t> maxOrderAmt;
        currency<uint64_t> maxLimitOrderQty;
        currency<uint64_t> maxMarketOrderQty;
        currency<uint64_t> postOnlyMaxLimitOrderSize;
    };

    struct RiskParameters {
        currency<uint64_t> priceLimitRatioX;
        currency<uint64_t> priceLimitRatioY;
    };
}

// InstrumentInfoAPI - matches ByBit API JSON structure with nested objects
// Deserializes directly from API format, then converts to flat InstrumentInfo for DAO
struct InstrumentInfoAPI {
    std::string symbol;
    std::string baseCoin;
    std::string quoteCoin;
    std::string symbolType;
    std::string innovation;
    InstrumentStatus status;
    std::string marginTrading;
    std::string stTag;

    // Nested objects from ByBit API
    detail::PriceFilter priceFilter;
    detail::LotSizeFilter lotSizeFilter;
    detail::RiskParameters riskParameters;

    // Conversion operator to flat InstrumentInfo for DAO storage
    operator InstrumentInfo() const {
        return InstrumentInfo {
            symbol, baseCoin, quoteCoin, symbolType, innovation, status, marginTrading, stTag,
            priceFilter.tickSize,
            lotSizeFilter.basePrecision, lotSizeFilter.quotePrecision,
            lotSizeFilter.minOrderQty, lotSizeFilter.maxOrderQty,
            lotSizeFilter.minOrderAmt, lotSizeFilter.maxOrderAmt,
            lotSizeFilter.maxLimitOrderQty, lotSizeFilter.maxMarketOrderQty,
            lotSizeFilter.postOnlyMaxLimitOrderSize,
            riskParameters.priceLimitRatioX, riskParameters.priceLimitRatioY
        };
    }
};

} // namespace scratcher::bybit

#endif // BYBIT_INSTRUMENT_HPP
