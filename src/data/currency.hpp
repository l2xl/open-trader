// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_DATA_CURRENCY_HPP
#define SCRATCHER_DATA_CURRENCY_HPP

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace scratcher {

namespace {
    size_t parse_presision_decimals(std::string_view s)
    {
        if (s.empty()) throw std::invalid_argument("empty currency string");
        //auto s = str;
        if (s.front() == '-') s.remove_prefix(1);
        if (s.empty()) throw std::invalid_argument("empty currency string");
        if (auto point_pos = s.find('.'); point_pos != std::string_view::npos) {
            if (point_pos == s.length() - 1 || s.find('.', point_pos + 1) != std::string_view::npos) throw std::invalid_argument("invalid currency format: " + std::string(s));
            return s.length() - point_pos - 1;
        }
        return 0;
    }
}

namespace detail {
// Underlying integer of a currency's storage. The storage may be a plain integral (closed
// candles, entity fields) or a std::atomic integral (the single live "active" candle whose
// fields are mutated by the data thread and snapshot-read by the render thread).
template<class T> struct cur_underlying { using type = T; static constexpr bool is_atomic = false; };
template<class U> struct cur_underlying<std::atomic<U>> { using type = U; static constexpr bool is_atomic = true; };
}

template<class T>
requires (std::integral<T> || detail::cur_underlying<T>::is_atomic)
class currency
{
public:
    using value_type = typename detail::cur_underlying<T>::type;

private:
    static constexpr bool atomic_storage = detail::cur_underlying<T>::is_atomic;
    using dec_storage = std::conditional_t<atomic_storage, std::atomic<size_t>, size_t>;
    static constexpr value_type MAX_PARSE = std::numeric_limits<value_type>::max() / 10;

    dec_storage m_decimals;
    T m_value;

    // Single access seam for both storage flavours. The atomic loads/stores are what lets a
    // currency<atomic> field be mutated and snapshot-read without a data race; (value,decimals)
    // may still tear across the two members, which the active candle tolerates as a one-frame
    // visual artefact (see BuoyCandleQuotes).
    value_type val() const
    { if constexpr (atomic_storage) return m_value.load(std::memory_order_acquire); else return m_value; }
    void set_val(value_type v)
    { if constexpr (atomic_storage) m_value.store(v, std::memory_order_release); else m_value = v; }
    size_t dec() const
    { if constexpr (atomic_storage) return m_decimals.load(std::memory_order_acquire); else return m_decimals; }
    void set_dec(size_t d)
    { if constexpr (atomic_storage) m_decimals.store(d, std::memory_order_release); else m_decimals = d; }

public:
    // constexpr-constructible (std::atomic has a constexpr value ctor too) so entity types holding
    // currency fields stay usable in the compile-time metadata reflection (find_member_index).
    constexpr currency() : m_decimals(0), m_value(0) {}

    template <std::integral I>
    constexpr currency(I value, size_t decimals) : m_decimals(decimals), m_value(static_cast<value_type>(value)) {}

    explicit currency(std::string_view str) : currency(0, parse_presision_decimals(str))
    { parse(str); }

    explicit currency(std::string_view str, size_t decimals) : currency(0, decimals)
    { parse(str); }

    // Copy and cross-storage conversion go through the accessor seam: atomic members forbid a
    // defaulted copy, and load/store is exactly how the active (atomic) candle is seeded from
    // and snapshot into the plain closed-candle currencies.
    currency(const currency& c) : m_decimals(c.dec()), m_value(c.val()) {}
    template <class O> currency(const currency<O>& c) : m_decimals(c.decimals()), m_value(static_cast<value_type>(c.raw())) {}

    currency& operator=(const currency& c) { set_dec(c.dec()); set_val(c.val()); return *this; }
    template <class O> currency& operator=(const currency<O>& c) { set_dec(c.decimals()); set_val(static_cast<value_type>(c.raw())); return *this; }

    currency<value_type> operator-() const { return currency<value_type>(static_cast<value_type>(0) - val(), dec()); }

    currency& negate() { set_val(static_cast<value_type>(0) - val()); return *this; }

    template <std::integral I>
    void set_raw(I raw)
    { set_val(static_cast<value_type>(raw)); }

    // Comparison and arithmetic reconcile differing scales through raw_at (integer rescale),
    // never floating point, so two wire values parsed with their own decimal counts compare and
    // combine exactly. Results carry the natural fixed-point scale: +/- keep the wider scale,
    // * sums the operands' scales, / subtracts them (the volume-weighted mean's notional/volume
    // therefore lands back on the price scale).
    template <class O> bool operator==(const currency<O>& c) const
    { const size_t d = std::max(dec(), c.decimals()); return raw_at(d) == c.raw_at(d); }

    template <class O> bool operator!=(const currency<O>& c) const
    { return !operator==(c); }

    template <class O> bool operator<(const currency<O>& c) const
    { const size_t d = std::max(dec(), c.decimals()); return raw_at(d) < c.raw_at(d); }

    template <class O> bool operator>(const currency<O>& c) const
    { return c < *this; }

    template <class O> bool operator<=(const currency<O>& c) const
    { return !(c < *this); }

    template <class O> bool operator>=(const currency<O>& c) const
    { return !(*this < c); }

    template <class O> currency<value_type> operator+(const currency<O>& c) const
    { const size_t d = std::max(dec(), c.decimals()); return currency<value_type>(raw_at(d) + c.raw_at(d), d); }

    template <class O> currency<value_type> operator-(const currency<O>& c) const
    { const size_t d = std::max(dec(), c.decimals()); return currency<value_type>(raw_at(d) - c.raw_at(d), d); }

    template <class O> currency<value_type> operator*(const currency<O>& c) const
    { return currency<value_type>(val() * static_cast<value_type>(c.raw()), dec() + c.decimals()); }

    template <class O> currency<value_type> operator/(const currency<O>& c) const
    {
        const size_t dn = dec(), dd = c.decimals();
        if (dn >= dd) return currency<value_type>(val() / static_cast<value_type>(c.raw()), dn - dd);
        value_type factor = 1;
        for (size_t i = dn; i < dd; ++i) factor *= 10;
        return currency<value_type>((val() * factor) / static_cast<value_type>(c.raw()), 0);
    }

    value_type raw() const
    { return val(); }

    size_t decimals() const
    { return dec(); }

    value_type multiplier() const
    { value_type m = 1; for (size_t i = 0, n = dec(); i < n; ++i) m *= 10; return m; }

    // Raw value rescaled to a fixed number of fractional decimals (truncating toward
    // zero on narrowing). Lets a consumer normalise wire values that were each parsed
    // with their own scale onto one instrument-wide scale without re-parsing strings.
    value_type raw_at(size_t decimals) const
    {
        const size_t md = dec();
        const value_type v = val();
        if (decimals == md) return v;
        value_type factor = 1;
        if (decimals > md) {
            for (size_t i = md; i < decimals; ++i) factor *= 10;
            return static_cast<value_type>(v * factor);
        }
        for (size_t i = decimals; i < md; ++i) factor *= 10;
        return static_cast<value_type>(v / factor);
    }

    std::string to_string() const
    {
        const value_type v = val();
        const size_t md = dec();
        bool negative = v < 0;
        value_type abs_val = negative ? static_cast<value_type>(0) - v : v;
        std::string res = std::to_string(abs_val);
        if (md > 0) {
            while (res.length() < md + 1) res.insert(res.begin(), '0');
            res.insert(res.end() - md, '.');
        }
        if (negative) res.insert(res.begin(), '-');
        return res;
    }

    currency& parse(std::string_view str)
    {
        static const std::string delims = ".,' ";
        const size_t md = dec();
        bool is_decimal = false;
        bool negative = false;
        size_t decimals = 0;
        value_type value = 0;

        auto it = str.begin();
        if (it != str.end() && *it == '-') { negative = true; ++it; }

        for (; it != str.end(); ++it) {
            auto c = *it;
            if (delims.find(c) != std::string::npos) {
                if (is_decimal) throw std::invalid_argument(std::string(str));
                if (c == '.') is_decimal = true;
                continue;
            }
            if (decimals > md && c != '0') throw std::overflow_error("decimals length: " + std::string(str));
            if (value > MAX_PARSE) throw std::overflow_error(std::string(str));

            value *= 10;
            if (is_decimal) ++decimals;

            if (c >= '1' && c <= '9') {
                value_type step = c - '1' + 1;
                if (std::numeric_limits<value_type>::max() - step < value) throw std::overflow_error(std::string(str));
                value += step;
            }
            else if (c != '0') throw std::invalid_argument(std::string(str));
        }

        if (decimals < md)
            value *= std::pow(10, md - decimals);
        else if (decimals > md)
            value /= std::pow(10, decimals - md);

        set_val(negative ? static_cast<value_type>(0) - value : value);
        return *this;
    }

};

// Trait / concept identifying a currency<T> specialisation. Used by generic code
// (e.g. the datahub DAO) to treat fixed-point fields uniformly without naming the
// integer storage type.
template<class> inline constexpr bool is_currency_v = false;
template<std::integral T> inline constexpr bool is_currency_v<currency<T>> = true;

template<class T> concept currency_like = is_currency_v<std::remove_cvref_t<T>>;

}

namespace std {

template<std::integral T>
std::string to_string(const scratcher::currency<T>& c)
{ return c.to_string(); }

}

#include <glaze/glaze.hpp>

// Glaze codec for any currency<T>: read/write as a JSON quoted decimal string.
// Exchange wire formats carry every fractional value as a quoted string, so a
// currency-typed entity field deserialises directly from "0.01" and serialises
// back via to_string(). An empty string ("" — sent by ByBit for unset optional
// price fields such as triggerPrice) maps to a zero currency rather than throwing,
// so a single "" field never fails the whole record's parse.
template<std::integral T>
struct glz::meta<scratcher::currency<T>> {
    static constexpr auto value = custom<
        [](scratcher::currency<T>& c, const std::string& s) {
            c = s.empty() ? scratcher::currency<T>{} : scratcher::currency<T>(s);
        },
        [](const scratcher::currency<T>& c) -> std::string { return c.to_string(); }
    >;
};

#endif // SCRATCHER_DATA_CURRENCY_HPP
