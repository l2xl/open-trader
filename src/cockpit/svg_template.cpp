// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "svg_template.hpp"

#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace scratcher::cockpit {

namespace {

using index_type = std::unordered_map<uint32_t, tvg_ptr<tvg::Paint>>;

// Paint::duplicate() copies geometry, style and transform but not Paint::id; walk the loader tree
// and its duplicate in lock-step to carry the ids across and index the duplicate's paints.
void remap_ids(const tvg::Paint& source, tvg::Paint& target, index_type& index)
{
    target.id = source.id;
    if (source.id) index.emplace(source.id, tvg_ptr<tvg::Paint>{&target});
    if (source.type() != tvg::Type::Scene) return;

    const auto& source_children = static_cast<const tvg::Scene&>(source).paints();
    const auto& target_children = static_cast<tvg::Scene&>(target).paints();
    auto s = source_children.begin();
    auto t = target_children.begin();
    for (; s != source_children.end() && t != target_children.end(); ++s, ++t)
        remap_ids(**s, **t, index);
}

}

svg_template svg_template::load(std::string_view svg)
{
    tvg_ptr<tvg::Picture> picture{tvg::Picture::gen()};
    if (picture->load(svg.data(), static_cast<uint32_t>(svg.size()), "svg", nullptr, true) != tvg::Result::Success)
        throw std::runtime_error("svg_template: ThorVG rejected the SVG document");

    // The Picture's iterator yields exactly one child: the loader-built root scene.
    const tvg::Paint* root = nullptr;
    std::unique_ptr<tvg::Accessor> accessor{tvg::Accessor::gen()};
    accessor->set(picture.get(), [](const tvg::Paint* paint, void* data) {
        if (paint->type() == tvg::Type::Picture) return true;
        *static_cast<const tvg::Paint**>(data) = paint;
        return false;
    }, &root);
    if (!root || root->type() != tvg::Type::Scene)
        throw std::runtime_error("svg_template: the SVG document produced no scene");

    tvg_ptr<tvg::Scene> detached{static_cast<tvg::Scene*>(root->duplicate())};
    // The loader wraps the document in a layer clipped to its viewBox; a template is geometry the
    // owner lays out and clips itself, so the document viewport must not survive the detach.
    detached->clip(nullptr);
    for (auto layer : detached->paints()) layer->clip(nullptr);
    index_type index;
    remap_ids(*root, *detached, index);
    return svg_template{std::move(detached), std::move(index)};
}

svg_template svg_template::load_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("svg_template: cannot read " + path.string());
    std::string svg{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return load(svg);
}

const tvg_ptr<tvg::Paint>& svg_template::find(std::string_view id, tvg::Type type) const
{
    auto it = m_index.find(tvg::Accessor::id(std::string{id}.c_str()));
    if (it == m_index.end()) throw std::runtime_error("svg_template: no element with id '" + std::string{id} + "'");
    if (it->second->type() != type) throw std::runtime_error("svg_template: element '" + std::string{id} + "' is not of the requested kind");
    return it->second;
}

} // namespace scratcher::cockpit
