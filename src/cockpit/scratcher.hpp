// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

namespace scratcher::cockpit {

class InstrumentContentPanel;

// Scratchers are observer-driven actors that own a portion of the panel's scene tree.
// Lifecycle:
//   * OnAttach   — the scratcher creates its sub-scene(s), attaches them to
//                  panel.HudScene() / panel.LogicalScene(), and (typically) subscribes
//                  to view-change notifications via panel.SubscribeView().
//   * CalculateSize — pre-layout phase; rulers shrink panel.MutableInnerDataRect() here
//                     to reserve their axis strips. Most scratchers leave it as a no-op.
//   * OnLayout   — post-layout phase; called after the inner-rect is finalised and
//                  ApplyLogicalSceneTransform has run. Scratchers update geometry to
//                  match the new layout. Anything they mutate after OnLayout should be
//                  announced via panel.MarkDirty for the damage-tracked render loop.
//   * OnDetach   — unsubscribe, remove sub-scenes, release resources.
struct Scratcher
{
    virtual ~Scratcher() = default;

    virtual void OnAttach(InstrumentContentPanel& panel) = 0;
    virtual void CalculateSize(InstrumentContentPanel& /*panel*/) {}
    virtual void OnLayout(InstrumentContentPanel& /*panel*/) {}
    virtual void OnDetach(InstrumentContentPanel& /*panel*/) {}
};

}
