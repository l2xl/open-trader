// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

namespace scratcher::cockpit {

class InstrumentPanel;

// Scratchers are observer-driven actors that own a portion of the panel's scene tree.
// Lifecycle:
//   * OnAttach      — create sub-scene(s), attach to panel.HudScene() / panel.LogicalScene().
//   * CalculateSize — pre-layout phase; rulers shrink panel.MutableInnerDataRect() to
//                     reserve their axis strips. Most scratchers leave this as a no-op.
//   * OnLayout      — post-layout phase; called after the inner-rect is finalised and
//                     ApplyLogicalSceneTransform has run. Scratchers update geometry to match
//                     the new layout. Mutations after OnLayout should be announced via
//                     panel.MarkDirty for the damage-tracked render loop.
//   * OnDetach      — remove sub-scenes, release resources.
struct Scratcher
{
    virtual ~Scratcher() = default;

    virtual void OnAttach(InstrumentPanel& panel) = 0;
    virtual void CalculateSize(InstrumentPanel& /*panel*/) {}
    virtual void OnLayout(InstrumentPanel& /*panel*/) {}
    virtual void OnDetach(InstrumentPanel& /*panel*/) {}
};

}
