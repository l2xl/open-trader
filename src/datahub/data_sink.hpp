// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef DATAHUB_DATA_SINK_HPP
#define DATAHUB_DATA_SINK_HPP

#include <memory>
#include <exception>
#include <concepts>
#include <ranges>
#include <iostream>
#include <boost/lockfree/spsc_queue.hpp>

#include "data_model.hpp"
#include "generic_handler.hpp"

namespace datahub {

using scratcher::generic_handler;

template<typename D, typename H>
class data_adapter
{
public:
    using data_type = D;
    using handler_type = H;

private:
    handler_type m_handler;

public:
    explicit data_adapter(handler_type&& h) : m_handler(std::forward<H>(h))
    {}

    bool operator()(const std::string& json_data) {
        std::clog << "Try JSON: " << json_data << std::endl;
        try {
            data_type result{};
            auto err = glz::read<glz::opts{.error_on_unknown_keys = false, .error_on_missing_keys = true}>(result, json_data);
            if (!err) {
                m_handler(std::move(result));
                return true;
            }
            std::clog << "Failed to read json data for type '" << typeid(data_type).name() << "':\n"
                          << "  Error: " << glz::format_error(err, json_data) << std::endl;
        }
        catch (...) {
            std::cerr << "Unknown exception while parsing json for type '" << typeid(data_type).name() << "'" << std::endl;
        }
        return false;
    }
};

template <typename D, typename H>
data_adapter<D, H> make_data_adapter(H&& h)
{ return data_adapter<D, H>(std::forward<H>(h)); }

template<typename... Acceptor>
class data_dispatcher
{
public:
    using acceptor_tuple_type = std::tuple<Acceptor...>;
    using queue_type = boost::lockfree::spsc_queue<std::string>;
    using executor_type = boost::asio::any_io_executor;

private:
    std::shared_ptr<boost::lockfree::spsc_queue<std::string>> m_data_queue;
    std::shared_ptr<acceptor_tuple_type> m_acceptors;
    boost::asio::strand<executor_type> m_dispatch_strand;

public:
    explicit data_dispatcher(boost::asio::any_io_executor executor, Acceptor&&... acceptors)
        : m_data_queue(std::make_shared<queue_type>(1024))
        , m_acceptors(std::make_shared<acceptor_tuple_type>(std::forward<Acceptor>(acceptors)...))
        , m_dispatch_strand(boost::asio::make_strand(std::move(executor)))
    { }

    data_dispatcher(const data_dispatcher&) = default;
    data_dispatcher(data_dispatcher&&) = default;
    data_dispatcher& operator=(const data_dispatcher&) = default;
    data_dispatcher& operator=(data_dispatcher&&) = default;

    void operator()(std::string data) const
    {
        if (!m_data_queue->push(std::move(data))) {
            // Queue is full, data is discarded
            return;
        }

        auto acceptors_ref = std::weak_ptr<acceptor_tuple_type>(m_acceptors);
        auto queue_ref = std::weak_ptr<queue_type>(m_data_queue);

        // Post for async dispatching
        boost::asio::post(m_dispatch_strand, [=] {
            process_queue(queue_ref, acceptors_ref);
        });
    }

private:
    static void process_queue(std::weak_ptr<queue_type> queue_ref, std::weak_ptr<acceptor_tuple_type> acceptors_ref)
    {
        auto queue = queue_ref.lock();
        auto acceptors = acceptors_ref.lock();

        if (queue && acceptors) {
            std::string data;
            while (queue->pop(data)) {
                // Try every acceptor in sequence
                try_acceptors(*acceptors, data, std::index_sequence_for<Acceptor...>{});
            }
        }
    }

    template<std::size_t... Indices>
    static bool try_acceptors(acceptor_tuple_type& acceptors, const std::string& data, std::index_sequence<Indices...>)
    {
        // Try each acceptor in sequence using fold expression
        return (try_accept<Indices>(acceptors, data) || ...);
    }

    template<std::size_t Index>
    static bool try_accept(acceptor_tuple_type& acceptors, const std::string& data)
    {
        auto& acceptor = std::get<Index>(acceptors);
        return acceptor(data);
    }
};

template<typename... Acceptor>
data_dispatcher<Acceptor...> make_data_dispatcher(boost::asio::any_io_executor executor, Acceptor&& ... acceptors)
{ return data_dispatcher<Acceptor...>(std::move(executor), std::forward<Acceptor>(acceptors)...); }

template<typename Model>
class data_sink : public std::enable_shared_from_this<data_sink<Model>>
{
public:
    using model_type = Model;
    using entity_type = typename Model::entity_type;
    using cache_type = typename Model::cache_type;

    struct EnsurePrivate {};

private:
    std::shared_ptr<model_type> m_model;

public:
    data_sink(std::shared_ptr<model_type> model, EnsurePrivate)
        : m_model(std::move(model))
    { }

    virtual ~data_sink() = default;

    template<typename DataCallable, typename ErrorCallable>
    static std::shared_ptr<data_sink> create(std::shared_ptr<model_type> model, DataCallable&& data_handler, ErrorCallable&& error_hdl)
    {
        auto self = std::make_shared<generic_handler<cache_type&&, data_sink, DataCallable, ErrorCallable, std::shared_ptr<model_type>, EnsurePrivate>>(
            std::forward<DataCallable>(data_handler), std::forward<ErrorCallable>(error_hdl), std::move(model), EnsurePrivate{});

        return std::static_pointer_cast<data_sink>(self);
    }

    std::shared_ptr<model_type> model() const
    { return m_model; }

    template<std::ranges::input_range Range>
    requires std::convertible_to<std::ranges::range_value_t<Range>, entity_type>
    void accept(Range&& data)
    {
        cache_type cache(std::ranges::begin(data), std::ranges::end(data));
        handle_data(cache_type(cache));
        m_model->accept(std::move(cache));
    }

    template<std::ranges::input_range Range>
    requires std::convertible_to<std::ranges::range_value_t<Range>, entity_type>
    auto data_acceptor()
    {
        std::weak_ptr<data_sink> ref = data_sink::weak_from_this();
        return [=](Range&& entities) {
            if (auto self = ref.lock())
                self->template accept<Range>(std::forward<Range>(entities));
        };
    }

    virtual void handle_data(cache_type&& data) = 0;
    virtual void handle_error(std::exception_ptr eptr) = 0;
};

template<typename Model, typename Acceptor, typename ErrorCallable>
auto make_data_sink(std::shared_ptr<Model> model, Acceptor&& data_acceptor, ErrorCallable&& error_handler)
{
    return data_sink<Model>::create(std::move(model), std::forward<Acceptor>(data_acceptor), std::forward<ErrorCallable>(error_handler));
}

} // namespace datahub

#endif // DATAHUB_DATA_SINK_HPP
