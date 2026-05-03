// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "time_ruler.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "instrument_panel.hpp"
#include "timedef.hpp"

namespace scratcher::cockpit {

namespace {

constexpr float kLabelFontScale = 0.85f;     // tick labels are slightly smaller than body font
constexpr float kLineHeightScale = 1.4f;     // approx ascender + descender + leading per font unit
constexpr int kLabelPadding   = 2;           // px between tick base and label top, and around label
constexpr int kTickLength     = 3;
constexpr int kMajorTickLen   = 4;
constexpr int kMinLabelGapPx  = 14;          // minimum gap between adjacent label edges
constexpr int kAvgCharWidthN  = 6;           // numerator   — char_width ≈ font_size * 6/10
constexpr int kAvgCharWidthD  = 10;          // denominator
constexpr int kFallbackLabelW = 60;          // px when string-width estimate fails

enum class StepUnit : uint8_t { Millisecond, Second, Minute, Hour, Day, Week, Month, Year };

struct Step
{
    milliseconds typical;  // approximate spacing — used for selection only. For Month/Year the
                           // chrono::months / chrono::years definitions (Gregorian averages,
                           // 30.44d / 365.2425d) are more accurate than the previous 30d / 365d.
    StepUnit unit;
    int      count;        // multiplier within unit (1, 2, 5, 6, 12, …)
};

constexpr std::array kSteps = std::to_array<Step>({
    {milliseconds{100}, StepUnit::Millisecond, 100},
    {milliseconds{250}, StepUnit::Millisecond, 250},
    {milliseconds{500}, StepUnit::Millisecond, 500},
    {seconds{1},        StepUnit::Second,        1},
    {seconds{5},        StepUnit::Second,        5},
    {seconds{10},       StepUnit::Second,       10},
    {seconds{15},       StepUnit::Second,       15},
    {seconds{30},       StepUnit::Second,       30},
    {minutes{1},        StepUnit::Minute,        1},
    {minutes{5},        StepUnit::Minute,        5},
    {minutes{10},       StepUnit::Minute,       10},
    {minutes{15},       StepUnit::Minute,       15},
    {minutes{30},       StepUnit::Minute,       30},
    {hours{1},          StepUnit::Hour,          1},
    {hours{2},          StepUnit::Hour,          2},
    {hours{3},          StepUnit::Hour,          3},
    {hours{4},          StepUnit::Hour,          4},
    {hours{6},          StepUnit::Hour,          6},
    {hours{12},         StepUnit::Hour,         12},
    {days{1},           StepUnit::Day,           1},
    {days{2},           StepUnit::Day,           2},
    {weeks{1},          StepUnit::Week,          1},
    {months{1},         StepUnit::Month,         1},
    {months{2},         StepUnit::Month,         2},
    {months{3},         StepUnit::Month,         3},
    {months{6},         StepUnit::Month,         6},
    {years{1},          StepUnit::Year,          1},
    {years{2},          StepUnit::Year,          2},
    {years{5},          StepUnit::Year,          5},
    {years{10},         StepUnit::Year,         10},
    {years{25},         StepUnit::Year,         25},
    {years{100},        StepUnit::Year,        100},
});

const Step& select_step(milliseconds target)
{
    for (const auto& s : kSteps)
        if (s.typical >= target) return s;
    return kSteps.back();
}

struct CalView
{
    sys_time<milliseconds> tp;

    sys_days       day_start() const { return floor<days>(tp); }
    milliseconds   in_day()    const { return tp - day_start(); }
    year_month_day ymd()       const { return year_month_day{day_start()}; }

    auto year()  const { return ymd().year(); }
    auto month() const { return ymd().month(); }
    auto day()   const { return ymd().day(); }

