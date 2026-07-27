// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <atomic>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "currency.hpp"

using namespace scratcher;

TEST_CASE("Fixed-point ctor preserves raw value, decimal count and multiplier", "[CURRENCY-001]")
{
    currency<int> val1(10, 1);
    CHECK(val1.raw() == 10);
    CHECK(val1.decimals() == 1);
    CHECK(val1.multiplier() == 10);

    CHECK(currency<int>(10, 10).raw() == 10);
}

TEST_CASE("Decimal string parsing infers scale and group separators are accepted", "[CURRENCY-002]")
{
    CHECK(currency<size_t>("1.00").to_string() == "1.00");
    CHECK(currency<size_t>("1.00").raw() == 100);
    CHECK(currency<size_t>("1.00").decimals() == 2);

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

    CHECK(currency<uint64_t>("1'234.5").raw() == 12345);
    CHECK(currency<uint64_t>("1'234.5").decimals() == 1);
    CHECK(currency<uint64_t>("1 234.50").raw() == 123450);
    CHECK(currency<uint64_t>("1 234.50").decimals() == 2);
    CHECK(currency<uint64_t>("1,234.75").raw() == 123475);
    CHECK(currency<uint64_t>("1,234.75").decimals() == 2);
}

TEST_CASE("Malformed or out-of-range input is rejected", "[CURRENCY-003]")
{
    CHECK_THROWS_AS(currency<uint64_t>("1.2.3"), std::invalid_argument);
    CHECK_THROWS_AS(currency<uint64_t>("1."), std::invalid_argument);
    CHECK_THROWS_AS(currency<uint64_t>("12a4"), std::invalid_argument);
    CHECK_THROWS_AS(currency<uint64_t>(""), std::invalid_argument);

    CHECK_THROWS_AS(currency<uint64_t>("18446744073709551616"), std::overflow_error);

    CHECK_THROWS_AS(currency<uint64_t>("1.234", 2), std::overflow_error);
    CHECK_THROWS_AS(currency<uint64_t>("1.2345", 3), std::overflow_error);
    CHECK(currency<uint64_t>("1.2300", 2).raw() == 123);
}

TEST_CASE("to_string zero-pads, omits the dot at scale 0 and round-trips", "[CURRENCY-004]")
{
    CHECK(currency<uint64_t>("83").to_string() == "83");
    CHECK(currency<uint64_t>("8000000").to_string() == "8000000");
    CHECK(currency<uint64_t>{}.to_string() == "0");
    CHECK(currency<uint64_t>(currency<uint64_t>("83").to_string()).raw() == 83);

    CHECK(currency<size_t>(10, 1).to_string() == "1.0");
    CHECK(currency<size_t>(10, 5).to_string() == "0.00010");

    currency<int64_t> neg(-12345, 2);
    CHECK(neg.to_string() == "-123.45");
    CHECK(currency<int64_t>("-123.45").raw() == -12345);
    CHECK(currency<int64_t>("-123.45").decimals() == 2);
    CHECK(currency<int64_t>(neg.to_string()).raw() == -12345);
}

TEST_CASE("raw_at rescales to a fixed decimal count", "[CURRENCY-005]")
{
    currency<uint64_t> px("16578.5");   // decimals 1, raw 165785
    CHECK(px.raw_at(1) == 165785);      // same scale
    CHECK(px.raw_at(2) == 1657850);     // widen
    CHECK(px.raw_at(4) == 165785000);   // widen
    CHECK(px.raw_at(0) == 16578);       // narrow, truncates toward zero
}

TEST_CASE("Comparisons rescale operands to the wider of the two scales", "[CURRENCY-006]")
{
    currency<uint64_t> a("1.5");
    currency<uint64_t> b("1.50");
    currency<uint64_t> c("1.6");

    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);
    CHECK(c > a);
    CHECK(a <= b);
    CHECK(b >= a);
    CHECK(a <= c);
}

TEST_CASE("Addition and subtraction carry the wider operand scale", "[CURRENCY-007]")
{
    currency<uint64_t> a("1.5");
    currency<uint64_t> b("1.25");

    auto sum = a + b;
    CHECK(sum.raw() == 275);
    CHECK(sum.decimals() == 2);
    CHECK(sum.to_string() == "2.75");

    auto diff = a - b;
    CHECK(diff.raw() == 25);
    CHECK(diff.decimals() == 2);
    CHECK(diff.to_string() == "0.25");

    currency<uint64_t> c("100");
    currency<uint64_t> d("0.5");
    auto sum2 = c + d;
    CHECK(sum2.raw() == 1005);
    CHECK(sum2.decimals() == 1);
    CHECK(sum2.to_string() == "100.5");
}

TEST_CASE("Multiplication sums scales and division subtracts them", "[CURRENCY-008]")
{
    currency<uint64_t> price("1.5");
    currency<uint64_t> factor("2.25");
    auto product = price * factor;
    CHECK(product.raw() == 3375);
    CHECK(product.decimals() == 3);
    CHECK(product.to_string() == "3.375");

    currency<uint64_t> three("3");
    currency<uint64_t> onehalf("1.5");
    auto quotient = three / onehalf;   // divisor has more decimals: numerator is widened first, so this is exact
    CHECK(quotient.raw() == 2);
    CHECK(quotient.decimals() == 0);
    CHECK(quotient.to_string() == "2");

    currency<uint64_t> vwap_price("150.75");
    currency<uint64_t> volume("100");
    auto notional = vwap_price * volume;
    CHECK(notional.to_string() == "15075.00");
    auto recovered_price = notional / volume;
    CHECK(recovered_price.raw() == 15075);
    CHECK(recovered_price.decimals() == 2);
    CHECK(recovered_price.to_string() == "150.75");
}

TEST_CASE("Negation flips sign in place and via unary minus", "[CURRENCY-009]")
{
    currency<int64_t> a(12345, 2);

    auto b = -a;
    CHECK(b.raw() == -12345);
    CHECK(b.decimals() == 2);
    CHECK(b.to_string() == "-123.45");
    CHECK(a.raw() == 12345);

    a.negate();
    CHECK(a.raw() == -12345);
    CHECK(a.decimals() == 2);

    a.negate();
    CHECK(a.raw() == 12345);
}

TEST_CASE("Plain and atomic storage convert into each other on copy and assignment", "[CURRENCY-010]")
{
    currency<std::atomic_uint64_t> src(15075, 2);

    currency<uint64_t> plain(src);
    CHECK(plain.raw() == 15075);
    CHECK(plain.decimals() == 2);
    CHECK(plain.to_string() == "150.75");

    currency<std::atomic_uint64_t> back(plain);
    CHECK(back.raw() == 15075);
    CHECK(back.decimals() == 2);
    CHECK(back.to_string() == "150.75");

    currency<uint64_t> plain2;
    plain2 = src;
    CHECK(plain2.raw() == 15075);
    CHECK(plain2.decimals() == 2);

    currency<std::atomic_uint64_t> atomic2;
    atomic2 = plain;
    CHECK(atomic2.raw() == 15075);
    CHECK(atomic2.decimals() == 2);
    CHECK(atomic2.to_string() == "150.75");
}
