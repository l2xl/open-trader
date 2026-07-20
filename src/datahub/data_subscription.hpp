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

// A single `subscription<Range, Extra...>` template with two partial
// specialisations, one per feed shape:
//
//   subscription<Range>                                   — snapshot-only feeds
//     (sorted_snapshot_data_feed, keyed_snapshot_data_feed). Dispatch passes
//     just (update_kind, const Range&).
//
//   subscription<Range, It, It>                           — incremental feeds
//     (sorted_data_feed). Dispatch passes the changed window directly as
//     [first, last) — no synthetic iterators for snapshots-as-increments.
//
// Polymorphism uses the single virtual `handle_data` of whichever spec the
// feed picked; the impl holds the user's Callable as a direct member.
template<std::ranges::input_range Range, typename... Extra>
class data_subscription;

template<std::ranges::input_range Range>
class data_subscription<Range>
{
public:
    using range_type = Range;
    virtual ~data_subscription() = default;
    virtual void handle_data(update_kind kind, const Range& full) = 0;
};

template<std::ranges::input_range Range, typename It>
class data_subscription<Range, It>
{
public:
    using range_type = Range;
    using const_iterator = It;
    virtual ~data_subscription() = default;
    virtual void handle_data(update_kind kind, const Range& full, It first, It last) = 0;
};

namespace detail {

template<std::ranges::input_range Range, typename Callable, typename... Extra>
class subscription_impl;

template<std::ranges::input_range Range, typename Callable>
class subscription_impl<Range, Callable> : public data_subscription<Range>
{
    Callable m_cb;
public:
    template<typename C>
    explicit subscription_impl(C&& cb) : m_cb(std::forward<C>(cb)) {}
    void handle_data(update_kind kind, const Range& full) override
    { m_cb(kind, full); }
};

template<std::ranges::input_range Range, typename Callable, typename It>
class subscription_impl<Range, Callable, It> : public data_subscription<Range, It>
{
    Callable m_cb;
public:
    template<typename C>
    explicit subscription_impl(C&& cb) : m_cb(std::forward<C>(cb)) {}
    void handle_data(update_kind kind, const Range& full, It first, It last) override
    { m_cb(kind, full, first, last); }
};

template<typename> inline constexpr bool dependent_false_v = false;

} // namespace detail

// Single factory — statically dispatches on the Callable's arity to the matching
// subscription spec. Wrong-arity callables fail at the static_assert with a
// readable diagnostic; right-arity callables for the wrong feed kind fail at
// the feed's subscribe() call where the shared_ptr conversion is rejected.
template<std::ranges::input_range Range, typename Callable>
auto make_subscription(Callable&& cb)
{
    using cb_t = std::decay_t<Callable>;
    using It   = std::ranges::iterator_t<const Range>;

    if constexpr (std::is_invocable_v<cb_t&, update_kind, const Range&>) {
        return std::shared_ptr<data_subscription<Range>>(
            std::make_shared<detail::subscription_impl<Range, cb_t>>(std::forward<Callable>(cb)));
    }
    else if constexpr (std::is_invocable_v<cb_t&, update_kind, const Range&, It, It>) {
        return std::shared_ptr<data_subscription<Range, It>>(
            std::make_shared<detail::subscription_impl<Range, cb_t, It>>(std::forward<Callable>(cb)));
    }
    else {
        static_assert(detail::dependent_false_v<cb_t>,
                      "datahub::make_subscription requires a Callable invocable as "
                      "(update_kind, const Range&) for snapshot-only feeds, or "
                      "(update_kind, const Range&, const_iterator, const_iterator) "
                      "for incremental feeds.");
    }
}

} // namespace datahub

#endif // DATAHUB_DATA_SUBSCRIPTION_HPP