    auto hour()   const { return floor<hours>(in_day()); }
    auto minute() const { return floor<minutes>(in_day()) - hour(); }
};

sys_time<milliseconds> ymd_to_tp(int year, unsigned month, unsigned day)
{
    return sys_days{year_month_day{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}}};
}

sys_time<milliseconds> align_first_tick(sys_time<milliseconds> from, const Step& step)
{
    auto ceil_div = [](int64_t a, int64_t b) -> int64_t {
        return (a >= 0) ? (a + b - 1) / b : -((-a) / b);
    };

    switch (step.unit) {
    case StepUnit::Millisecond:
    case StepUnit::Second:
    case StepUnit::Minute:
    case StepUnit::Hour: {
        const CalView p{from};
        const auto step_ms = step.typical;
        const milliseconds off{ceil_div(p.in_day().count(), step_ms.count()) * step_ms.count()};
        return p.day_start() + off;
    }
    case StepUnit::Day: {
        const auto day = floor<days>(from);
        const int64_t day_idx = day.time_since_epoch().count();
        const int     n        = step.count;
        const int64_t aligned  = ceil_div(day_idx, n) * n;
        sys_time<milliseconds> aligned_tp = sys_days{days{aligned}};
        if (aligned_tp < from) aligned_tp += days{n};
        return aligned_tp;
    }
    case StepUnit::Week: {
        const auto day    = floor<days>(from);
        const weekday wd{day};
        const auto monday = day - (wd - Monday);
        sys_time<milliseconds> result = monday;
        if (result < from) result += weeks{1};
        return result;
    }
    case StepUnit::Month: {
        const CalView p{from};
        const int month_idx0       = static_cast<int>(unsigned(p.month())) - 1;
        const int aligned_idx0     = static_cast<int>(ceil_div(month_idx0, step.count)) * step.count;
        int target_year  = static_cast<int>(p.year()) + aligned_idx0 / 12;
        int target_month = (aligned_idx0 % 12) + 1;
        auto result = ymd_to_tp(target_year, static_cast<unsigned>(target_month), 1);
        if (result < from) {
            target_month += step.count;
            while (target_month > 12) { target_month -= 12; ++target_year; }
            result = ymd_to_tp(target_year, static_cast<unsigned>(target_month), 1);
        }
        return result;
    }
    case StepUnit::Year: {
        const CalView p{from};
        const int n  = step.count;
        const int aligned_year = static_cast<int>(ceil_div(int64_t{static_cast<int>(p.year())}, int64_t{n})) * n;
        auto result = ymd_to_tp(aligned_year, 1, 1);
        if (result < from) result = ymd_to_tp(aligned_year + n, 1, 1);
        return result;
    }
    }
    return from;
}

sys_time<milliseconds> advance_tick(sys_time<milliseconds> tp, const Step& step)
{
    switch (step.unit) {
    case StepUnit::Millisecond:
    case StepUnit::Second:
    case StepUnit::Minute:
    case StepUnit::Hour:
    case StepUnit::Day:
    case StepUnit::Week:
        return tp + step.typical;
    case StepUnit::Month: {
        const CalView p{tp};
        int new_month = static_cast<int>(unsigned(p.month())) + step.count;
        int new_year  = static_cast<int>(p.year());
        while (new_month > 12) { new_month -= 12; ++new_year; }
        return ymd_to_tp(new_year, static_cast<unsigned>(new_month), 1);
    }
    case StepUnit::Year: {
        const CalView p{tp};
        return ymd_to_tp(static_cast<int>(p.year()) + step.count, 1, 1);
    }
    }
    return tp + step.typical;
}

// Uniform per-step regular-tick format. NO rolling-context: the day/month/year context
// is shown separately via the boundary labels above the axis line, so each tick can
// stick to its own granularity (e.g. all hour-step ticks render as HH:MM regardless of
// where day boundaries fall).
std::string format_regular(const CalView& cur, const Step& step)
{
    const auto tp = cur.tp;
    switch (step.unit) {
    case StepUnit::Year:        return std::format("{}", int(cur.year()));
    case StepUnit::Month:       return std::format("{:%b}", cur.month());
    case StepUnit::Week:
    case StepUnit::Day:         return std::format("{}", unsigned(cur.day()));
    case StepUnit::Hour:
    case StepUnit::Minute:      return std::format("{:%R}", tp);
    case StepUnit::Second:      return std::format("{:%T}", floor<seconds>(tp));
    case StepUnit::Millisecond: return std::format("{:%M:%S}", tp);
    }
    return {};
}

// Boundary labels appear ABOVE the axis line at calendar transitions. Priority order is
// Year > Month > Day; only the highest-priority crossing emits a label per tick to avoid
// stacking three labels at e.g. New Year midnight.
enum class BoundaryKind : uint8_t { None, Day, Month, Year };

BoundaryKind detect_boundary(const CalView& cur, const std::optional<CalView>& prev, const Step& step)
{
    // Year step: every tick IS a year, so above-line label would just duplicate the
    // regular label. None.
    if (step.unit == StepUnit::Year) return BoundaryKind::None;

    // Month step: regular label is month name, so above-line shows Year crossings only.
    if (step.unit == StepUnit::Month) {
        if (!prev) return BoundaryKind::Year;
        if (prev->year() != cur.year()) return BoundaryKind::Year;
        return BoundaryKind::None;
    }

    // Day/Week step: regular label is day-of-month, so above-line shows Month and Year
    // crossings. Year wins on a New Year tick (Jan 1 is both year + month transition).
    if (step.unit == StepUnit::Day || step.unit == StepUnit::Week) {
        if (!prev) return BoundaryKind::Year;
        if (prev->year() != cur.year())   return BoundaryKind::Year;
        if (prev->month() != cur.month()) return BoundaryKind::Month;
        return BoundaryKind::None;
    }

    // Sub-day step: check year > month > day transitions (or first tick).
    if (!prev) return BoundaryKind::Year;
    if (prev->year() != cur.year())   return BoundaryKind::Year;
    if (prev->month() != cur.month()) return BoundaryKind::Month;
    if (prev->day_start() != cur.day_start()) return BoundaryKind::Day;
    return BoundaryKind::None;
}

// Day-of-month rendered via unsigned(...) to drop chrono's %d zero-padding ("3" vs "03"),
// matching the year + abbreviated-month convention used elsewhere in the project.
std::string format_boundary(const CalView& cur, BoundaryKind kind)
{
    switch (kind) {
    case BoundaryKind::Year:  return std::format("{}", int(cur.year()));
    case BoundaryKind::Month: return std::format("{:%b} {}", cur.month(), int(cur.year()));
    case BoundaryKind::Day:   return std::format("{:%b} {}", cur.month(), unsigned(cur.day()));
    case BoundaryKind::None:  return {};
    }
    return {};
}

// Step-aware leftmost-timestamp format. Carries every granularity component COARSER than
// the regular tick step — i.e., the unchanging context that the regular labels do not.
// Year-step views return empty (the regular labels already carry the only relevant scale).
// Day rendered via unsigned(d) to drop %d zero-padding.
std::string format_leftmost(sys_time<milliseconds> tp, StepUnit unit)
{
    const CalView v{tp};
    const auto m = v.month();
    const unsigned d = static_cast<unsigned>(v.day());
    const int y = static_cast<int>(v.year());
    switch (unit) {
    case StepUnit::Year:        return {};
    case StepUnit::Month:       return std::format("{}", y);
    case StepUnit::Week:
    case StepUnit::Day:         return std::format("{:%b} {}", m, y);
    case StepUnit::Hour:        return std::format("{:%b} {} {}", m, d, y);
    case StepUnit::Minute:      return std::format("{:%b} {} {} {:%H}h", m, d, y, floor<hours>(tp));
    case StepUnit::Second:      return std::format("{:%b} {} {} {:%R}", m, d, y, floor<minutes>(tp));
    case StepUnit::Millisecond: return std::format("{:%b} {} {} {:%T}", m, d, y, floor<seconds>(tp));
    }
    return {};
}

int approx_label_px(const std::string& s, float font_size)
{
    if (s.empty()) return kFallbackLabelW;
    const int char_w = std::max(1, static_cast<int>(font_size) * kAvgCharWidthN / kAvgCharWidthD);
    return static_cast<int>(s.size()) * char_w;
}

// Helper: emit a left-anchored Text into a scene at HUD-pixel coordinates. Counter-flips
// Y so glyphs render upright under the HUD scene's Y-flip-about-canvas_h transform.
void emit_label(tvg::Scene& scene, const char* font, float font_size, const std::string& text,
                float x, float y_hud, uint8_t r, uint8_t g, uint8_t b)
{
    tvg_ptr<tvg::Text> lbl{tvg::Text::gen()};
    lbl->font(font);
    lbl->size(font_size);
    lbl->text(text.c_str());
    lbl->fill(r, g, b);
    lbl->align(0.0f, 0.0f);  // left-anchored top-left
    lbl->transform(tvg::Matrix{1.0f, 0.0f, x,
                               0.0f, -1.0f, y_hud,
                               0.0f, 0.0f, 1.0f});
    scene.add(lbl.get());
}

} // namespace

