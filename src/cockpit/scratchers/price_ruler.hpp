// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include "scratcher.hpp"
#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

// PriceRuler renders the right-hand vertical price axis: a full-height axis line, evenly spaced
// tick marks at "nice" 1-2-5 price steps, and a left-anchored price label per tick (formatted
// through the instrument's currency scale). The visible price band is recovered from the live
// LogicalScene transform via InstrumentPanel::PriceOfHudY, so tick prices track pan/zoom and the
// quote scratcher's autoscale without any shared state.
//
// Internal scene structure (all attached under the panel's mHudScene, pixel space):
//   mScene
//   ├─ mAxisShape       — single vertical axis line at the inner-rect right edge
//   ├─ mTickLinesShape  — one Shape with N sub-paths (one stub per tick)
//   └─ mLabelScene      — pool of Text paints, re-emitted each layout (translate + counter-flip
//                         Y so glyphs stay upright under HUD's Y-flip)
class PriceRuler final : public Scratcher
{
public:
    PriceRuler() = default;

    void OnAttach(InstrumentPanel& panel) override;
    void CalculateSize(InstrumentPanel& panel) override;
    void OnLayout(InstrumentPanel& panel) override;
    void OnDetach(InstrumentPanel& panel) override;

private:
    void RebuildAll(InstrumentPanel& panel);

    int mReservedWidth = 0;

    tvg_ptr<tvg::Scene> mScene;
    tvg_ptr<tvg::Shape> mAxisShape;
    tvg_ptr<tvg::Shape> mTickLinesShape;
    tvg_ptr<tvg::Scene> mLabelScene;
};

}
