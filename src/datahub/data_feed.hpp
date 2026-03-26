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

#ifndef DATAHUB_DATA_FEED_HPP
#define DATAHUB_DATA_FEED_HPP

#include <memory>
#include <functional>
#include <deque>
#include <list>
#include <algorithm>
#include <concepts>
#include <ranges>
#include <set>

#include "data_subscription.hpp"

namespace datahub {

template<typename Entity, auto TimeField, auto KeyField>
class sorted_data_feed : public std::enable_shared_from_this<sorted_data_feed<Entity, TimeField, KeyField>>
{
public:
    using entity_type = Entity;
    using cache_type = std::deque<entity_type>;
    using filter_type = std::function<bool(const entity_type&)>;
    using subscription_type = data_subscription<Entity, cache_type>;

private:
    cache_type m_cache;
    std::list<std::weak_ptr<subscription_type>> m_subscriptions;
    const filter_type m_filter;

    using key_type = std::decay_t<decltype(std::declval<Entity>().*KeyField)>;

    void push_to_subscriptions(const cache_type& data, update_kind kind)
    {
        auto it = m_subscriptions.begin();
        while (it != m_subscriptions.end()) {
            if (auto sub = it->lock()) { sub->handle_update(kind, data); ++it; }
            else { it = m_subscriptions.erase(it); }
        }
    }

public:
    explicit sorted_data_feed(filter_type filter = {})
        : m_filter(std::move(filter))
    {}

    static std::shared_ptr<sorted_data_feed> create(filter_type filter = {})
    { return std::make_shared<sorted_data_feed>(std::move(filter)); }

    void subscribe(std::weak_ptr<subscription_type> sub)
    {
        if (!m_cache.empty())
            if (auto locked = sub.lock())
                locked->handle_update(update_kind::snapshot, m_cache);
        m_subscriptions.emplace_back(std::move(sub));
    }

    const cache_type& get_snapshot() const { return m_cache; }

    template<std::ranges::input_range Range>
    requires std::convertible_to<std::ranges::range_value_t<Range>, entity_type>
    auto data_acceptor()
    {
        std::weak_ptr<sorted_data_feed> ref = this->weak_from_this();
        return [ref](Range&& entities) {
            if (auto self = ref.lock()) {
                std::set<key_type> existing;
                for (const auto& e : self->m_cache)
                    existing.insert(e.*KeyField);

                cache_type accepted;
                for (auto&& e : entities) {
                    if (existing.contains(e.*KeyField))
                        continue;
                    existing.insert(e.*KeyField);

                    if (self->m_filter && !self->m_filter(e))
                        continue;

                    auto pos = std::lower_bound(self->m_cache.begin(), self->m_cache.end(), e, [](const entity_type& a, const entity_type& b) { return a.*TimeField < b.*TimeField; });
                    auto& inserted = *self->m_cache.insert(pos, std::forward<decltype(e)>(e));
                    accepted.emplace_back(inserted);
                }

                if (!accepted.empty())
                    self->push_to_subscriptions(accepted, update_kind::increment);
            }
        };
    }
};

template<typename Entity>
class snapshot_data_feed : public std::enable_shared_from_this<snapshot_data_feed<Entity>>
{
public:
    using entity_type = Entity;
    using cache_type = std::deque<entity_type>;
    using filter_type = std::function<bool(const entity_type&)>;
    using subscription_type = data_subscription<Entity, cache_type>;

private:
    cache_type m_cache;
    std::list<std::weak_ptr<subscription_type>> m_subscriptions;
    const filter_type m_filter;

    void push_to_subscriptions(const cache_type& data, update_kind kind)
    {
        auto it = m_subscriptions.begin();
        while (it != m_subscriptions.end()) {
            if (auto sub = it->lock()) { sub->handle_update(kind, data); ++it; }
            else { it = m_subscriptions.erase(it); }
        }
    }

public:
    explicit snapshot_data_feed(filter_type filter = {})
        : m_filter(std::move(filter))
    {}

    static std::shared_ptr<snapshot_data_feed> create(filter_type filter = {})
    { return std::make_shared<snapshot_data_feed>(std::move(filter)); }

    void subscribe(std::weak_ptr<subscription_type> sub)
    {
        if (!m_cache.empty())
            if (auto locked = sub.lock())
                locked->handle_update(update_kind::snapshot, m_cache);
        m_subscriptions.emplace_back(std::move(sub));
    }

    const cache_type& get_snapshot() const { return m_cache; }

    template<std::ranges::input_range Range>
    requires std::convertible_to<std::ranges::range_value_t<Range>, entity_type>
    auto data_acceptor()
    {
        std::weak_ptr<snapshot_data_feed> ref = this->weak_from_this();
        return [ref](Range&& entities) {
            if (auto self = ref.lock()) {
                self->m_cache.clear();
                for (auto&& e : entities) {
                    if (self->m_filter && !self->m_filter(e))
                        continue;
                    self->m_cache.emplace_back(std::forward<decltype(e)>(e));
                }
                self->push_to_subscriptions(self->m_cache, update_kind::snapshot);
            }
        };
    }
};

} // namespace datahub

#endif // DATAHUB_DATA_FEED_HPP