void TimeRuler::OnAttach(InstrumentContentPanel& panel)
{
    mScene.reset(tvg::Scene::gen());
    mAxisShape.reset(tvg::Shape::gen());
    mTickLinesScene.reset(tvg::Scene::gen());
    mTickLinesShape.reset(tvg::Shape::gen());
    mLabelScene.reset(tvg::Scene::gen());
    mLeftmostTimestamp.reset(tvg::Text::gen());

    mTickLinesScene->add(mTickLinesShape.get());
    mScene->add(mAxisShape.get());
    mScene->add(mTickLinesScene.get());
    mScene->add(mLabelScene.get());
    mScene->add(mLeftmostTimestamp.get());

    panel.HudScene().add(mScene.get());

    // View subscription: any change to the panel's view (pan, zoom, floor reseat) triggers
    // a full rebuild. This is Phase-3-minimum: persistent paints structure is wired but
    // the in-place pan-translate optimisation is deferred — the rebuild keeps the contract
    // identical to OnLayout, which is enough for visual correctness.
    mViewSubscriptionId = panel.SubscribeView([this, &panel]() {
        RebuildAll(panel);
    });
}

void TimeRuler::OnDetach(InstrumentContentPanel& panel)
{
    if (mViewSubscriptionId != 0) {
        panel.Unsubscribe(mViewSubscriptionId);
        mViewSubscriptionId = 0;
    }

    mLeftmostTimestamp.reset();
    mLabelScene.reset();
    mTickLinesShape.reset();
    mTickLinesScene.reset();
    mAxisShape.reset();
    mScene.reset();
}

