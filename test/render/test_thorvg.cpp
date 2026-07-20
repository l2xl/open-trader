// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <thorvg.h>
#include <cairo/cairo.h>

namespace {

uint8_t alpha_of(uint32_t pixel)
{
    return static_cast<uint8_t>((pixel >> 24) & 0xFFu);
}

template <uint32_t W, uint32_t H>
uint32_t pixel_at(const std::array<uint32_t, W * H>& buf, uint32_t x, uint32_t y)
{
    return buf[y * W + x];
}

uint32_t cairo_pixel_at(const cairo_surface_t* surface, uint32_t x, uint32_t y)
{
    auto* surface_mut = const_cast<cairo_surface_t*>(surface);
    auto stride = static_cast<uint32_t>(cairo_image_surface_get_stride(surface_mut));
    const auto* data = cairo_image_surface_get_data(surface_mut);
    return *reinterpret_cast<const uint32_t*>(data + y * stride + x * sizeof(uint32_t));
}

// Builds a 3x3 cyrillic-Г figure as a single filled shape:
//   X X X
//   X . .
//   X . .
// Two appended sub-rects compose the L-on-its-back glyph in path coords [0..3]x[0..3].
tvg::Shape* make_g_shape()
{
    auto* shape = tvg::Shape::gen();
    shape->appendRect(0.0f, 0.0f, 3.0f, 1.0f);
    shape->appendRect(0.0f, 1.0f, 1.0f, 2.0f);
    shape->fill(255, 255, 255, 255);
    return shape;
}

// Builds a "virtual surface" Scene: a clipped, Y-flipped sub-region in which child paints
// are placed in their own (e.g. human-native) coords. Pure ThorVG: Scene = Paint, so it
// owns transform() + clip() and propagates them to every child added via add().
tvg::Scene* make_virtual_surface(float w, float h, float scroll_x = 0.0f)
{
    auto* surface = tvg::Scene::gen();
    auto* clipper = tvg::Shape::gen();
    clipper->appendRect(0.0f, 0.0f, w, h);
    surface->clip(clipper);
    surface->transform(tvg::Matrix{1.0f, 0.0f, scroll_x,
                                   0.0f, -1.0f, h,
                                   0.0f, 0.0f, 1.0f});
    return surface;
}

void check_matrix_equals(const tvg::Matrix& m, const tvg::Matrix& expected)
{
    CHECK(m.e11 == expected.e11);  CHECK(m.e12 == expected.e12);  CHECK(m.e13 == expected.e13);
    CHECK(m.e21 == expected.e21);  CHECK(m.e22 == expected.e22);  CHECK(m.e23 == expected.e23);
    CHECK(m.e31 == expected.e31);  CHECK(m.e32 == expected.e32);  CHECK(m.e33 == expected.e33);
}

void check_l_at(auto pixel_reader, uint32_t W, uint32_t H, uint32_t left_col)
{
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            const bool inside_l = (x == left_col) || (y == H - 1 && x >= left_col && x < left_col + 3);
            const auto a = alpha_of(pixel_reader(x, y));
            if (inside_l) {
                CHECK(a == 0xFFu);
            } else {
                CHECK(a == 0x00u);
            }
        }
    }
}

}

