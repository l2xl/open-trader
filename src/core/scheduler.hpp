// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef WORK_SCHEDULER_HPP
#define WORK_SCHEDULER_HPP

#include <thread>
#include <list>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/spawn.hpp>

namespace scratcher {

using std::move;
using std::forward;

enum class error:int { success, no_host_name, no_time_sync, already_opened, connection_error };

class xscratcher_error_category_impl : public boost::system::error_category
{
public:
    virtual ~xscratcher_error_category_impl() = default;

    static xscratcher_error_category_impl instance;
    const char* name() const noexcept override { return "xscratcher"; }
    std::string message(int ev) const override;
    char const* message(int ev, char* buffer, std::size_t len) const noexcept override;
};

inline boost::system::error_category& xscratcher_error_category()
{ return xscratcher_error_category_impl::instance; }

inline boost::system::error_code xscratcher_error_code(error e)
{ return boost::system::error_code(static_cast<int>(e), xscratcher_error_category());}

using boost::asio::io_context;
using boost::asio::use_awaitable;
using boost::asio::detached;

namespace ssl = boost::asio::ssl;

class scheduler: public std::enable_shared_from_this<scheduler> {
    io_context m_io_ctx;
    boost::asio::executor_work_guard<io_context::executor_type> m_io_guard;
    std::list<std::thread> m_threads;

    struct EnsurePrivate {};
public:
    explicit scheduler(EnsurePrivate);
    virtual ~scheduler();

    static std::shared_ptr<scheduler> create(size_t threads);

    io_context& io() {return m_io_ctx; }
};
}

#endif //WORK_SCHEDULER_HPP
