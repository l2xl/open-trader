// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef TIMEDEF_HPP
#define TIMEDEF_HPP

#include <chrono>
#include <cstdint>

namespace scratcher {

// system_clock (Unix ms, leap-second-free) is THE project time frame: wire trade timestamps,
// buoy candle times and the render/view edge all live here. utc_clock must never be mixed in —
// it carries leap seconds (~27 s in 2026), which skews the live edge and the visible-buoy span.
typedef std::chrono::system_clock::time_point time_point;
typedef time_point::duration duration;

using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::minutes;
using std::chrono::hours;
using std::chrono::days;
using std::chrono::weeks;
using std::chrono::months;
using std::chrono::years;

using std::chrono::sys_time;
using std::chrono::sys_days;
using std::chrono::year_month_day;
using std::chrono::weekday;
using std::chrono::Monday;
using std::chrono::last;
using std::chrono::floor;
using std::chrono::duration_cast;

inline uint64_t get_timestamp(time_point t)
{ return duration_cast<milliseconds>(t.time_since_epoch()).count(); }

}

#endif //TIMEDEF_HPP
