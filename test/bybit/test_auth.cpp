// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "data/bybit/auth.hpp"

using namespace scratcher::bybit;
namespace http = boost::beast::http;

namespace {

const credentials test_credentials{.api_key = "test-key", .api_secret = "test-secret"};

// HMAC-SHA256("test-secret", "1700000000000" + "test-key" + "5000" + payload), computed with Python hmac
constexpr std::string_view fixed_timestamp = "1700000000000";
constexpr std::string_view get_query       = "category=spot&symbol=BTCUSDT";
constexpr std::string_view post_body       = R"({"symbol":"BTCUSDT","qty":"0.01"})";
constexpr std::string_view get_signature   = "0048edf42c4979197cec265d4f090ffe6c30d7dec8782e4e6a26b51c2703cbf9";
constexpr std::string_view post_signature  = "abca3716a253238c2473d70d019d8fd6aa515e71c92fa7b1350adf8adacc8e28";
constexpr std::string_view empty_signature = "d8d5e71d8f986368aa5c13405f059ab6adb4f41df59d2f11bb056226b63457d6";

std::string header(const scratcher::connect::http_request& request, std::string_view name)
{
    auto it = std::ranges::find(request.headers, name, &std::pair<std::string, std::string>::first);
    return it == request.headers.end() ? std::string{} : it->second;
}

bool is_millisecond_timestamp(const std::string& text)
{
    return text.size() == 13 && std::ranges::all_of(text, [](unsigned char c) { return std::isdigit(c); });
}

struct temp_keyfile
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / "scratcher_test_bybit.key";

    explicit temp_keyfile(std::string_view content)
    {
        std::ofstream(path) << content;
    }
    ~temp_keyfile() { std::filesystem::remove(path); }
};

} // anonymous namespace

TEST_CASE("rest_signer computes the ByBit HMAC-SHA256 signature", "[bybit][auth]")
{
    rest_signer signer{test_credentials};

    CHECK(signer.sign(fixed_timestamp, get_query) == get_signature);
    CHECK(signer.sign(fixed_timestamp, post_body) == post_signature);
    CHECK(signer.sign(fixed_timestamp, "") == empty_signature);
}

TEST_CASE("rest_signer injects the X-BAPI headers and signs the GET query string", "[bybit][auth]")
{
    rest_signer signer{test_credentials};
    scratcher::connect::http_request request{.verb = http::verb::get, .target = "/v5/order/realtime?" + std::string{get_query}};
    int query = 0;

    signer(query, request);

    CHECK(header(request, "X-BAPI-API-KEY") == "test-key");
    CHECK(header(request, "X-BAPI-RECV-WINDOW") == "5000");
    const std::string timestamp = header(request, "X-BAPI-TIMESTAMP");
    CHECK(is_millisecond_timestamp(timestamp));
    CHECK(header(request, "X-BAPI-SIGN") == signer.sign(timestamp, get_query));
    CHECK(header(request, "X-BAPI-SIGN") != signer.sign(timestamp, ""));
}

TEST_CASE("rest_signer signs an empty query for a bare GET target", "[bybit][auth]")
{
    rest_signer signer{test_credentials};
    scratcher::connect::http_request request{.verb = http::verb::get, .target = "/v5/account/wallet-balance"};
    int query = 0;

    signer(query, request);

    CHECK(header(request, "X-BAPI-SIGN") == signer.sign(header(request, "X-BAPI-TIMESTAMP"), ""));
}

TEST_CASE("rest_signer signs the POST body rather than the target", "[bybit][auth]")
{
    rest_signer signer{test_credentials};
    scratcher::connect::http_request request{.verb = http::verb::post, .target = "/v5/order/create?ignored=1", .body = std::string{post_body}};
    int query = 0;

    signer(query, request);

    const std::string timestamp = header(request, "X-BAPI-TIMESTAMP");
    CHECK(is_millisecond_timestamp(timestamp));
    CHECK(header(request, "X-BAPI-SIGN") == signer.sign(timestamp, post_body));
    CHECK(header(request, "X-BAPI-SIGN") != signer.sign(timestamp, "ignored=1"));
}

TEST_CASE("ws_authenticator builds the auth frame with the api key and a future expiry", "[bybit][auth]")
{
    ws_authenticator authenticator{test_credentials};
    const int64_t before = current_time_ms();

    const std::string message = authenticator.auth_message();

    const std::string prefix = R"({"op":"auth","args":["test-key",)";
    REQUIRE(message.starts_with(prefix));
    const auto expires_end = message.find(',', prefix.size());
    REQUIRE(expires_end != std::string::npos);
    const int64_t expires = std::stoll(message.substr(prefix.size(), expires_end - prefix.size()));
    CHECK(expires >= before + 10000);
    CHECK(message.ends_with(R"("]})"));
}

TEST_CASE("load_credentials reads the key and the secret from a two-line file", "[bybit][auth]")
{
    SECTION("plain two lines")
    {
        temp_keyfile file("file-key\nfile-secret\n");
        auto creds = load_credentials(file.path);
        CHECK(creds.api_key == "file-key");
        CHECK(creds.api_secret == "file-secret");
        CHECK(creds.recv_window == "5000");
    }
    SECTION("CRLF endings and trailing blanks are stripped")
    {
        temp_keyfile file("file-key \r\nfile-secret\t\r\n");
        auto creds = load_credentials(file.path);
        CHECK(creds.api_key == "file-key");
        CHECK(creds.api_secret == "file-secret");
    }
    SECTION("missing file throws")
    {
        CHECK_THROWS_AS(load_credentials(std::filesystem::temp_directory_path() / "scratcher_missing.key"), std::runtime_error);
    }
    SECTION("single line throws")
    {
        temp_keyfile file("file-key\n");
        CHECK_THROWS_AS(load_credentials(file.path), std::runtime_error);
    }
}
