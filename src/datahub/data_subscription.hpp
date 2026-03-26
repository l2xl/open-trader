// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b21tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#ifndef DATAHUB_DATA_SUBSCRIPTION_HPP
#define DATAHUB_DATA_SUBSCRIPTION_HPP

#include <memory>
#include <deque>
#include <type_traits>
#include <utility>

#include "data_update.hpp"

namespace datahub {

// Notification-only subscription endpoint. No cache — the feed owns the data.
// Container template parameter determines the range type passed to handle_update;
// defaults to std::deque for in-memory feeds, extensible to DB cursor container later.
template<typename Entity, typename Container = std::deque<Entity>>
class data_subscription
{
public:
    using entity_type = Entity;
    using container_type = Container;

    virtual ~data_subscription() = default;
    virtual void handle_update(update_kind kind, const container_type& data) = 0;
};

// Concrete handler: stores the callable as a compile-time template parameter.
// Follows the same pattern as generic_handler / error_handler in generic_handler.hpp.
template<typename Entity, typename Container, typename Callable>
class subscription_handler : public data_subscription<Entity, Container>
{
    using callable_type = std::decay_t<Callable>;
    callable_type mHandler;

public:
    explicit subscription_handler(Callable&& handler)
        : mHandler(std::forward<Callable>(handler))
    {}

    void handle_update(update_kind kind, const Container& data) override
    { mHandler(kind, data); }
};

template<typename Entity, typename Container = std::deque<Entity>, typename Callable>
auto make_data_subscription(Callable&& handler)
{
    return std::static_pointer_cast<data_subscription<Entity, Container>>(
        std::make_shared<subscription_handler<Entity, Container, Callable>>(std::forward<Callable>(handler)));
}

} // namespace datahub

#endif // DATAHUB_DATA_SUBSCRIPTION_HPP
