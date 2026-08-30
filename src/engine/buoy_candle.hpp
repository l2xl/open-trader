// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BOUY_CANDLE_QUOTES_HPP
#define BOUY_CANDLE_QUOTES_HPP

#include <ranges>
#include <cmath>
#include <concepts>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <vector>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/debug_adaptor.hpp>
#include <tbb/concurrent_vector.h>

#include "currency.hpp"
#include "timedef.hpp"

namespace scratcher {

// Exact wide-integer backend of the buoy math: arbitrary precision, so no accumulation can
// overflow by construction. Debug builds wrap the same backend in debug_adaptor, which keeps
// a decimal string of every value visible in the debugger.
#ifdef NDEBUG
using big_int = boost::multiprecision::cpp_int;
#else
using big_int = boost::multiprecision::number<boost::multiprecision::debug_adaptor<boost::multiprecision::cpp_int_backend<>>>;
#endif

// sqrt over the debug_adaptor variant is ambiguous in Boost 1.83 (eval_qr), so the root always
// runs on the plain backend; exact integer sqrt either way.
inline big_int big_sqrt(const big_int& value)
{ return big_int(boost::multiprecision::sqrt(boost::multiprecision::cpp_int(value))); }

enum class BuoyRegime { NORMAL, HALF_NORMAL };

template <typename P, typename V>
struct BuoyCandleData
{
    P min;
    P max;
    P mean;
    P close;  // last trade price of the period; for an empty buoy the carried previous close
    V volume;
    // Per-side volume-weighted deviations around the waist, fixed-point on the price scale,
    // floored at one raw unit per side so downstream divisions stay finite.
    P sigma_plus;
    P sigma_minus;

    // BUOY_CANDLE.md tunables: bell treated as zero at TAIL sigmas (range fit), a waist within
    // SKEW_NUM/SKEW_DEN of an extreme renders as half a bell.
    static constexpr uint64_t TAIL = 3;
    static constexpr uint64_t SKEW_NUM = 3;
    static constexpr uint64_t SKEW_DEN = 20;

    struct ContourPoint { float width = 0.f; P price; };
    struct Contour { std::vector<ContourPoint> points; bool closed = false; };
    struct SigmaTicks { P upper; P lower; float width = 0.f; };

    BuoyCandleData() = default;

    BuoyCandleData(auto min, auto max, auto mean, auto close, auto vol)
        : min(min), max(max), mean(mean), close(close), volume(vol)
    {
        if (!(this->min < this->max)) throw std::invalid_argument("Degenerate buoy candle: min price is not below max price");
    }

    template <typename P1, typename V1> requires std::constructible_from<P, const P1&> && std::constructible_from<V, const V1&>
    BuoyCandleData(const BuoyCandleData<P1, V1>& other)
        : min(other.min), max(other.max), mean(other.mean), close(other.close), volume(other.volume), sigma_plus(other.sigma_plus), sigma_minus(other.sigma_minus)
    {}

    BuoyCandleData& operator=(const BuoyCandleData& other) = default;

    template <typename P1, typename V1> requires std::assignable_from<P&, const P1&> && std::assignable_from<V&, const V1&>
    BuoyCandleData& operator=(const BuoyCandleData<P1, V1>& other)
    {
        min = other.min;
        max = other.max;
        mean = other.mean;
        close = other.close;
        volume = other.volume;
        sigma_plus = other.sigma_plus;
        sigma_minus = other.sigma_minus;
        return *this;
    }

    BuoyRegime Classify() const
    {
        const auto span = max - min;
        const auto skew_num = decltype(span)(SKEW_NUM, 0);
        const auto skew_den = decltype(span)(SKEW_DEN, 0);
        if ((max - mean) * skew_den < span * skew_num || (mean - min) * skew_den < span * skew_num) return BuoyRegime::HALF_NORMAL;
        return BuoyRegime::NORMAL;
    }

    // Range fit per BUOY_CANDLE.md section 4: one-sided clamp of each deviation to its tip
    // distance over TAIL, applied before calibration and every other consumer. The truncating
    // fixed-point division can zero the cap on a 1-2 tick period, so the cap itself is floored
    // at one raw unit — the fit must never destroy the positive sigma floor (sections 3 and 8).
    BuoyCandleData FitRange() const
    {
        BuoyCandleData fitted(*this);
        const auto tail = decltype(max - mean)(TAIL, 0);
        auto upper_cap = (max - mean) / tail;
        auto lower_cap = (mean - min) / tail;
        if (upper_cap.raw() == 0) upper_cap = decltype(upper_cap)(1, upper_cap.decimals());
        if (lower_cap.raw() == 0) lower_cap = decltype(lower_cap)(1, lower_cap.decimals());
        if (upper_cap < fitted.sigma_plus) fitted.sigma_plus = upper_cap;
        if (lower_cap < fitted.sigma_minus) fitted.sigma_minus = lower_cap;
        return fitted;
    }

