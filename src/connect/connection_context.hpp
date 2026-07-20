// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_CONNECT_CONTEXT_HPP
#define SCRATCHER_CONNECT_CONTEXT_HPP

#include <memory>
#include <string>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/container/flat_map.hpp>

namespace scratcher {
using boost::asio::io_context;
namespace ssl = boost::asio::ssl;
}

namespace scratcher::connect {

/**
 * @brief ConnectionContext manages shared connection infrastructure
 *
 * This class provides common functionality needed by both JSON-RPC and WebSocket connections:
 * - IO context reference for async operations
 * - SSL context for secure connections
 * - Host resolution and caching with strand-based thread safety
 * - Connection parameters management
 *
 * The context is shared between multiple connection instances to avoid duplicate
 * DNS resolution and provide consistent connection parameters.
 */
class context : public std::enable_shared_from_this<context>
{
public:
    // Key for resolution cache (host:port)
    using HostPortKey = std::string;

private:
    std::reference_wrapper<io_context> m_io_ctx;

    // SSL context for secure connections
    ssl::context m_ssl_ctx;

    // Resolution cache with strand for thread safety
    boost::asio::strand<io_context::executor_type> m_resolution_strand;
    boost::container::flat_map<HostPortKey, boost::asio::ip::tcp::resolver::results_type> m_resolution_cache;

    // Connection parameters
    std::chrono::milliseconds m_timeout;

    struct ensure_private {};
public:
    /**
     * @brief Construct ConnectionContext with required parameters
     * @param io_ctx IO context for async operations
     * @param timeout Request timeout duration
     */
    context(io_context& io_ctx, std::chrono::milliseconds timeout, ensure_private)
        : m_io_ctx(io_ctx)
        , m_ssl_ctx(ssl::context::tlsv12_client)
        , m_resolution_strand(boost::asio::make_strand(m_io_ctx.get()))
        , m_timeout(timeout)
    {}

    /**
     * @brief Create ConnectionContext
     */
    static std::shared_ptr<context> create(
        io_context& io_ctx,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(10000));

    // Accessors
    io_context& io() { return m_io_ctx.get(); }
    ssl::context& ssl() { return m_ssl_ctx; }
    std::chrono::milliseconds timeout() const { return m_timeout; }
    
    /**
     * @brief Resolve host and port, using cache if available
     * @param host Remote host name or IP address
     * @param port Remote port number
     * @return awaitable that completes with resolved endpoints
     */
    static boost::asio::awaitable<boost::asio::ip::tcp::resolver::results_type>
    co_resolve(std::shared_ptr<context> self, std::string host, std::string port);
};

} // namespace scratcher::connect

#endif // SCRATCHER_CONNECT_CONTEXT_HPP
