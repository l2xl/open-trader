// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <cstdint>

#include "scratcher.hpp"
#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

// TimeRuler renders the bottom horizontal time axis with tick marks, regular tick labels
// (uniform format per step), boundary context labels (day/month/year crossings, priority
// ranked) and a single leftmost full-context timestamp pinned at the chart's top-left.
//
// Internal scene structure (all attached under the panel's mHudScene):
//   mScene
//   ├─ mAxisShape          — single horizontal axis line, full inner-rect width
//   ├─ mTickLinesScene     — child scene; transform mirrors mLogicalScene's X axis so
//   │   │                    sub-paths inside the contained Shape are positioned in
//   │   │                    floor-relative ms units. Pan = update transform.e13 only.
//   │   └─ mTickLinesShape — one Shape with N sub-paths (one per tick)
//   ├─ mLabelScene         — identity-transform; pool of Text paints, each repositioned
//   │                        via its own transform(...) (translate + counter-flip Y to
//   │                        keep glyphs upright under HUD's Y-flip). Holds both regular
//   │                        below-line labels and boundary above-line labels.
//   └─ mLeftmostTimestamp  — single mutable Text at chart-top-left; text content updates
//                            on view changes via Text::text(...).
class TimeRuler final : public Scratcher
{
public:
    TimeRuler() = default;

    void OnAttach(InstrumentContentPanel& panel) override;
    void CalculateSize(InstrumentContentPanel& panel) override;
    void OnLayout(InstrumentContentPanel& panel) override;
    void OnDetach(InstrumentContentPanel& panel) override;

private:
    void RebuildAll(InstrumentContentPanel& panel);

    int mReservedHeight = 0;

    tvg_ptr<tvg::Scene> mScene;
    tvg_ptr<tvg::Shape> mAxisShape;
    tvg_ptr<tvg::Scene> mTickLinesScene;
    tvg_ptr<tvg::Shape> mTickLinesShape;
    tvg_ptr<tvg::Scene> mLabelScene;
    tvg_ptr<tvg::Text>  mLeftmostTimestamp;

    // Subscription id from panel.SubscribeView so view-side mutations trigger a rebuild.
    // 0 sentinel = no active subscription (simpler than std::optional and consistent with
    // the panel's monotonically increasing 1-based id allocation).
    uint64_t mViewSubscriptionId = 0;
};

}
