// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once
#include <type_traits>
#include <utility>
#include <stdexcept>

namespace scratcher {

template<typename PARENT, typename ERROR_CALLABLE, typename ... ARGS>
class error_handler: public PARENT
{
public:
    using error_callable = std::decay_t<ERROR_CALLABLE>;
private:
    error_callable m_error_handler;

public:
    error_handler(ERROR_CALLABLE&& error_handler, ARGS&& ... args)
        : PARENT(std::forward<ARGS>(args)...)
        , m_error_handler(std::forward<ERROR_CALLABLE>(error_handler))
    { }

    ~error_handler() override = default;

    void handle_error(std::exception_ptr eptr) override
    { m_error_handler(eptr); }
};

template<typename DATA, typename PARENT, typename DATA_CALLABLE, typename ERROR_CALLABLE, typename ... ARGS>
class generic_handler: public error_handler<PARENT, ERROR_CALLABLE, ARGS...>
{
    using base = error_handler<PARENT, ERROR_CALLABLE, ARGS...>;
public:
    using data_type = DATA;
    using data_callable = std::decay_t<DATA_CALLABLE>;

private:
    data_callable m_data_handler;

public:
    generic_handler(DATA_CALLABLE&& data_handler, ERROR_CALLABLE&& error_handler, ARGS&& ... args)
        : base(std::forward<ERROR_CALLABLE>(error_handler), std::forward<ARGS>(args)...)
        , m_data_handler(std::forward<DATA_CALLABLE>(data_handler))
    { }

    ~generic_handler() override = default;

    void handle_data(data_type&& data) override
    {
        try {
            m_data_handler(std::forward<data_type>(data));
        }
        catch (std::exception& e) {
            base::handle_error(std::current_exception());
        }
    }
};

// Partial specialization: no error callable (ERROR_CALLABLE = void)

template<typename DATA, typename PARENT, typename DATA_CALLABLE, typename ... ARGS>
class generic_handler<DATA, PARENT, DATA_CALLABLE, void, ARGS...> : public PARENT
{
public:
    using data_type = DATA;
    using data_callable = std::decay_t<DATA_CALLABLE>;

private:
    data_callable m_data_handler;

public:
    generic_handler(DATA_CALLABLE&& data_handler, ARGS&& ... args)
        : PARENT(std::forward<ARGS>(args)...)
        , m_data_handler(std::forward<DATA_CALLABLE>(data_handler))
    { }

    ~generic_handler() override = default;

    void handle_data(data_type&& data) override
    { m_data_handler(std::forward<data_type>(data)); }
};

}
