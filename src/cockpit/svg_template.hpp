// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include <thorvg.h>

#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

// SVG document parsed by ThorVG's loader and detached from the hosting Picture: a loaded Picture
// skips Canvas::update() unless it is itself marked dirty, so paints mutated inside it never
// re-render. The detached duplicate is an ordinary Scene the owner places under its own tree, and
// every element `id` survives as Paint::id (djb2 hash, see tvg::Accessor::id) in the lookup index.
// PRECONDITION: the ThorVG runtime is initialised and the fonts the document references are loaded.
class svg_template
{
public:
    static svg_template load(std::string_view svg);
    static svg_template load_file(const std::filesystem::path& path);

    const tvg_ptr<tvg::Scene>& root() const { return m_root; }

    // Typed lookup by element id; throws when the id is absent or the element is of another kind.
    template<class Paint>
    tvg_ptr<Paint> get(std::string_view id) const
    { return tvg_ptr<Paint>{static_cast<Paint*>(find(id, paint_type<Paint>()).get())}; }

private:
    using index_type = std::unordered_map<uint32_t, tvg_ptr<tvg::Paint>>;

    svg_template(tvg_ptr<tvg::Scene> root, index_type index) : m_root(std::move(root)), m_index(std::move(index)) {}

    const tvg_ptr<tvg::Paint>& find(std::string_view id, tvg::Type type) const;

    template<class> static constexpr bool dependent_false = false;

    template<class Paint>
    static constexpr tvg::Type paint_type()
    {
        if constexpr (std::is_same_v<Paint, tvg::Scene>) return tvg::Type::Scene;
        else if constexpr (std::is_same_v<Paint, tvg::Shape>) return tvg::Type::Shape;
        else if constexpr (std::is_same_v<Paint, tvg::Text>) return tvg::Type::Text;
        else if constexpr (std::is_same_v<Paint, tvg::Picture>) return tvg::Type::Picture;
        else static_assert(dependent_false<Paint>, "svg_template::get supports tvg::Scene, Shape, Text and Picture");
    }

    tvg_ptr<tvg::Scene> m_root;
    index_type m_index;
};

} // namespace scratcher::cockpit