void TimeRuler::CalculateSize(InstrumentContentPanel& panel)
{
    const float font_size  = panel.DefaultFontSize() * kLabelFontScale;
    const int   text_box_h = static_cast<int>(std::ceil(font_size * kLineHeightScale));
    // Reserve only the regular below-axis lane; the boundary lane (and the leftmost
    // timestamp) overlay the chart area above the axis with transparent backgrounds.
    // Keeping the strip thin lets the price-ruler vertical line meet the time-ruler axis
    // line at the inner-rect bottom-right corner without a visual gap.
    mReservedHeight = text_box_h + kLabelPadding * 2 + kMajorTickLen;

    PixelRect& rect = panel.MutableInnerDataRect();
    rect.bottom = std::max(rect.top, rect.bottom - mReservedHeight);
}

void TimeRuler::OnLayout(InstrumentContentPanel& panel)
{
    RebuildAll(panel);
}

void TimeRuler::RebuildAll(InstrumentContentPanel& panel)
{
    if (!mScene) return;

    const PixelRect& rect = panel.InnerDataRect();
    if (rect.width() <= 0) return;

    const float canvas_h = static_cast<float>(panel.OuterCanvasRect().height());
    const float font_size = panel.DefaultFontSize() * kLabelFontScale;
    const int   text_box_h = static_cast<int>(std::ceil(font_size * kLineHeightScale));

    // Axis sits at the strip's top edge (= rect.bottom in canvas-Y-down) so the price
    // ruler's vertical line meets it cleanly at the inner-rect corner. The 0.5 nudge
    // centres the half-pixel-wide stroke on a pixel boundary.
    const float axis_y_hud = canvas_h - static_cast<float>(rect.bottom) - 0.5f;

    // mAxisShape — single horizontal line, full inner-rect width. Rebuild path replaces
    // the stroke geometry; tvg::Shape::reset clears all sub-paths so the next moveTo/lineTo
    // pair defines the entire line.
    mAxisShape->reset();
    mAxisShape->moveTo(static_cast<float>(rect.left), axis_y_hud);
    mAxisShape->lineTo(static_cast<float>(rect.right), axis_y_hud);
    mAxisShape->strokeFill(180, 180, 180, 255);
    mAxisShape->strokeWidth(0.5f);

    const auto period = duration_cast<milliseconds>(panel.CandlePeriod());
    const uint32_t cwidth_px = panel.CandleWidth();
    if (cwidth_px == 0 || period.count() <= 0) return;

    const double ms_per_px = static_cast<double>(period.count()) / static_cast<double>(cwidth_px);

    const int64_t view_left_ms  = panel.ViewLeftTimeMs();
    const int64_t view_span_ms  = static_cast<int64_t>(rect.width() * ms_per_px);
    // Q1 — emit visible window ± 0.5 viewport buffer. Pan-extend within the buffer is
    // covered by future commits; for now, the buffer just widens the iterated tick range.
    const int64_t buffer_ms     = view_span_ms / 2;
    const sys_time<milliseconds> start_tp{milliseconds{view_left_ms - buffer_ms}};
    const sys_time<milliseconds> end_tp  {milliseconds{view_left_ms + view_span_ms + buffer_ms}};

    constexpr int label_slot_px = kFallbackLabelW + kMinLabelGapPx;
    const milliseconds target_step{std::max<int64_t>(1, static_cast<int64_t>(std::ceil(ms_per_px * label_slot_px)))};
    const Step& step = select_step(target_step);

    // Lane Ys in HUD-Y-up (where bigger = higher on screen). Below-axis lane lives in the
    // reserved strip; above-axis lane (boundary labels + leftmost timestamp) overlays the
    // chart area with transparent text. The above-axis offset uses text_box_h so the
    // glyph bbox bottom (descender included) clears the axis stroke by kLabelPadding —
    // using bare font_size leaves descenders pierced by the axis line for fonts whose
    // typo-descender extends below the EM box.
    //
    // Below-axis offset is `kTickLength + 1`: label bbox-top sits 1 px below the tick
    // stub. Most TTF fonts pad the bbox top above the visible ascender by 2-3 px, so the
    // tick stub still appears in clear space above the glyph; this packing matches the
    // 5-ish-pixel visual gap on the boundary lane, where bbox-bottom sits kLabelPadding
    // above the axis and the descender pad eats most of the difference.
    const float below_label_top_y_hud = axis_y_hud - static_cast<float>(kTickLength + 1);
    const float above_label_top_y_hud = axis_y_hud + static_cast<float>(kLabelPadding + text_box_h);

    // Leftmost full-context timestamp lives in the SAME LANE as boundary labels (upper
    // half of the strip), pinned at the chart's left edge. It carries every granularity
    // component coarser than the regular step — i.e., the anchor that does NOT change as
    // ticks scroll past. Empty for year-step views (no coarser context exists).
    const sys_time<milliseconds> view_left_tp{milliseconds{view_left_ms}};
    const std::string leftmost_text = format_leftmost(view_left_tp, step.unit);
    mLeftmostTimestamp->font(panel.DefaultFontName());
    mLeftmostTimestamp->size(font_size);
    mLeftmostTimestamp->text(leftmost_text.c_str());
    mLeftmostTimestamp->fill(220, 220, 220);
    mLeftmostTimestamp->align(0.0f, 0.0f);
    const float leftmost_x = static_cast<float>(rect.left + kLabelPadding);
    mLeftmostTimestamp->transform(tvg::Matrix{1.0f, 0.0f, leftmost_x,
                                              0.0f, -1.0f, above_label_top_y_hud,
                                              0.0f, 0.0f, 1.0f});

    // Pre-compute leftmost timestamp's right edge for boundary collision tests. Empty
    // text => the leftmost label isn't rendered, so boundaries can start from rect.left.
    const int leftmost_right_px = leftmost_text.empty()
        ? rect.left
        : rect.left + kLabelPadding + approx_label_px(leftmost_text, font_size);

    // Wipe the per-rebuild content. mAxisShape stays (mutated above); the tick lines
    // shape and label scene are re-emitted from scratch.
    mTickLinesShape->reset();
    mLabelScene->remove();

    sys_time<milliseconds> t = align_first_tick(start_tp, step);
    std::optional<CalView> prev;
    int last_regular_right_px  = INT_MIN;
    int last_boundary_right_px = leftmost_right_px;  // boundary collision must also avoid the leftmost timestamp

    while (t <= end_tp) {
        // Use the panel helper rather than reproducing scale + view-offset locally — the
        // matrix it reads from mLogicalScene is the single source of truth for the X-axis
        // mapping (e11 = px_per_ms, e13 = inner_left - e11 * (view_left - floor)).
        const float tick_x = panel.HudXOfTime(static_cast<int64_t>(t.time_since_epoch().count()));
        const int   tick_x_px = static_cast<int>(tick_x);

        // Skip ticks whose anchor falls outside the inner rect's horizontal extent. The
        // ± 0.5-viewport buffer over which we iterate intentionally over-emits ticks; the
        // HUD scene's clip is the canvas, not the inner rect, so without this skip the
        // axis would carry tick stubs into the price-ruler strip on the right and into
        // negative-x sliver on the left.
        if (tick_x_px < rect.left || tick_x_px > rect.right) {
            prev = CalView{t};
            t = advance_tick(t, step);
            continue;
        }

        // Tick line as a sub-path on the persistent shape: vertical stroke from axis_y
        // downward (toward canvas bottom = smaller HUD-y). Sub-paths share the shape's
        // single stroke style, so the stroke is set once below outside the loop.
        mTickLinesShape->moveTo(tick_x, axis_y_hud);
        mTickLinesShape->lineTo(tick_x, axis_y_hud - kTickLength);

        const CalView cur{t};

        // Regular below-line label, ALL left-anchored. Suppress the label if it would
        // extend past rect.right (into the price-ruler strip): unlike boundary labels,
        // which already filter on rect.right, regular labels were drifting into the
        // price strip whenever the rightmost tick sat near the inner-rect edge.
        const std::string regular_text = format_regular(cur, step);
        const int regular_w = approx_label_px(regular_text, font_size);
        const int regular_right_px = tick_x_px + regular_w;
        const bool regular_fits_horizontally = tick_x_px > last_regular_right_px + kMinLabelGapPx;
        const bool regular_fits_in_inner_rect = regular_right_px <= rect.right;
        if (regular_fits_horizontally && regular_fits_in_inner_rect) {
            emit_label(*mLabelScene, panel.DefaultFontName(), font_size, regular_text,
                       static_cast<float>(tick_x_px), below_label_top_y_hud, 210, 210, 210);
            last_regular_right_px = regular_right_px;
        }

        // Boundary above-line label, ALL left-anchored, priority Year > Month > Day.
        // Lives in the chart area (above the axis, transparent over chart content) — its
        // right edge must still respect rect.right so labels don't overflow into the
        // price-ruler strip.
        const BoundaryKind kind = detect_boundary(cur, prev, step);
        if (kind != BoundaryKind::None) {
            const std::string boundary_text = format_boundary(cur, kind);
            const int boundary_w = approx_label_px(boundary_text, font_size);
            const int boundary_right_px = tick_x_px + boundary_w;
            const bool fits_horizontally = tick_x_px > last_boundary_right_px + kMinLabelGapPx;
            const bool fits_in_inner_rect = boundary_right_px <= rect.right;
            if (fits_horizontally && fits_in_inner_rect) {
                emit_label(*mLabelScene, panel.DefaultFontName(), font_size, boundary_text,
                           static_cast<float>(tick_x_px), above_label_top_y_hud, 230, 230, 180);
                last_boundary_right_px = boundary_right_px;
            }
        }

        prev = cur;
        t = advance_tick(t, step);
    }

    mTickLinesShape->strokeFill(180, 180, 180, 255);
    mTickLinesShape->strokeWidth(0.5f);
}

}