    // The exponential is the single render-facing (float) step; the distance/deviation ratio
    // underneath is taken from the exact fixed-point raws. The engine floors its own sigmas at
    // one raw unit, but a field-assembled candle (default-constructed, DAO-loaded) can carry
    // zero — collapse that to the sigma→0 limit (unit width at the waist, zero elsewhere)
    // instead of NaN/inf.
    float WallWidth(const P& price) const
    {
        const bool upper = mean < price;
        const auto distance = upper ? price - mean : mean - price;
        const auto& sigma = upper ? sigma_plus : sigma_minus;
        const size_t d = std::max(distance.decimals(), sigma.decimals());
        if (sigma.raw_at(d) == 0) return distance.raw_at(d) == 0 ? 1.f : 0.f;
        const float x = static_cast<float>(distance.raw_at(d)) / static_cast<float>(sigma.raw_at(d));
        return std::exp(-0.5f * x * x);
    }

    SigmaTicks TickLevels() const
    { return {mean + sigma_plus, mean - sigma_minus, std::exp(-0.5f)}; }

    // The mirrored halves share the tip points rather than duplicating them; half-normal buoys
    // span waist to the far tip only and stay open so the waist corners render sharp.
    Contour SampleContour(size_t levels) const
    {
        const BuoyRegime regime = Classify();
        Contour contour{{}, regime == BuoyRegime::NORMAL};
        if (levels < 2) return contour;

        if (regime == BuoyRegime::NORMAL) {
            const size_t sd = std::max({min.decimals(), max.decimals(), mean.decimals()});
            const auto high = max.raw_at(sd), low = min.raw_at(sd), waist = mean.raw_at(sd);
            std::vector<uint64_t> level_raws;
            level_raws.reserve(levels + 1);
            for (size_t k = 0; k < levels; ++k) {
                const big_int drop = big_int(high - low) * k / (levels - 1);
                level_raws.push_back(high - drop.convert_to<uint64_t>());
            }
            // The waist must itself be a sample: a uniform grid generally misses the mean,
            // and with asymmetric sigmas the spline would then interpolate the peak from two
            // off-peak neighbours, bulging the waist off the mean price.
            const auto pos = std::lower_bound(level_raws.begin(), level_raws.end(), waist, std::greater<>{});
            if (pos == level_raws.end() || *pos != waist) level_raws.insert(pos, waist);
            const size_t count = level_raws.size();
            contour.points.reserve(2 * count - 2);
            for (size_t k = 0; k < count; ++k) {
                const P level(level_raws[k], sd);
                const float width = (k == 0 || k + 1 == count) ? 0.f : WallWidth(level);
                contour.points.push_back({width, level});
            }
            for (size_t k = count - 1; k-- > 1;)
                contour.points.push_back({-contour.points[k].width, contour.points[k].price});
            return contour;
        }

        const auto span = max - min;
        const bool waist_high = (max - mean) * decltype(span)(SKEW_DEN, 0) < span * decltype(span)(SKEW_NUM, 0);
        const P& far_tip = waist_high ? min : max;
        const size_t sd = std::max(mean.decimals(), far_tip.decimals());
        const auto waist = mean.raw_at(sd), tip = far_tip.raw_at(sd);
        contour.points.reserve(2 * levels - 1);
        for (size_t k = 0; k < levels; ++k) {
            const big_int scaled = big_int(waist_high ? waist - tip : tip - waist) * k / (levels - 1);
            const uint64_t offset = scaled.convert_to<uint64_t>();
            const P level(waist_high ? waist - offset : waist + offset, sd);
            const float width = k == 0 ? 1.f : (k + 1 == levels ? 0.f : WallWidth(level));
            contour.points.push_back({width, level});
        }
        for (size_t k = levels - 1; k-- > 0;)
            contour.points.push_back({-contour.points[k].width, contour.points[k].price});
        return contour;
    }
};


class BuoyCandleQuotes {
public:
    // Prices/volumes are carried as currency<uint64_t> straight from the wire PublicTrade, so the
    // candle keeps the exchange's fixed-point values verbatim; currency reconciles per-trade scale
    // differences. No instrument decimals live here — conversion to scene "points" happens only at
    // the ThorVG boundary (QuoteScratcher, via Currency::raw_at + InstrumentInfo).
    typedef currency<uint64_t> price_t;
    typedef BuoyCandleData<price_t, price_t> candle_t;
    typedef tbb::concurrent_vector<candle_t> quotes_t;

