// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_CONNECTION_ERRORS_HPP
#define SCRATCHER_CONNECTION_ERRORS_HPP

#include <exception>
#include <string>
#include <memory>
#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>

namespace scratcher::connect {

class connection_error : public std::exception
{
protected:
    std::string m_message;
    boost::system::error_code m_error_code;

public:
    explicit connection_error(const boost::system::error_code& ec, const std::string& context = "")
        : m_error_code(ec)
    {
        if (context.empty()) {
            m_message = ec.message();
        } else {
            m_message = context + ": " + ec.message();
        }
    }

    explicit connection_error(const std::string& message)
        : m_message(message)
    {
    }

    const char* what() const noexcept override
    {
        return m_message.c_str();
    }

    const boost::system::error_code& error_code() const noexcept
    {
        return m_error_code;
    }
};

class domain_error : public connection_error
{
    std::string m_host;

public:
    explicit domain_error(const boost::system::error_code& ec, const std::string& host)
        : connection_error(ec), m_host(host)
    {
        m_message = "Host resolution failed for '" + m_host + "': " + ec.message();
    }

    const std::string& host() const noexcept
    {
        return m_host;
    }
};

class http_error : public connection_error
{
protected:
    boost::beast::http::status m_status;
    std::string m_url;

public:
    explicit http_error(boost::beast::http::status status, const std::string& url)
        : connection_error(""), m_status(status), m_url(url)
    {
        m_message = std::to_string(static_cast<unsigned>(status)) + "/" +
                   std::string(boost::beast::http::obsolete_reason(status)) + ": " + url;
    }

    boost::beast::http::status status() const noexcept
    {
        return m_status;
    }

    const std::string& url() const noexcept
    {
        return m_url;
    }
};

class http_redirect_error : public http_error
{
public:
    explicit http_redirect_error(boost::beast::http::status status, const std::string& url)
        : http_error(status, url)
    {
        if (static_cast<unsigned>(status) < 300 || static_cast<unsigned>(status) >= 400) {
            throw std::invalid_argument("http_redirect_error requires 3xx status code");
        }
    }
};

class http_client_error : public http_error
{
public:
    explicit http_client_error(boost::beast::http::status status, const std::string& url)
        : http_error(status, url)
    {
        if (static_cast<unsigned>(status) < 400 || static_cast<unsigned>(status) >= 500) {
            throw std::invalid_argument("http_client_error requires 4xx status code");
        }
    }
};

class http_server_error : public http_error
{
public:
    explicit http_server_error(boost::beast::http::status status, const std::string& url)
        : http_error(status, url)
    {
        if (static_cast<unsigned>(status) < 500 || static_cast<unsigned>(status) >= 600) {
            throw std::invalid_argument("http_server_error requires 5xx status code");
        }
    }
};

inline std::unique_ptr<domain_error> make_domain_error(const boost::system::error_code& ec, const std::string& host)
{
    return std::make_unique<domain_error>(ec, host);
}

inline std::unique_ptr<http_error> make_http_error(boost::beast::http::status status, const std::string& url)
{
    auto status_code = static_cast<unsigned>(status);

    if (status_code >= 300 && status_code < 400) {
        return std::make_unique<http_redirect_error>(status, url);
    } else if (status_code >= 400 && status_code < 500) {
        return std::make_unique<http_client_error>(status, url);
    } else if (status_code >= 500 && status_code < 600) {
        return std::make_unique<http_server_error>(status, url);
    } else {
        return std::make_unique<http_error>(status, url);
    }
}

inline void throw_domain_error(const boost::system::error_code& ec, const std::string& host)
{
    throw domain_error(ec, host);
}

inline void throw_http_error(boost::beast::http::status status, const std::string& url)
{
    auto status_code = static_cast<unsigned>(status);

    if (status_code >= 300 && status_code < 400) {
        throw http_redirect_error(status, url);
    } else if (status_code >= 400 && status_code < 500) {
        throw http_client_error(status, url);
    } else if (status_code >= 500 && status_code < 600) {
        throw http_server_error(status, url);
    } else {
        throw http_error(status, url);
    }
}

} // namespace scratcher::connect

#endif // SCRATCHER_CONNECTION_ERRORS_HPP