TEST_CASE("ThorVG renders a 1px square outline into a 5x5 ARGB buffer", "[thorvg][render]")
{
    constexpr uint32_t canvas_width  = 5;
    constexpr uint32_t canvas_height = 5;

    REQUIRE(tvg::Initializer::init(0) == tvg::Result::Success);

    std::array<uint32_t, canvas_width * canvas_height> buffer{};

    tvg::SwCanvas* canvas = tvg::SwCanvas::gen();
    REQUIRE(canvas);
    REQUIRE(canvas->target(buffer.data(), canvas_width, canvas_width, canvas_height, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);

    tvg::Shape* shape = tvg::Shape::gen();
    REQUIRE(shape);
    shape->moveTo(0.0f, 0.0f);
    shape->lineTo(4.0f, 0.0f);
    shape->lineTo(4.0f, 4.0f);
    shape->lineTo(0.0f, 4.0f);
    shape->close();
    shape->strokeFill(255, 255, 255, 255);
    shape->strokeWidth(1.0f);

    REQUIRE(canvas->add(shape) == tvg::Result::Success);
    REQUIRE(canvas->draw() == tvg::Result::Success);
    REQUIRE(canvas->sync() == tvg::Result::Success);

    // The 1px stroke is centred on the integer path, so on each side it spreads ±0.5px.
    // For a path traced at columns/rows {0, 4}, the stroke covers pixel indices {0, 3, 4}.
    // The truly enclosed interior is therefore the 2x2 region at indices {1, 2} x {1, 2}.

    SECTION("stroke covers every pixel along the four edges")
    {
        for (uint32_t i = 0; i < canvas_width; ++i) {
            CHECK(alpha_of(pixel_at<canvas_width, canvas_height>(buffer, i, 0)) > 0);
            CHECK(alpha_of(pixel_at<canvas_width, canvas_height>(buffer, i, canvas_height - 1)) > 0);
            CHECK(alpha_of(pixel_at<canvas_width, canvas_height>(buffer, 0, i)) > 0);
            CHECK(alpha_of(pixel_at<canvas_width, canvas_height>(buffer, canvas_width - 1, i)) > 0);
        }
    }

    SECTION("enclosed interior remains fully transparent")
    {
        CHECK(alpha_of(pixel_at<canvas_width, canvas_height>(buffer, 1, 1)) == 0);
        CHECK(alpha_of(pixel_at<canvas_width, canvas_height>(buffer, 2, 1)) == 0);
        CHECK(alpha_of(pixel_at<canvas_width, canvas_height>(buffer, 1, 2)) == 0);
        CHECK(alpha_of(pixel_at<canvas_width, canvas_height>(buffer, 2, 2)) == 0);
    }

    delete canvas;
    REQUIRE(tvg::Initializer::term() == tvg::Result::Success);
}


TEST_CASE("Virtual surface (Scene) transposes Y so a Г child renders as L", "[thorvg][scene][transform]")
{
    constexpr uint32_t W = 3;
    constexpr uint32_t H = 3;

    REQUIRE(tvg::Initializer::init(0) == tvg::Result::Success);

    std::array<uint32_t, W * H> buffer{};

    auto* canvas = tvg::SwCanvas::gen();
    REQUIRE(canvas->target(buffer.data(), W, W, H, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);

    // The Scene IS the transformation area. The Г child is placed plainly inside;
    // the surface's transform alone makes it appear flipped on canvas.
    auto* surface = make_virtual_surface(static_cast<float>(W), static_cast<float>(H));
    surface->add(make_g_shape());

    check_matrix_equals(surface->transform(),
                        tvg::Matrix{1.0f, 0.0f, 0.0f,  0.0f, -1.0f, 3.0f,  0.0f, 0.0f, 1.0f});

    REQUIRE(canvas->add(surface) == tvg::Result::Success);
    REQUIRE(canvas->draw(true) == tvg::Result::Success);
    REQUIRE(canvas->sync() == tvg::Result::Success);

    auto reader = [&](uint32_t x, uint32_t y) { return pixel_at<W, H>(buffer, x, y); };
    check_l_at(reader, W, H, 0);

    delete canvas;
    REQUIRE(tvg::Initializer::term() == tvg::Result::Success);
}


TEST_CASE("Scroll-box: stationary 5x3 viewport with a scrollable inner surface", "[thorvg][scene][incremental]")
{
    constexpr uint32_t W = 5;
    constexpr uint32_t H = 3;

    REQUIRE(tvg::Initializer::init(0) == tvg::Result::Success);

    std::array<uint32_t, W * H> buffer{};

    auto* canvas = tvg::SwCanvas::gen();
    REQUIRE(canvas->target(buffer.data(), W, W, H, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);

    // Outer Scene = stationary viewport: just the clip, identity transform.
    auto* viewport = tvg::Scene::gen();
    auto* viewport_clipper = tvg::Shape::gen();
    viewport_clipper->appendRect(0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H));
    viewport->clip(viewport_clipper);

    // Inner Scene = the scrollable surface: Y-flipped, scroll-x-translated.
    // Children are placed in surface-local coords and don't know about scrolling.
    auto* surface = make_virtual_surface(static_cast<float>(W), static_cast<float>(H));
    surface->add(make_g_shape());

    check_matrix_equals(viewport->transform(),
                        tvg::Matrix{1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f});
    check_matrix_equals(surface->transform(),
                        tvg::Matrix{1.0f, 0.0f, 0.0f,  0.0f, -1.0f, 3.0f,  0.0f, 0.0f, 1.0f});

    viewport->add(surface);
    REQUIRE(canvas->add(viewport) == tvg::Result::Success);
    REQUIRE(canvas->draw(true) == tvg::Result::Success);
    REQUIRE(canvas->sync() == tvg::Result::Success);

    auto reader = [&](uint32_t x, uint32_t y) { return pixel_at<W, H>(buffer, x, y); };

    SECTION("initial pose: L at the left of the viewport")
    {
        check_l_at(reader, W, H, 0);
    }

    SECTION("scrolling the inner surface +2 X relocates all its content as a whole")
    {
        // Mutate just the surface's transform — Canvas::update() reprocesses this paint and
        // every child it carries, while the viewport clip stays put.
        surface->transform(tvg::Matrix{1.0f, 0.0f, 2.0f,
                                       0.0f, -1.0f, static_cast<float>(H),
                                       0.0f, 0.0f, 1.0f});

        check_matrix_equals(surface->transform(),
                            tvg::Matrix{1.0f, 0.0f, 2.0f,  0.0f, -1.0f, 3.0f,  0.0f, 0.0f, 1.0f});
        check_matrix_equals(viewport->transform(),
                            tvg::Matrix{1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f});

        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(true) == tvg::Result::Success);
        REQUIRE(canvas->sync() == tvg::Result::Success);

        check_l_at(reader, W, H, 2);
    }

    delete canvas;
    REQUIRE(tvg::Initializer::term() == tvg::Result::Success);
}


TEST_CASE("Scroll-box rendered into a Cairo backing surface (zero-copy)", "[thorvg][scene][cairo]")
{
    constexpr uint32_t W = 5;
    constexpr uint32_t H = 3;

    REQUIRE(tvg::Initializer::init(0) == tvg::Result::Success);

    // Cairo owns the pixel store; ThorVG draws straight into it.
    cairo_surface_t* cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    REQUIRE(cairo_surface_status(cairo_surface) == CAIRO_STATUS_SUCCESS);

    cairo_surface_flush(cairo_surface);
    auto* cairo_data = cairo_image_surface_get_data(cairo_surface);
    const auto cairo_stride_pixels = static_cast<uint32_t>(cairo_image_surface_get_stride(cairo_surface) / 4);

    auto* canvas = tvg::SwCanvas::gen();
    REQUIRE(canvas->target(reinterpret_cast<uint32_t*>(cairo_data), cairo_stride_pixels, W, H, tvg::ColorSpace::ARGB8888) == tvg::Result::Success);

    auto* viewport = tvg::Scene::gen();
    auto* viewport_clipper = tvg::Shape::gen();
    viewport_clipper->appendRect(0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H));
    viewport->clip(viewport_clipper);

    auto* surface = make_virtual_surface(static_cast<float>(W), static_cast<float>(H));
    surface->add(make_g_shape());

    check_matrix_equals(viewport->transform(),
                        tvg::Matrix{1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f, 1.0f});
    check_matrix_equals(surface->transform(),
                        tvg::Matrix{1.0f, 0.0f, 0.0f,  0.0f, -1.0f, 3.0f,  0.0f, 0.0f, 1.0f});

    viewport->add(surface);
    REQUIRE(canvas->add(viewport) == tvg::Result::Success);
    REQUIRE(canvas->draw(true) == tvg::Result::Success);
    REQUIRE(canvas->sync() == tvg::Result::Success);
    cairo_surface_mark_dirty(cairo_surface);

    auto reader = [&](uint32_t x, uint32_t y) { return cairo_pixel_at(cairo_surface, x, y); };

    SECTION("first frame: ThorVG wrote L straight into Cairo's buffer")
    {
        check_l_at(reader, W, H, 0);
    }

    SECTION("scroll mutation: Cairo buffer is repainted in place, viewport clip stays put")
    {
        surface->transform(tvg::Matrix{1.0f, 0.0f, 2.0f,
                                       0.0f, -1.0f, static_cast<float>(H),
                                       0.0f, 0.0f, 1.0f});
        REQUIRE(canvas->update() == tvg::Result::Success);
        REQUIRE(canvas->draw(true) == tvg::Result::Success);
        REQUIRE(canvas->sync() == tvg::Result::Success);
        cairo_surface_mark_dirty(cairo_surface);

        check_l_at(reader, W, H, 2);
    }

    delete canvas;
    cairo_surface_destroy(cairo_surface);
    REQUIRE(tvg::Initializer::term() == tvg::Result::Success);
}
