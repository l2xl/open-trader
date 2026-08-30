// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <memory>
#include <utility>

#include <cairo/cairo.h>
#include <elements.hpp>

#include "instrument_panel.hpp"
#include "vector_scene_panel.hpp"
#include "wallet_panel.hpp"

namespace scratcher::elements {

// Cycfi/Elements face of a cockpit::VectorScenePanel: wraps the panel-owned render buffer in a
// cairo image surface for elements painting and routes layout()/draw() to the panel's
// AllocatePixelBuffer/OnUpdate/Render. Knows only the VectorScenePanel host contract; the concrete
// panel is glued on by VectorScenePanelElement<Panel>, which IS both the panel and this element.
class VectorSceneElement : public cycfi::elements::element
{
public:
    ~VectorSceneElement() override;

    cycfi::elements::view_limits limits(cycfi::elements::basic_context const&) const override;
    void layout(cycfi::elements::context const& ctx) override;
    void draw(cycfi::elements::context const& ctx) override;

protected:
    VectorSceneElement(cockpit::VectorScenePanel& panel, std::weak_ptr<cycfi::elements::view> view);

    void PostRefresh();

private:
    cockpit::VectorScenePanel& mPanel;
    std::weak_ptr<cycfi::elements::view> mView;
    cairo_surface_t* mSurface = nullptr;    // cairo C API handle; released in the destructor
};

template<class Panel>
class VectorScenePanelElement final : public Panel, public VectorSceneElement
{
    struct EnsurePrivate {};

public:
    template<class... Args>
    VectorScenePanelElement(std::weak_ptr<cycfi::elements::view> view, EnsurePrivate, Args&&... args)
        : Panel(std::forward<Args>(args)...)
        , VectorSceneElement(static_cast<cockpit::VectorScenePanel&>(static_cast<Panel&>(*this)), std::move(view))
    {}

    template<class... Args>
    static std::shared_ptr<VectorScenePanelElement> Create(std::weak_ptr<cycfi::elements::view> view, Args&&... args)
    { return std::make_shared<VectorScenePanelElement>(std::move(view), EnsurePrivate{}, std::forward<Args>(args)...); }

    void Update() override
    {
        Panel::Update();
        PostRefresh();
    }

    void Refresh() override { PostRefresh(); }
};

using InstrumentPanelElement = VectorScenePanelElement<cockpit::InstrumentPanel>;
using WalletPanelElement = VectorScenePanelElement<cockpit::WalletPanel>;

} // namespace scratcher::elements
