// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef DATAHUB_DATA_SUBSCRIPTION_HPP
#define DATAHUB_DATA_SUBSCRIPTION_HPP

#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

#include "data_update.hpp"

namespace datahub {

// A single `data_subscription<View, Extra...>` template with two partial
// specialisations, one per feed shape:
//
//   data_subscription<View>                               — snapshot-only feeds
//     (sorted_snapshot_data_feed, keyed_snapshot_data_feed). Dispatch passes
//     just (update_kind, View).
//
//   data_subscription<View, Window>                       — incremental feeds
//     (sorted_data_feed). Dispatch passes the full view plus the changed
//     window as a second view — no synthetic windows for snapshots-as-increments.
//
// A view is whatever the feed builds over its cache for that subscriber (a lazy
// filter by the subscriber's own condition); the contract never names the cache
// container. Views are passed by value: they are cheap handles, and a
// filter_view has to be non-const to be iterated. They borrow the cache for the
// duration of the callback and must not be kept.
// Polymorphism uses the single virtual `handle_data` of whichever spec the
// feed picked; the impl holds the user's Callable as a direct member.
template<std::ranges::view View, typename... Extra>
class data_subscription;

template<std::ranges::view View>
class data_subscription<View>
{
public:
    using view_type = View;
    virtual ~data_subscription() = default;
    virtual void handle_data(update_kind kind, View full) = 0;
};

template<std::ranges::view View, std::ranges::view Window>
class data_subscription<View, Window>
{
public:
    using view_type = View;
    using window_type = Window;
    virtual ~data_subscription() = default;
    virtual void handle_data(update_kind kind, View full, Window window) = 0;
};

// The default gate of a subscription: a delivery reaches the handler only when the decisive
// view — the window of an increment, the full view of a snapshot — holds a record inside the
// subscriber's condition. The feed pushes every update; what to suppress is decided here, at
// the subscription end, and any other gate (or `[](auto&&...) { return true; }` for none) can
// be passed to make_subscription instead.
struct skip_empty
{
    template<std::ranges::view View>
    bool operator()(update_kind, View& full) const { return full.begin() != full.end(); }

    template<std::ranges::view View, std::ranges::view Window>
    bool operator()(update_kind, View&, Window& window) const { return window.begin() != window.end(); }
};

namespace detail {

template<std::ranges::view View, typename Callable, typename Gate, typename... Extra>
class subscription_impl;

template<std::ranges::view View, typename Callable, typename Gate>
class subscription_impl<View, Callable, Gate> : public data_subscription<View>
{
    Callable m_cb;
    Gate m_gate;
public:
    template<typename C, typename G>
    subscription_impl(C&& cb, G&& gate) : m_cb(std::forward<C>(cb)), m_gate(std::forward<G>(gate)) {}
    void handle_data(update_kind kind, View full) override
    {
        if (m_gate(kind, full))
            m_cb(kind, std::move(full));
    }
};

template<std::ranges::view View, typename Callable, typename Gate, std::ranges::view Window>
class subscription_impl<View, Callable, Gate, Window> : public data_subscription<View, Window>
{
    Callable m_cb;
    Gate m_gate;
public:
    template<typename C, typename G>
    subscription_impl(C&& cb, G&& gate) : m_cb(std::forward<C>(cb)), m_gate(std::forward<G>(gate)) {}
    void handle_data(update_kind kind, View full, Window window) override
    {
        if (m_gate(kind, full, window))
            m_cb(kind, std::move(full), std::move(window));
    }
};

template<typename> inline constexpr bool dependent_false_v = false;

} // namespace detail

// Single factory keyed on the feed, so callers never spell view types —
// statically dispatches on the Callable's arity to the matching subscription
// spec. Wrong-arity callables fail at the static_assert with a readable
// diagnostic; right-arity callables for the wrong feed kind fail at the feed's
// subscribe() call where the shared_ptr conversion is rejected.
template<typename Feed, typename Callable, typename Gate = skip_empty>
auto make_subscription(Callable&& cb, Gate&& gate = {})
{
    using cb_t = std::decay_t<Callable>;
    using gate_t = std::decay_t<Gate>;
    using View = typename Feed::view_type;

    if constexpr (std::is_invocable_v<cb_t&, update_kind, View>) {
        return std::shared_ptr<data_subscription<View>>(
            std::make_shared<detail::subscription_impl<View, cb_t, gate_t>>(std::forward<Callable>(cb), std::forward<Gate>(gate)));
    }
    else if constexpr (std::is_invocable_v<cb_t&, update_kind, View, View>) {
        return std::shared_ptr<data_subscription<View, View>>(
            std::make_shared<detail::subscription_impl<View, cb_t, gate_t, View>>(std::forward<Callable>(cb), std::forward<Gate>(gate)));
    }
    else {
        static_assert(detail::dependent_false_v<cb_t>,
                      "datahub::make_subscription<Feed> requires a Callable invocable as "
                      "(update_kind, Feed::view_type) for snapshot-only feeds, or "
                      "(update_kind, Feed::view_type, Feed::view_type) for incremental feeds.");
    }
}

} // namespace datahub

#endif // DATAHUB_DATA_SUBSCRIPTION_HPP
