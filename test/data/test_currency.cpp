// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <iostream>

#include <catch2/catch_test_macros.hpp>

#include "currency.hpp"

using namespace scratcher;

TEST_CASE("Construct")
{
    currency<int> val1(10, 1);
    CHECK(val1.raw() == 10);
    CHECK(val1.decimals() == 1);
    CHECK(val1.multiplier() == 10);

    CHECK(currency<int>(10, 10).raw() == 10);

    CHECK(currency<size_t>(10, 1).to_string() == "1.0");
    CHECK(currency<size_t>(10, 5).to_string() == "0.00010");

    CHECK(currency<size_t>("1.00").to_string() == "1.00");
    CHECK(currency<size_t>("1.00").raw() == 100);
    CHECK(currency<size_t>("1.00").decimals() == 2);
}

TEST_CASE("Parse")
{
    currency<int64_t> val(0, 9);
    CHECK(val.raw() == 0);
    CHECK(val.decimals() == 9);
    CHECK(val.multiplier() == 1000000000);

    val.parse("1");
    CHECK(val.raw() == 1000000000);
    CHECK(val.decimals() == 9);
    CHECK(val.multiplier() == 1000000000);

    val.parse("1.0");
    CHECK(val.raw() == 1000000000);
    CHECK(val.decimals() == 9);
    CHECK(val.multiplier() == 1000000000);

    val.parse("1.0000000000");
    CHECK(val.raw() == 1000000000);
    CHECK(val.decimals() == 9);
    CHECK(val.multiplier() == 1000000000);

    val.parse("1.11");
    CHECK(val.raw() == 1110000000);
    CHECK(val.decimals() == 9);
    CHECK(val.multiplier() == 1000000000);
}

TEST_CASE("Integer-scale values round-trip without a trailing dot")
{
    // decimals == 0 must not append a '.', so the string round-trips back to currency.
    CHECK(currency<uint64_t>("83").to_string() == "83");
    CHECK(currency<uint64_t>("8000000").to_string() == "8000000");
    CHECK(currency<uint64_t>{}.to_string() == "0");
    CHECK(currency<uint64_t>(currency<uint64_t>("83").to_string()).raw() == 83);
}

TEST_CASE("raw_at rescales to a fixed decimal count")
{
    currency<uint64_t> px("16578.5");   // decimals 1, raw 165785
    CHECK(px.raw_at(1) == 165785);      // same scale
    CHECK(px.raw_at(2) == 1657850);     // widen
    CHECK(px.raw_at(4) == 165785000);   // widen
    CHECK(px.raw_at(0) == 16578);       // narrow, truncates toward zero
}
