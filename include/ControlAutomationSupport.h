/*
 * ControlAutomationSupport.h - shared helpers for the automation.* command
 *                              group (SPEC A11-A16).
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

#ifndef LMMS_CONTROL_AUTOMATION_SUPPORT_H
#define LMMS_CONTROL_AUTOMATION_SUPPORT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;
class AutomationClip;
struct ControlTarget;

namespace control
{

// ---------------------------------------------------------------------------
// the small JSON-schema subset every group uses
// ---------------------------------------------------------------------------

LMMS_EXPORT QJsonObject schemaObject(QJsonObject properties, QJsonArray required = {});
LMMS_EXPORT QJsonObject stringProperty();
LMMS_EXPORT QJsonObject numberProperty();
LMMS_EXPORT QJsonObject booleanProperty();
LMMS_EXPORT QJsonObject integerProperty(int minimum, int maximum);
//! A tick position: an integer that cannot be negative.
LMMS_EXPORT QJsonObject tickProperty();

// ---------------------------------------------------------------------------
// parameter addressing
// ---------------------------------------------------------------------------

/*! One automatable parameter of a target, with the device that owns it.
 *
 * The id is "<plugin>/<index>"; \c plugin is "inst" (the track's instrument) or
 * an "fx-<n>" device instance id, exactly the ids plugin.param_get and
 * dsp.get_state already use, so a parameter found there is automated here with
 * no second lookup scheme.
 */
struct AutomationParameter
{
	QString pluginId;
	QString devicePlugin;
	int index = 0;
	AutomatableModel* model = nullptr;

	QString id() const;
};

//! Every parameter of \a target in plugin.param_get's order.
LMMS_EXPORT QList<AutomationParameter> automationParameters(const ControlTarget& target);

//! Resolves a "<plugin>/<index>" id; false and *error set on failure.
LMMS_EXPORT bool findAutomationParameter(const ControlTarget& target, const QString& id,
	AutomationParameter* out, ControlResult* error);

//! invalid_args naming the model's own min..max (the same refusal plugin.param_set uses).
LMMS_EXPORT ControlResult automatableRangeRefusal(const AutomatableModel* model, double value);

// ---------------------------------------------------------------------------
// automation clips
// ---------------------------------------------------------------------------

//! The first clip bound to \a model, whether or not it still has points.
LMMS_EXPORT AutomationClip* existingAutomationClip(AutomatableModel* model);

/*! The clip that automates \a model, or a fresh one on a new AutomationTrack.
 *
 * The song's *global* automation track is deliberately not used: it is a
 * Track::Type::HiddenAutomation track, TrackContainer::addTrack() keeps those
 * out of the container's own list and the project file no longer serialises
 * them (DataFile::upgrade_noHiddenAutomationTracks), so automation written
 * there would vanish the moment render.render re-serialises the session. A
 * regular AutomationTrack is saved, loaded and played like any other track.
 *
 * \a created is set when this call had to create the track and the clip.
 */
LMMS_EXPORT AutomationClip* automationClipForModel(AutomatableModel* model, bool* created,
	ControlResult* error);

// ---------------------------------------------------------------------------
// read-back
// ---------------------------------------------------------------------------

//! The clip's nodes: "value" is the model's own unit (plugin.param_get's unit),
//! "raw_value" is what the clip stores (Song::processAutomations applies the
//! model's scale curve to it).
LMMS_EXPORT QJsonArray automationPointsJson(AutomationClip* clip, const AutomatableModel* model);

//! The clip as automation.get_state and the mutating commands report it.
LMMS_EXPORT QJsonObject automationJson(AutomationClip* clip, const AutomatableModel* model);

//! One parameter: its id, its range, its value and its automation.
LMMS_EXPORT QJsonObject automationParameterJson(const AutomationParameter& parameter);

// ---------------------------------------------------------------------------
// A16 transactions
// ---------------------------------------------------------------------------

/*! The transaction a mutating automation command records.
 *
 * A clip the engine already had takes a ProjectJournal checkpoint before the
 * edit, so control.undo reverses it. A clip this command *created* is not:
 * ProjectJournal has no checkpoint for the AutomationTrack creation (track.add
 * and track.remove are the same gap), so the brand-new track and clip survive a
 * control.undo. The two cases are recorded apart rather than assumed.
 */
LMMS_EXPORT QJsonObject automationTransaction(const AutomationParameter& parameter,
	const QJsonArray& before, int pointCountBefore, bool created);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_AUTOMATION_SUPPORT_H
