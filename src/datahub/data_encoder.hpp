// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_DATAHUB_DATA_ENCODER_HPP
#define SCRATCHER_DATAHUB_DATA_ENCODER_HPP

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <glaze/glaze.hpp>

#include "currency.hpp"

namespace datahub {

namespace detail {

template<typename T> struct query_value { using type = T; };
template<typename T> struct query_value<std::optional<T>> { using type = T; };

template<typename T>
concept query_scalar = std::is_arithmetic_v<T> || std::is_enum_v<T> || std::same_as<T, std::string> || scratcher::is_currency_v<T>;

template<typename T>
concept query_field = query_scalar<typename query_value<std::remove_cvref_t<T>>::type>;

template<typename Entity>
constexpr bool flat_query_entity = []<std::size_t... I>(std::index_sequence<I...>) {
    return (query_field<decltype(glz::get<I>(glz::to_tie(std::declval<Entity&>())))> && ...);
}(std::make_index_sequence<glz::reflect<Entity>::size>{});

inline std::string percent_encode(std::string_view text)
{
    constexpr std::string_view hex_digits = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(text.size());
    for (unsigned char c : text) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            encoded.push_back(static_cast<char>(c));
        }
        else {
            encoded.push_back('%');
            encoded.push_back(hex_digits[c >> 4]);
            encoded.push_back(hex_digits[c & 0x0F]);
        }
    }
    return encoded;
}

template<typename T>
std::string json_scalar_text(const T& value)
{
    if constexpr (std::same_as<T, std::string>) {
        return value;
    }
    else {
        auto json = glz::write_json(value);
        if (!json) throw std::runtime_error(glz::format_error(json.error()));
        std::string text = std::move(*json);
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
            return text.substr(1, text.size() - 2);
        return text;
    }
}

template<typename T>
void append_query_field(std::string& query, std::string_view key, const T& value)
{
    if constexpr (std::same_as<T, std::optional<typename query_value<T>::type>>) {
        if (value) append_query_field(query, key, *value);
    }
    else {
        if (!query.empty()) query.push_back('&');
        query.append(key).push_back('=');
        query.append(percent_encode(json_scalar_text(value)));
    }
}

template<typename T> struct is_shared_ptr : std::false_type {};
template<typename T> struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template<typename Acceptor>
concept outbound_acceptor_pointer = is_shared_ptr<Acceptor>::value && requires(Acceptor& next, std::string query, std::string body) { (*next)(std::move(query), std::move(body)); };

template<typename Acceptor>
void accept_request(Acceptor& next, std::string query, std::string body)
{
    if constexpr (outbound_acceptor_pointer<Acceptor>)
        (*next)(std::move(query), std::move(body));
    else
        next(std::move(query), std::move(body));
}

} // namespace detail

template<typename Acceptor>
concept outbound_acceptor = std::invocable<Acceptor&, std::string, std::string> || detail::outbound_acceptor_pointer<Acceptor>;

template<typename Entity, outbound_acceptor Acceptor, typename Projection = std::identity>
class json_body_encoder
{
    Acceptor m_next;
    Projection m_project;

public:
    json_body_encoder(Acceptor next, Projection project) : m_next(std::move(next)), m_project(std::move(project)) {}

    void operator()(Entity&& entity)
    {
        auto body = glz::write_json(std::invoke(m_project, std::move(entity)));
        if (!body) throw std::runtime_error(glz::format_error(body.error()));
        detail::accept_request(m_next, std::string{}, std::move(*body));
    }
};

template<typename Entity, typename Acceptor, typename Projection = std::identity>
    requires outbound_acceptor<std::decay_t<Acceptor>>
auto make_json_body_encoder(Acceptor&& next, Projection&& project = {})
{
    return json_body_encoder<Entity, std::decay_t<Acceptor>, std::decay_t<Projection>>(std::forward<Acceptor>(next), std::forward<Projection>(project));
}

template<typename Entity, outbound_acceptor Acceptor>
class url_query_encoder
{
    static_assert(detail::flat_query_entity<Entity>, "url query entities must be flat: every member a scalar (arithmetic, bool, enum, std::string, currency) or std::optional of one");

    Acceptor m_next;

public:
    explicit url_query_encoder(Acceptor next) : m_next(std::move(next)) {}

    void operator()(Entity&& entity)
    {
        std::string query;
        auto tie = glz::to_tie(entity);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (detail::append_query_field(query, glz::reflect<Entity>::keys[I], glz::get<I>(tie)), ...);
        }(std::make_index_sequence<glz::reflect<Entity>::size>{});
        detail::accept_request(m_next, std::move(query), std::string{});
    }
};

template<typename Entity, typename Acceptor>
    requires outbound_acceptor<std::decay_t<Acceptor>>
auto make_url_query_encoder(Acceptor&& next)
{
    return url_query_encoder<Entity, std::decay_t<Acceptor>>(std::forward<Acceptor>(next));
}

} // namespace datahub

#endif // SCRATCHER_DATAHUB_DATA_ENCODER_HPP
