// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "http_query.hpp"
#include "connection_errors.hpp"
#include <iostream>

namespace scratcher::connect {

using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;

namespace this_coro = boost::asio::this_coro;
using ssl_stream = ssl_stream<tcp_stream>;


http_query::http_query(std::shared_ptr<context> context, http::verb verb, const std::string& url)
    : m_context(std::move(context)), m_verb(verb)
{
    try {
        auto parsed_url = boost::urls::parse_uri(url);

        std::string scheme = parsed_url.value().scheme();
        m_host = parsed_url.value().host();
        m_port = parsed_url.value().port();
        m_path = parsed_url.value().path();

        if (parsed_url.value().has_query())
            m_query = parsed_url.value().query();

        if (m_port.empty()) {
            if (scheme == "https")
                m_port = "443";
            else
                throw std::invalid_argument("Unsupported scheme: " + scheme);
        }

        co_spawn(context->io(), context->co_resolve(context, m_host, m_port), detached);
    }
    catch (...) {
        std::throw_with_nested(std::invalid_argument("Invalid URL: " + url));
    }
}


void http_query::operator()(std::string query, http_headers headers, std::string body)
{
    std::string path_query = m_path;

    if (!m_query.empty() || !query.empty())
    {
        path_query += "?";

        if (!m_query.empty()) {
            path_query += m_query;
        }

        if (!query.empty())
        {
            if (!m_query.empty() && query[0] != '&')
                path_query += "&";
            else if (m_query.empty() && query[0] == '&')
                query = query.substr(1);
            path_query += std::move(query);
        }
    }

    std::weak_ptr<http_query> ref = weak_from_this();
    if (auto ctx = m_context.lock())
    {
        co_spawn(ctx->io(), co_request(ref, std::move(path_query), std::move(headers), std::move(body)), [ref](std::exception_ptr e, std::string result) {
            if (auto self = ref.lock()) {
                if (e) {
                    self->handle_error(e);
                } else {
                    self->handle_data(std::move(result));
                }
            }
        });
    }
}

boost::asio::awaitable<std::string> http_query::co_request(std::weak_ptr<http_query> ref, std::string path_query, http_headers headers, std::string body)
{
    if (auto self = ref.lock()) {
        if (auto context = self->m_context.lock()) {
            std::string full_url = "https://" + self->m_host + ":" + self->m_port + path_query;

            try {
                auto resolved_endpoints = co_await context::co_resolve(context, self->m_host, self->m_port);

                ssl_stream stream(co_await this_coro::executor, context->ssl());
                get_lowest_layer(stream).expires_after(context->timeout());
                co_await get_lowest_layer(stream).async_connect(resolved_endpoints, use_awaitable);

                if (!SSL_set_tlsext_host_name(stream.native_handle(), self->m_host.c_str()))
                    throw std::runtime_error("Failed to set SNI hostname");

                get_lowest_layer(stream).expires_after(context->timeout());
                co_await stream.async_handshake(ssl::stream_base::client, use_awaitable);

                http::request<http::string_body> req(self->m_verb, path_query, 11);
                req.set(http::field::host, self->m_host);

                for (const auto& [name, value] : headers)
                    req.set(name, value);

                if (!body.empty()) {
                    req.set(http::field::content_type, "application/json");
                    req.body() = std::move(body);
                }

                req.prepare_payload();

                get_lowest_layer(stream).expires_after(context->timeout());
                co_await http::async_write(stream, req, use_awaitable);

                flat_buffer buffer;
                http::response<http::string_body> response;
                co_await http::async_read(stream, buffer, response, use_awaitable);

                get_lowest_layer(stream).expires_never();

                if (response.result() != http::status::ok)
                    throw_http_error(response.result(), full_url);

                co_return response.body();
            }
            catch (const boost::system::system_error& e) {
                // Check if this is a domain resolution error
                const auto& error_code = e.code();
                if (error_code.category() == boost::asio::error::get_netdb_category() || error_code.category() == boost::asio::error::get_addrinfo_category()) {
                    throw_domain_error(error_code, self->m_host);
                }
                throw;
            }
        }
    }
    co_return "";
}

} // namespace scratcher::connect
