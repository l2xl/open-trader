// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_HTTP_QUERY
#define SCRATCHER_HTTP_QUERY

#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <utility>
#include <stdexcept>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url.hpp>

#include "connection_context.hpp"
#include "connection_errors.hpp"
#include "generic_handler.hpp"

namespace scratcher::connect {

using namespace boost::beast;

using http_headers = std::vector<std::pair<std::string, std::string>>;

struct http_request {
    http::verb verb;
    std::string target;
    std::string body;
    http_headers headers;
};

struct no_request_policy {
    template<typename Query>
    void operator()(Query&, http_request&) const {}
};

template<typename RequestPolicy = no_request_policy>
class http_query : public std::enable_shared_from_this<http_query<RequestPolicy>>
{
    friend RequestPolicy;

    RequestPolicy m_policy;
    std::weak_ptr<context> m_context;
    std::string m_host;
    std::string m_port;
    std::string m_path;
    std::string m_query;
    http::verb m_verb;

public:
    explicit http_query(std::shared_ptr<context> ctx, http::verb verb, const std::string& url, RequestPolicy policy = {});
    virtual ~http_query() = default;

    template<typename DataAcceptor, typename ErrorHandler>
    static std::shared_ptr<http_query> create(std::shared_ptr<context> ctx, http::verb verb, const std::string& url, RequestPolicy policy, DataAcceptor&& data_handler, ErrorHandler&& error_handler)
    {
        return std::static_pointer_cast<http_query>(std::make_shared<cex::generic_handler<std::string&&, http_query, DataAcceptor, ErrorHandler, std::shared_ptr<context>, http::verb, const std::string&, RequestPolicy>>(std::forward<DataAcceptor>(data_handler), std::forward<ErrorHandler>(error_handler), std::move(ctx), std::move(verb), url, std::move(policy)));
    }

    template<typename DataAcceptor, typename ErrorHandler>
    static std::shared_ptr<http_query> create(std::shared_ptr<context> ctx, http::verb verb, const std::string& url, DataAcceptor&& data_handler, ErrorHandler&& error_handler)
    {
        return create(std::move(ctx), verb, url, RequestPolicy{}, std::forward<DataAcceptor>(data_handler), std::forward<ErrorHandler>(error_handler));
    }

    template<typename DataAcceptor, typename ErrorHandler>
    static std::shared_ptr<http_query> create(std::shared_ptr<context> ctx, const std::string& url, DataAcceptor&& data_handler, ErrorHandler&& error_handler)
    {
        return create(std::move(ctx), http::verb::get, url, RequestPolicy{}, std::forward<DataAcceptor>(data_handler), std::forward<ErrorHandler>(error_handler));
    }

    void operator()(std::string query = {}, std::string body = {});

    virtual void handle_data(std::string&& data) = 0;
    virtual void handle_error(std::exception_ptr eptr) = 0;

private:
    static boost::asio::awaitable<std::string> co_request(std::weak_ptr<http_query> ref, std::string path_query, std::string body);
};


template<typename RequestPolicy>
http_query<RequestPolicy>::http_query(std::shared_ptr<context> ctx, http::verb verb, const std::string& url, RequestPolicy policy)
    : m_policy(std::move(policy)), m_context(ctx), m_verb(verb)
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

        boost::asio::co_spawn(ctx->io(), context::co_resolve(ctx, m_host, m_port), boost::asio::detached);
    }
    catch (...) {
        std::throw_with_nested(std::invalid_argument("Invalid URL: " + url));
    }
}

template<typename RequestPolicy>
void http_query<RequestPolicy>::operator()(std::string query, std::string body)
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

    std::weak_ptr<http_query> ref = this->weak_from_this();
    if (auto ctx = m_context.lock())
    {
        boost::asio::co_spawn(ctx->io(), co_request(ref, std::move(path_query), std::move(body)), [ref](std::exception_ptr e, std::string result) {
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

template<typename RequestPolicy>
boost::asio::awaitable<std::string> http_query<RequestPolicy>::co_request(std::weak_ptr<http_query> ref, std::string path_query, std::string body)
{
    using boost::asio::use_awaitable;
    namespace this_coro = boost::asio::this_coro;

    if (auto self = ref.lock()) {
        if (auto ctx = self->m_context.lock()) {
            std::string full_url = "https://" + self->m_host + ":" + self->m_port + path_query;

            try {
                auto resolved_endpoints = co_await context::co_resolve(ctx, self->m_host, self->m_port);

                ssl_stream<tcp_stream> stream(co_await this_coro::executor, ctx->ssl());
                get_lowest_layer(stream).expires_after(ctx->timeout());
                co_await get_lowest_layer(stream).async_connect(resolved_endpoints, use_awaitable);

                if (!SSL_set_tlsext_host_name(stream.native_handle(), self->m_host.c_str()))
                    throw std::runtime_error("Failed to set SNI hostname");

                get_lowest_layer(stream).expires_after(ctx->timeout());
                co_await stream.async_handshake(ssl::stream_base::client, use_awaitable);

                http_request request{.verb = self->m_verb, .target = std::move(path_query), .body = std::move(body)};
                self->m_policy(*self, request);

                http::request<http::string_body> req(request.verb, request.target, 11);
                req.set(http::field::host, self->m_host);
                for (const auto& [name, value] : request.headers)
                    req.set(name, value);

                if (!request.body.empty()) {
                    req.set(http::field::content_type, "application/json");
                    req.body() = std::move(request.body);
                }

                req.prepare_payload();

                get_lowest_layer(stream).expires_after(ctx->timeout());
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

#endif // SCRATCHER_HTTP_QUERY
