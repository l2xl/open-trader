// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <cstddef>

#include <thorvg.h>

namespace scratcher::cockpit {

// std::shared_ptr-style handle for ThorVG Paint subclasses (Scene, Shape, Text, ...).
// ThorVG carries an intrusive ref count on every Paint (Paint::ref / Paint::unref).
//
// IMPORTANT — Paint::rel(paint) is NOT a counterpart to ref(): it only deletes the
// paint if refCnt() <= 0 and never decrements. We therefore use unref(true) here,
// which decrements and deletes when the count hits zero.
//
// Lifecycle: T::gen() returns a paint with refCnt = 0. The tvg_ptr ctor calls ref(),
// taking refCnt to 1. When the paint is handed to a parent via Scene::add the parent
// also calls ref() internally — refCnt becomes 2. Destruction of the wrapper calls
// unref() back to 1 (parent still holds), and the parent's own teardown later drops
// the last slot, freeing the paint.
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
            if (mPtr)   mPtr->unref();
            mPtr = o.mPtr;
        }
        return *this;
    }

    tvg_ptr& operator=(tvg_ptr&& o) noexcept
    {
        if (this != &o) {
            if (mPtr) mPtr->unref();
            mPtr = o.mPtr;
            o.mPtr = nullptr;
        }
        return *this;
    }

    ~tvg_ptr() noexcept { if (mPtr) mPtr->unref(); }

    T* get() const noexcept { return mPtr; }
    T& operator*() const noexcept { return *mPtr; }
    T* operator->() const noexcept { return mPtr; }
    explicit operator bool() const noexcept { return mPtr != nullptr; }

    void reset(T* p = nullptr) noexcept
    {
        if (p == mPtr) return;
        if (p)    p->ref();
        if (mPtr) mPtr->unref();
        mPtr = p;
    }

private:
    T* mPtr = nullptr;
};

}