    struct Calibration { price_t median_volume; price_t median_sigma_sum; };
private:
    // The active candle is mutated by the data thread and snapshot-read by the render thread, so
    // its fields use currency-over-atomic storage.
    typedef currency<std::atomic_uint64_t> atomic_price_t;

    // Per-side raw moments of the active period, accumulated around the period's first trade
    // price (magnitude shift). big_int cannot overflow, so the shift is not a range guard — it
    // keeps the deviations (and thus every accumulator's limb count) small enough for the
    // per-trade updates to stay in cpp_int's inline storage. Data-thread-only state — readers
    // only see the derived sigmas stored into the atomic candle fields.
    struct SideMoments {
        big_int volume;
        big_int weighted_dev;
        big_int weighted_dev2;
    };

    const uint64_t m_buoy_duration;

    std::optional<std::atomic_uint64_t> m_first_buoy_timestamp;
    std::optional<std::atomic_uint64_t> m_first_trade_timestamp;
    std::optional<std::atomic_uint64_t> m_last_trade_timestamp;
    quotes_t m_buoy_data;
    BuoyCandleData<atomic_price_t, atomic_price_t> mCurCandle;

    SideMoments m_upper_moments;
    SideMoments m_lower_moments;
    big_int m_origin_price_raw;
    size_t m_moment_price_decimals = 0;
    size_t m_moment_volume_decimals = 0;
    bool m_origin_set = false;

    static big_int pow10(size_t exponent)
    { return boost::multiprecision::pow(big_int(10), static_cast<unsigned>(exponent)); }

    void rescale_price_moments(size_t decimals)
    {
        const big_int factor = pow10(decimals - m_moment_price_decimals);
        m_origin_price_raw *= factor;
        m_upper_moments.weighted_dev *= factor;
        m_upper_moments.weighted_dev2 *= factor * factor;
        m_lower_moments.weighted_dev *= factor;
        m_lower_moments.weighted_dev2 *= factor * factor;
        m_moment_price_decimals = decimals;
    }

    void rescale_volume_moments(size_t decimals)
    {
        const big_int factor = pow10(decimals - m_moment_volume_decimals);
        m_upper_moments.volume *= factor;
        m_upper_moments.weighted_dev *= factor;
        m_upper_moments.weighted_dev2 *= factor;
        m_lower_moments.volume *= factor;
        m_lower_moments.weighted_dev *= factor;
        m_lower_moments.weighted_dev2 *= factor;
        m_moment_volume_decimals = decimals;
    }

    void accumulate_moments(bool upper, const price_t& price, const price_t& size)
    {
        if (!m_origin_set) {
            m_origin_set = true;
            m_moment_price_decimals = price.decimals();
            m_moment_volume_decimals = size.decimals();
            m_origin_price_raw = price.raw();
        }
        if (price.decimals() > m_moment_price_decimals) rescale_price_moments(price.decimals());
        if (size.decimals() > m_moment_volume_decimals) rescale_volume_moments(size.decimals());

        const big_int deviation = big_int(price.raw_at(m_moment_price_decimals)) - m_origin_price_raw;
        const big_int volume = size.raw_at(m_moment_volume_decimals);
        SideMoments& side = upper ? m_upper_moments : m_lower_moments;
        side.volume += volume;
        side.weighted_dev += volume * deviation;
        side.weighted_dev2 += volume * deviation * deviation;
    }

    // Exact recentring of the origin-shifted moments around the current mean:
    // sum v*(p-mu)^2 = sum v*d^2 - 2*shift*sum v*d + shift^2*V with shift = mu - origin.
    // Scales: dev2/volume lands on twice the price scale, so the integer sqrt returns the
    // deviation straight on the price scale. Floored at one raw price unit per side.
    price_t side_sigma(const SideMoments& side) const
    {
        const price_t floor(1, m_moment_price_decimals);
        if (side.volume == 0) return floor;
        const big_int shift = big_int(mCurCandle.mean.raw_at(m_moment_price_decimals)) - m_origin_price_raw;
        big_int recentred = side.weighted_dev2 - 2 * shift * side.weighted_dev + shift * shift * side.volume;
        if (recentred < 0) recentred = 0;
        const big_int sigma = big_sqrt(recentred / side.volume);
        const uint64_t sigma_raw = sigma.convert_to<uint64_t>();
        return sigma_raw > 0 ? price_t(sigma_raw, m_moment_price_decimals) : floor;
    }

