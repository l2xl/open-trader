// Scratcher project
// Copyright (c) 2025 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b25tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#ifndef TIMEDEF_HPP
#define TIMEDEF_HPP

#include <chrono>

namespace scratcher {

typedef std::chrono::utc_clock::time_point time_point;
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
using std::chrono::utc_clock;
using std::chrono::last;
using std::chrono::floor;
using std::chrono::duration_cast;

inline uint64_t get_timestamp(time_point t)
{ return duration_cast<milliseconds>(t.time_since_epoch()).count(); }

}

#endif //TIMEDEF_HPP
