/*
 * LinkVendored.h - the ONLY way to include the vendored Ableton Link (Q050-024).
 *
 * Link's POSIX interface scanner (ableton/platforms/posix/ScanIpIfAddrs.hpp) is an inline
 * function that calls getifaddrs. To let a test confine Link's discovery to the loopback
 * interface (AbletonLinkSession::setLoopbackOnly), that one call is routed to
 * lmms::linkseam::scan, defined once (non-inline) in src/core/AbletonLinkSession.cpp.
 * Every translation unit that includes Link.hpp must do it through this header, so every
 * one sees the SAME tokens for the scanner (no ODR violation between TUs of one binary).
 * The vendored headers themselves stay byte-identical to upstream.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_LINK_VENDORED_H
#define LMMS_LINK_VENDORED_H

#ifndef _WIN32
#include <ifaddrs.h>

#include "lmms_export.h"

namespace lmms::linkseam
{
//! getifaddrs, with every non-loopback entry marked not running while confinement is on.
LMMS_EXPORT int scan(struct ifaddrs** out);
}

#define getifaddrs(out) ::lmms::linkseam::scan(out)
#endif

#include <ableton/Link.hpp>

#ifndef _WIN32
#undef getifaddrs
#endif

#endif // LMMS_LINK_VENDORED_H