    void store_sigmas()
    {
        mCurCandle.sigma_plus = side_sigma(m_upper_moments);
        mCurCandle.sigma_minus = side_sigma(m_lower_moments);
    }

    // In-place reset of the persistent active candle. The object is never reconstructed — a
    // concurrent active_candle() reader sees field-wise atomic stores. volume is stored last so
    // any intermediate state a reader observes is a consistent zero-extent (lone-trade) candle
    // rather than a torn extent (a torn currency value/decimals pair is a tolerated one-frame
    // visual artefact, consistent with the pre-existing torn-snapshot allowance).
    void reset_active(const price_t& price)
    {
        m_upper_moments = {};
        m_lower_moments = {};
        m_origin_price_raw = 0;
        m_moment_price_decimals = price.decimals();
        m_moment_volume_decimals = 0;
        m_origin_set = false;

        mCurCandle.min = price;
        mCurCandle.max = price;
        mCurCandle.mean = price;
        mCurCandle.close = price;
        mCurCandle.sigma_plus = price_t(1, price.decimals());
        mCurCandle.sigma_minus = price_t(1, price.decimals());
        mCurCandle.volume = price_t{};
    }

public:
    explicit BuoyCandleQuotes(uint64_t candle_time)
        : m_buoy_duration(candle_time)
    {}

    uint64_t buoy_duration() const
    { return m_buoy_duration; }

    std::optional<uint64_t> first_trade_timestamp() const
    { return m_first_trade_timestamp; }

    std::optional<uint64_t> last_trade_timestamp() const
    { return m_last_trade_timestamp; }

    std::optional<uint64_t> first_buoy_timestamp() const
    { return m_first_buoy_timestamp; }

    const quotes_t& quotes() const
    { return m_buoy_data; }

    candle_t active_candle() const
    { return mCurCandle; }

    // Window calibration medians (BUOY_CANDLE.md section 4) over a range of closed candles:
    // one pass collects the eligible periods — empty and single-price degenerate ones are
    // excluded — then nth_element picks both medians. The range fit is applied here to every
    // eligible candle (idempotent), so the medians always consume clamped deviations whatever
    // the caller passed; WidthRatio and the contour still expect the caller's fitted candle.
    template <std::ranges::input_range Range>
    requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, candle_t>
    static Calibration Calibrate(const Range& candles)
    {
        std::vector<price_t> volumes, sigma_sums;
        for (const candle_t& candle : candles) {
            if (candle.volume.raw() == 0 || !(candle.min < candle.max)) continue;
            const candle_t fitted = candle.FitRange();
            volumes.push_back(fitted.volume);
            sigma_sums.push_back(fitted.sigma_plus + fitted.sigma_minus);
        }
        if (volumes.empty()) return {};
        const auto median = [](std::vector<price_t>& values) {
            const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
            std::nth_element(values.begin(), middle, values.end());
            return *middle;
        };
        return {median(volumes), median(sigma_sums)};
    }

    // Dimensionless width ratio (BUOY_CANDLE.md section 4, "Scale invariance"):
    // (V / median V) * (median sigma-sum / sigma-sum), exact integer cross-products compared
    // in float only at the return. The renderer scales the target waist half-width by it; the
    // median candle's ratio is exactly one.
    static float WidthRatio(const candle_t& candle, const Calibration& calibration)
    {
        const price_t sigma_sum = candle.sigma_plus + candle.sigma_minus;
        const size_t vd = std::max(candle.volume.decimals(), calibration.median_volume.decimals());
        const size_t sd = std::max(sigma_sum.decimals(), calibration.median_sigma_sum.decimals());
        const big_int numerator = big_int(candle.volume.raw_at(vd)) * calibration.median_sigma_sum.raw_at(sd);
        const big_int denominator = big_int(calibration.median_volume.raw_at(vd)) * sigma_sum.raw_at(sd);
        return denominator != 0 ? numerator.convert_to<float>() / denominator.convert_to<float>() : 0.f;
    }

    // Rewind the series so the next AppendTrades rebuilds from scratch. Clears the
    // trade bookmarks too (not just the first-buoy anchor) so a snapshot rebuild does
    // not dedup incoming trades against a stale last-seen timestamp.
    void Reset()
    {
        m_first_buoy_timestamp.reset();
        m_first_trade_timestamp.reset();
        m_last_trade_timestamp.reset();
    }

