// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BYBIT_ENUMS_HPP
#define BYBIT_ENUMS_HPP

#include <glaze/glaze.hpp>

namespace scratcher::bybit {

enum class InstrumentStatus {
    PreLaunch,
    Trading,
    Settling,
    Delivering,
    Closed
};

enum class ExecType {
    Trade,
    AdlTrade,
    Funding,
    BustTrade,
    Settle,
    BlockTrade,
    MovePosition
};

enum class Category {
    Spot,
    Linear,
    Inverse,
    Option
};

enum class OrderSide {
    Buy,
    Sell
};

enum class OrderStatus {
    New,
    PartiallyFilled,
    Untriggered,
    Rejected,
    PartiallyFilledCanceled,
    Filled,
    Cancelled,
    Triggered,
    Deactivated
};

enum class OrderType {
    Market,
    Limit
};

enum class StopOrderType {
    TakeProfit,
    StopLoss,
    TrailingStop,
    Stop,
    PartialTakeProfit,
    PartialStopLoss
};

enum class TimeInForce {
    GTC,
    IOC,
    FOK,
    PostOnly
};

enum class TriggerBy {
    LastPrice,
    IndexPrice,
    MarkPrice
};

enum class AccountType {
    UNIFIED,
    CONTRACT
};

} // namespace scratcher::bybit

template <>
struct glz::meta<scratcher::bybit::Category> {
    using enum scratcher::bybit::Category;
    static constexpr auto value = enumerate(
        "spot", Spot,
        "linear", Linear,
        "inverse", Inverse,
        "option", Option
    );
};

template <>
struct glz::meta<scratcher::bybit::OrderSide> {
    using enum scratcher::bybit::OrderSide;
    static constexpr auto value = enumerate(
        "Buy", Buy,
        "Sell", Sell
    );
};

template <>
struct glz::meta<scratcher::bybit::InstrumentStatus> {
    using enum scratcher::bybit::InstrumentStatus;
    static constexpr auto value = enumerate(
        "PreLaunch", PreLaunch,
        "Trading", Trading,
        "Settling", Settling,
        "Delivering", Delivering,
        "Closed", Closed
    );
};

template <>
struct glz::meta<scratcher::bybit::ExecType> {
    using enum scratcher::bybit::ExecType;
    static constexpr auto value = enumerate(
        "Trade", Trade,
        "AdlTrade", AdlTrade,
        "Funding", Funding,
        "BustTrade", BustTrade,
        "Settle", Settle,
        "BlockTrade", BlockTrade,
        "MovePosition", MovePosition
    );
};

template <>
struct glz::meta<scratcher::bybit::OrderStatus> {
    using enum scratcher::bybit::OrderStatus;
    static constexpr auto value = enumerate(
        "New", New,
        "PartiallyFilled", PartiallyFilled,
        "Untriggered", Untriggered,
        "Rejected", Rejected,
        "PartiallyFilledCanceled", PartiallyFilledCanceled,
        "Filled", Filled,
        "Cancelled", Cancelled,
        "Triggered", Triggered,
        "Deactivated", Deactivated
    );
};

template <>
struct glz::meta<scratcher::bybit::OrderType> {
    using enum scratcher::bybit::OrderType;
    static constexpr auto value = enumerate(
        "Market", Market,
        "Limit", Limit
    );
};

template <>
struct glz::meta<scratcher::bybit::StopOrderType> {
    using enum scratcher::bybit::StopOrderType;
    static constexpr auto value = enumerate(
        "TakeProfit", TakeProfit,
        "StopLoss", StopLoss,
        "TrailingStop", TrailingStop,
        "Stop", Stop,
        "PartialTakeProfit", PartialTakeProfit,
        "PartialStopLoss", PartialStopLoss
    );
};

template <>
struct glz::meta<scratcher::bybit::TimeInForce> {
    using enum scratcher::bybit::TimeInForce;
    static constexpr auto value = enumerate(
        "GTC", GTC,
        "IOC", IOC,
        "FOK", FOK,
        "PostOnly", PostOnly
    );
};

template <>
struct glz::meta<scratcher::bybit::TriggerBy> {
    using enum scratcher::bybit::TriggerBy;
    static constexpr auto value = enumerate(
        "LastPrice", LastPrice,
        "IndexPrice", IndexPrice,
        "MarkPrice", MarkPrice
    );
};

template <>
struct glz::meta<scratcher::bybit::AccountType> {
    using enum scratcher::bybit::AccountType;
    static constexpr auto value = enumerate(
        "UNIFIED", UNIFIED,
        "CONTRACT", CONTRACT
    );
};

#endif // BYBIT_ENUMS_HPP
