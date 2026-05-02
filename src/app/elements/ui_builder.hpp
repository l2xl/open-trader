// Scratcher project
// Copyright (c) 2025 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b21tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#pragma once

#include <memory>
#include <functional>
#include <string>
#include <vector>

#include <elements.hpp>
#include "content_panel.hpp"
#include "split_direction.hpp"

namespace scratcher::elements {

struct InstrumentPanelWidgets
{
    cycfi::elements::element_ptr root;
    std::shared_ptr<cycfi::elements::deck_composite> workArea;
    std::function<void(const std::vector<std::string>&, std::function<void(std::string)>)> SetInstruments;
    std::function<void(const std::string&)> SetTitle;
};

class UiBuilder
{
public:
    using PanelWidgets = std::pair<cycfi::elements::element_ptr, std::shared_ptr<cycfi::elements::deck_composite>>;

    InstrumentPanelWidgets MakeInstrumentPanel(cockpit::PanelType type, std::function<void()> onClose, std::function<void(cockpit::PanelType, SplitDirection)> onSplit);

    cycfi::elements::element_ptr MakeAppBar(cycfi::elements::element_ptr tab_bar_area, cycfi::elements::element_ptr menu_items, std::function<void(bool)> onHamburger);

    cycfi::elements::element_ptr MakeMenuItems(std::function<void()> onExit);
    cycfi::elements::element_ptr MakeLoadingSpinner();

    cycfi::elements::element_ptr MakePanelTypeSelector(uint32_t icon_code, std::function<void(cockpit::PanelType)> onTypeSelected);
    cycfi::elements::element_ptr MakeCloseButton(std::function<void()> onClose);

    std::pair<cycfi::elements::element_ptr, std::shared_ptr<cycfi::elements::deck_composite>> MakePanel(cockpit::PanelType type, std::function<void(cockpit::PanelType)> onChangeType, std::function<void()> onClose, std::function<void(cockpit::PanelType, SplitDirection)> onSplit);

    cycfi::elements::element_ptr MakeVerticalSplit(cycfi::elements::element_ptr left, cycfi::elements::element_ptr right);
    cycfi::elements::element_ptr MakeHorizontalSplit(cycfi::elements::element_ptr top, cycfi::elements::element_ptr bottom);
    cycfi::elements::element_ptr MakeDivider(bool horizontal);

private:
    cycfi::elements::element_ptr MakePanelHeader(cockpit::PanelType type, std::function<void(cockpit::PanelType)> onChangeType, std::function<void()> onClose, std::function<void(cockpit::PanelType, SplitDirection)> onSplit);
    cycfi::elements::element_ptr MakePanelFooter(std::function<void(cockpit::PanelType, SplitDirection)> onSplit);
    cycfi::elements::element_ptr MakeWaitingIndicator();
};

} // namespace scratcher::elements
