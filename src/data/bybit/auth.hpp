// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BYBIT_AUTH_HPP
#define BYBIT_AUTH_HPP

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <span>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>
#include "http_query.hpp"
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "common/hex.hpp"

namespace scratcher::bybit {

struct credentials {
    std::string api_key;
    std::string api_secret;
    std::string recv_window = "5000";
};

inline credentials load_credentials(const std::filesystem::path& keyfile)
{
    std::ifstream in(keyfile);
    if (!in) throw std::runtime_error("Cannot open ByBit API key file: " + keyfile.string());

    auto read_line = [&in] {
        std::string line;
        std::getline(in, line);
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        return line;
    };
    credentials creds{.api_key = read_line(), .api_secret = read_line()};
    if (creds.api_key.empty() || creds.api_secret.empty())
        throw std::runtime_error("ByBit API key file must hold the key on the first line and the secret on the second: " + keyfile.string());
    return creds;
}

inline int64_t current_time_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string hmac_sha256(std::string_view key, std::string_view data)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest.data(), &digest_len);
    return hex(std::span(digest.data(), digest_len));
}

/**
 * @brief RequestPolicy for ByBit private REST: signs the query string of a GET or the body of any other verb
 * and injects the X-BAPI-* headers (https://bybit-exchange.github.io/docs/v5/guide#authentication)
 */
class rest_signer
{
    credentials m_credentials;

public:
    explicit rest_signer(credentials creds) : m_credentials(std::move(creds)) {}

    std::string sign(std::string_view timestamp, std::string_view payload) const
    {
        std::string message;
        message.reserve(timestamp.size() + m_credentials.api_key.size() + m_credentials.recv_window.size() + payload.size());
        message.append(timestamp).append(m_credentials.api_key).append(m_credentials.recv_window).append(payload);
        return hmac_sha256(m_credentials.api_secret, message);
    }

    template<typename Query>
    void operator()(Query&, connect::http_request& request) const
    {
        const std::string timestamp = std::to_string(current_time_ms());
        const std::string_view target = request.target;
        const auto query_pos = target.find('?');
        const std::string_view payload = request.verb == boost::beast::http::verb::get
            ? (query_pos == std::string_view::npos ? std::string_view{} : target.substr(query_pos + 1))
            : std::string_view{request.body};

        request.headers.emplace_back("X-BAPI-API-KEY", m_credentials.api_key);
        request.headers.emplace_back("X-BAPI-TIMESTAMP", timestamp);
        request.headers.emplace_back("X-BAPI-SIGN", sign(timestamp, payload));
        request.headers.emplace_back("X-BAPI-RECV-WINDOW", m_credentials.recv_window);
    }
};

/**
 * @brief SessionPolicy for the ByBit private stream: writes the auth frame right after the handshake,
 * ahead of every queued subscription
 */
class ws_authenticator
{
    credentials m_credentials;

public:
    explicit ws_authenticator(credentials creds) : m_credentials(std::move(creds)) {}

    // ByBit rejects an auth frame whose `expires` is already past, so it is built at handshake time, not at construction
    std::string auth_message() const
    {
        const std::string expires = std::to_string(current_time_ms() + 10000);
        return R"({"op":"auth","args":[")" + m_credentials.api_key + R"(",)" + expires + R"(,")" + hmac_sha256(m_credentials.api_secret, "GET/realtime" + expires) + R"("]})";
    }

    template<typename Conn>
    static boost::asio::awaitable<void> co_open(std::weak_ptr<Conn> ref)
    {
        std::string message;
        if (auto self = ref.lock())
            message = self->m_policy.auth_message();
        else
            co_return;
        co_await Conn::co_write(ref, std::move(message));
    }

    template<typename Conn>
    void operator()(Conn&, std::string&) const {}
};

/**
 * @brief Acceptor for websocket-bound encoders: (query, args_json) -> {"op":"<op>","args":[<args_json>]}; the query part is ignored
 */
template<typename Conn>
class ws_op
{
    std::shared_ptr<Conn> m_connection;
    std::string m_op;

public:
    ws_op(std::shared_ptr<Conn> connection, std::string op) : m_connection(std::move(connection)), m_op(std::move(op)) {}

    void operator()(std::string, std::string args_json) const
    {
        (*m_connection)(R"({"op":")" + m_op + R"(","args":[)" + args_json + "]}");
    }
};

template<typename Conn>
ws_op<Conn> make_ws_op(std::shared_ptr<Conn> connection, std::string op)
{ return ws_op<Conn>(std::move(connection), std::move(op)); }

} // namespace scratcher::bybit

#endif // BYBIT_AUTH_HPP
