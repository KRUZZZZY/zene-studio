/*
 * zene/api/ControlApi.h - the zene::api boundary: the control registry's public
 *                         surface as ONE named module (ARCH-2, board card #672).
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

#ifndef ZENE_API_CONTROL_API_H
#define ZENE_API_CONTROL_API_H

/* THE BOUNDARY. The registry (ControlRegistry), the contract table it stamps
 * transactions from (ControlReversibility) and the undo-coalescing rule
 * (ControlUndoCoalescing) are the headless core of the control surface: they
 * must build with no Qt widget include reachable, so that a build without a
 * display - headless render, the agent control socket, a test - can carry the
 * whole command surface and not the GUI.
 *
 * What enforces it, in order of strength:
 *   1. the `zene_api` target (src/CMakeLists.txt) compiles this boundary's
 *      translation units with the tree's Qt widget include directory removed
 *      and without linking Qt6::Widgets, so `#include <QWidget>` in a boundary
 *      TU is a COMPILE ERROR in every CI job;
 *   2. the registered `ZeneApiBoundary` ctest (tests/zene-api-boundary.py)
 *      re-measures the boundary's include closure and object symbols on every
 *      test run, and fails if a widget becomes reachable without an include
 *      (e.g. Qt6::Widgets linked back onto the target).
 *
 * What is NOT in the boundary, and why: the display-side command groups.
 * arrangement/clip selection sync, the controller surface's view half, the live
 * menu/toolbar reflection (control.surface_report) and the telemetry consent
 * screen reach QtWidgets through SongEditor.h / TrackView.h / MainWindow.h by
 * design - they read GUI state - so those translation units stay in the
 * object library and register into this registry from there. The id set is
 * unchanged: the registry is one singleton, whoever calls into it.
 *
 * The historical `lmms::` names stay the implementation names. This header is
 * the module's public entry point - a caller includes it and uses `zene::api::`
 * - and re-exporting rather than renaming is deliberate: a rename would touch
 * every command group's translation unit for no build-level gain, and the
 * boundary is enforced by the target and the test above, not by a namespace.
 */

#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlUndoCoalescing.h"
#include "ControlVocabulary.h"

/* The boundary must never be compiled with Qt Widgets. The definition below is
 * set by the boundary target; a TU that includes this header while its include
 * path DOES reach QtWidgets fails here with the reason rather than at some
 * later link step. (The primary gate is the target's include path - this is the
 * named diagnostic for the case where someone restores it.) */
#if defined(ZENE_API_BOUNDARY) && defined(QT_WIDGETS_LIB)
#error "the zene::api boundary (ARCH-2) must not be compiled with Qt Widgets: the registry, its contract table and its undo-coalescing rule are the headless core of the control surface, so a widget include in this module is a defect (see docs/reports/ARCH2-BOUNDARY-REPORT.md)"
#endif

namespace zene
{
namespace api
{

//! The registry's wire/transaction vocabulary (SPEC A11/A16).
using ::lmms::ControlCommand;
using ::lmms::ControlErrorKind;
using ::lmms::ControlRegistry;
using ::lmms::ControlResult;

//! The A16 contract table and its row types (the registry's `control` helpers
//! namespace: the contract table lives beside the id formatters it describes).
using ::lmms::control::ReversibilityClass;
using ::lmms::control::ReversibilityRow;
using ::lmms::control::reversibilityClassName;
using ::lmms::control::reversibilityRowTable;

//! The undo-coalescing rule ("two commands are one undo step when ...").
using ::lmms::control::UndoCoalescer;
using ::lmms::control::coalescingRuleText;

/*! Register every command group into \p registry - the composition entry point
 *  (SPEC A11). It calls into the display-side groups too, whose translation
 *  units live outside this boundary; the registry itself does not care. */
using ::lmms::registerControlCommands;

} // namespace api
} // namespace zene

#endif // ZENE_API_CONTROL_API_H