    template <std::ranges::input_range Range>
    requires requires(std::ranges::range_value_t<Range> trade) {
        trade.time;
        trade.price;
        trade.size;
    }
    price_t AppendTrades(const Range& trades, price_t last_price)
    {
        auto it = std::ranges::begin(trades);
        const auto trades_end = std::ranges::end(trades);
        if (it == trades_end)
            return last_price;

        uint64_t first_ts = get_timestamp((*it).time);

        if (!m_first_buoy_timestamp) { // indicates that Reset() was called
            reset_active(last_price);
            m_buoy_data.clear();
            m_first_trade_timestamp.emplace(first_ts);
            m_first_buoy_timestamp.emplace(first_ts - first_ts % buoy_duration());
            m_last_trade_timestamp.reset();
        }

        if (m_buoy_data.size() > 0 && first_ts < *m_first_buoy_timestamp + (m_buoy_data.size() - 1) * m_buoy_duration)
            throw std::invalid_argument("Trade time earlier then first candle time");

        if (m_last_trade_timestamp && first_ts < *m_last_trade_timestamp)
            throw std::invalid_argument("Trade time earlier then last processed trade time");

        uint64_t next_buoy_ts = *m_first_buoy_timestamp + m_buoy_data.size() * buoy_duration();
        uint64_t trade_ts = 0; // Will not be empty since trades is not empty
        for(; it != trades_end; ++it) {
            // Bind the element once: the range is the feed's native PublicTrade subrange (its
            // iterator may yield a prvalue and provide no operator->), so a single dereference
            // both satisfies the access pattern and reads each wire trade exactly once.
            const auto& trade = *it;
            trade_ts = get_timestamp(trade.time);

            while (trade_ts >= next_buoy_ts) {
                if (mCurCandle.volume.raw() > 0 || !m_buoy_data.empty()) {
                    store_sigmas();
                    candle_t buoy = mCurCandle;
                    reset_active(last_price);
                    m_buoy_data.emplace_back(buoy);
                }
                next_buoy_ts += buoy_duration();
            }

            price_t last_volume = mCurCandle.volume;
            price_t sum_volume = last_volume + trade.size;

            // Side classification against the running mean at ingestion time — the
            // approximation sanctioned by BUOY_CANDLE.md section 3 for a stream that is
            // not retained. The period's first trade coincides with the mean it sets and
            // lands on the upper side by the p >= mu convention.
            const bool upper_side = last_volume.raw() == 0 || !(trade.price < mCurCandle.mean);

            if (last_volume.raw() == 0) {
                // First trade of the period: the buoy opens AT the trade price, not at
                // the carried-forward previous close. A lone-trade buoy is therefore a
                // zero-extent diamond (min == max == mean == price); the move from the
                // previous close is indicated separately by the scratcher, not by
                // widening this candle. The carried close still seeds empty buoys for
                // the gray dash (see the reset above) but is overwritten the instant a
                // trade lands.
                mCurCandle.max = trade.price;
                mCurCandle.min = trade.price;
                mCurCandle.mean = trade.price;
            } else {
                if (mCurCandle.max < trade.price) mCurCandle.max = trade.price;
                if (trade.price < mCurCandle.min) mCurCandle.min = trade.price;
                mCurCandle.mean = (mCurCandle.mean * last_volume + trade.price * trade.size) / sum_volume;
            }
            mCurCandle.close = trade.price;
            mCurCandle.volume = sum_volume;

            accumulate_moments(upper_side, trade.price, trade.size);

            last_price = trade.price;
        }
        // Sigmas materialise at candle-close and batch boundaries only — readers observe the
        // active candle at frame granularity, so the per-trade integer sqrt cost is skipped.
        store_sigmas();
        m_last_trade_timestamp.emplace(trade_ts);

        return last_price;
    }

    // Time-driven advance: fill-forward empty buoys and roll the active candle up to
    // `now_ts`, carrying `last_price` into each empty period. Carries NO trade data —
    // this is the only series mutation the time/scroll path performs, decoupled from
    // AppendTrades so a wall-clock tick advances the live edge without re-ingesting.
    // Cheap when now_ts has not crossed the next buoy boundary (loop body skipped).
    void AdvanceTo(uint64_t now_ts, price_t last_price);
};

}

#endif //BOUY_CANDLE_QUOTES_HPP
