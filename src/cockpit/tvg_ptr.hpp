// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <cstddef>

#include <thorvg.h>

namespace scratcher::cockpit {

// std::shared_ptr-style handle for ThorVG Paint subclasses (Scene, Shape, Text, ...).
// ThorVG already carries an intrusive ref count on every Paint via Paint::ref() /
// Paint::rel(); we wrap that model directly rather than std::shared_ptr's external
// control block so the ref count and the object stay in lockstep across native
// ThorVG APIs (Scene::add, Scene::clip, ...).
//
// Construction from a raw T* (typically T::gen()) bumps the ref count, so a freshly
// genned object held by a single tvg_ptr has refCnt = 2 (gen + wrapper). Handing it
// to a parent via Scene::add consumes one slot but leaves refCnt unchanged, so the
// wrapper still owns a slot until destruction; when wrapper and parent both release,
// refCnt hits 0 and ThorVG frees the object. Copy/move follow std::shared_ptr
// semantics — copy bumps ref, move transfers, destruction always rel()s.
template<typename T>
class tvg_ptr
{
public:
    constexpr tvg_ptr() noexcept = default;
    constexpr tvg_ptr(std::nullptr_t) noexcept {}

    explicit tvg_ptr(T* p) noexcept : mPtr{p} { if (mPtr) mPtr->ref(); }

    tvg_ptr(const tvg_ptr& o) noexcept : mPtr{o.mPtr} { if (mPtr) mPtr->ref(); }
    tvg_ptr(tvg_ptr&& o) noexcept : mPtr{o.mPtr} { o.mPtr = nullptr; }

    tvg_ptr& operator=(const tvg_ptr& o) noexcept
    {
        if (mPtr != o.mPtr) {
            if (o.mPtr) o.mPtr->ref();
            if (mPtr)   tvg::Paint::rel(mPtr);
            mPtr = o.mPtr;
        }
        return *this;
    }

    tvg_ptr& operator=(tvg_ptr&& o) noexcept
    {
        if (this != &o) {
            if (mPtr) tvg::Paint::rel(mPtr);
            mPtr = o.mPtr;
            o.mPtr = nullptr;
        }
        return *this;
    }

    ~tvg_ptr() noexcept { if (mPtr) tvg::Paint::rel(mPtr); }

    T* get() const noexcept { return mPtr; }
    T& operator*() const noexcept { return *mPtr; }
    T* operator->() const noexcept { return mPtr; }
    explicit operator bool() const noexcept { return mPtr != nullptr; }

    void reset(T* p = nullptr) noexcept
    {
        if (p == mPtr) return;
        if (p)    p->ref();
        if (mPtr) tvg::Paint::rel(mPtr);
        mPtr = p;
    }

private:
    T* mPtr = nullptr;
};

}
