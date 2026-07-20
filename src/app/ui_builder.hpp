// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <elements.hpp>

#include "content_panel.hpp"
#include "split_direction.hpp"

namespace scratcher::elements {

class InstrumentPanelNode;

class UiBuilder
{
public:
    std::shared_ptr<InstrumentPanelNode> MakeInstrumentPanel(std::shared_ptr<cycfi::elements::view> root_view, cockpit::PanelType type, std::function<void()> onClose, std::function<void(cockpit::PanelType, SplitDirection)> onSplit);

    cycfi::elements::element_ptr MakeAppBar(cycfi::elements::element_ptr tab_bar_area, cycfi::elements::element_ptr menu_items, std::function<void(bool)> onHamburger);

    cycfi::elements::element_ptr MakeMenuItems(std::function<void()> onExit);
    cycfi::elements::element_ptr MakeLoadingSpinner();

    cycfi::elements::element_ptr MakePanelTypeSelector(uint32_t icon_code, std::function<void(cockpit::PanelType)> onTypeSelected);
    cycfi::elements::element_ptr MakeCloseButton(std::function<void()> onClose);

    cycfi::elements::element_ptr MakePanel(cockpit::PanelType type, std::function<void(cockpit::PanelType)> onChangeType, std::function<void()> onClose, std::function<void(cockpit::PanelType, SplitDirection)> onSplit);

    cycfi::elements::element_ptr MakeVerticalSplit(cycfi::elements::element_ptr left, cycfi::elements::element_ptr right);
    cycfi::elements::element_ptr MakeHorizontalSplit(cycfi::elements::element_ptr top, cycfi::elements::element_ptr bottom);
    cycfi::elements::element_ptr MakeDivider(bool horizontal);

private:
    cycfi::elements::element_ptr MakePanelHeader(cockpit::PanelType type, std::function<void(cockpit::PanelType)> onChangeType, std::function<void()> onClose, std::function<void(cockpit::PanelType, SplitDirection)> onSplit);
    cycfi::elements::element_ptr MakePanelFooter(std::function<void(cockpit::PanelType, SplitDirection)> onSplit);
    cycfi::elements::element_ptr MakeWaitingIndicator();
};

} // namespace scratcher::elements
