// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_HTTP_QUERY
#define SCRATCHER_HTTP_QUERY

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url.hpp>

#include "connection_context.hpp"
#include "generic_handler.hpp"

namespace scratcher::connect {

using http_headers = std::vector<std::pair<std::string, std::string>>;
using namespace boost::beast;

class http_query : public std::enable_shared_from_this<http_query>
{
    std::weak_ptr<context> m_context;
    std::string m_host;
    std::string m_port;
    std::string m_path;
    std::string m_query;
    http::verb m_verb;

public:
    explicit http_query(std::shared_ptr<context> context, http::verb verb, const std::string& url);
    virtual ~http_query() = default;

    template<typename DataAcceptor, typename ErrorHandler>
    static std::shared_ptr<http_query> create(std::shared_ptr<context> ctx, http::verb verb, const std::string& url, DataAcceptor&& data_handler, ErrorHandler&& error_handler)
    {
        return std::static_pointer_cast<http_query>(std::make_shared<generic_handler<std::string&&, http_query, DataAcceptor, ErrorHandler, std::shared_ptr<context>, http::verb, const std::string&>>(std::forward<DataAcceptor>(data_handler), std::forward<ErrorHandler>(error_handler), std::move(ctx), std::move(verb), url));
    }

    template<typename DataAcceptor, typename ErrorHandler>
    static std::shared_ptr<http_query> create(std::shared_ptr<context> ctx, const std::string& url, DataAcceptor&& data_handler, ErrorHandler&& error_handler)
    {
        return create(std::move(ctx), http::verb::get, url, std::forward<DataAcceptor>(data_handler), std::forward<ErrorHandler>(error_handler));
    }

    void operator()(std::string query = {}, http_headers headers = {}, std::string body = {});

    virtual void handle_data(std::string&& data) = 0;
    virtual void handle_error(std::exception_ptr eptr) = 0;

private:
    static boost::asio::awaitable<std::string> co_request(std::weak_ptr<http_query> ref, std::string path_query, http_headers headers, std::string body);
};


} // namespace scratcher::connect

#endif // SCRATCHER_HTTP_QUERY
