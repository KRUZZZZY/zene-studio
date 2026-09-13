/*
 * RackMacros.h - a rack's macros: named, persisted scalars that drive a set of
 *                existing model parameters, each within its own range window.
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

#ifndef LMMS_RACK_MACROS_H
#define LMMS_RACK_MACROS_H

#include <vector>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;
class Rack;

/**
 * @brief One existing model parameter a macro drives, with the window it maps
 *        the macro's own 0..1 value onto.
 *
 * The window is stated as a FRACTION of the parameter's own range, not in the
 * parameter's units: `low = 0.25, high = 0.75` on a resonance control that runs
 * 0..1 means "never below 0.25 and never above 0.75", and on a level in dB it
 * means the same quarter-to-three-quarters span of whatever range that model
 * declares. Stating the window this way is what makes a macro survive a
 * parameter whose range the engine defines - the alternative (recording the
 * engine's numbers) would silently pin the macro to the range a particular
 * build happened to report.
 *
 * `high < low` is legal and inverts the drive, which is what a "less when the
 * macro is up" assignment is.
 */
struct RackMacroTarget
{
	//! Chain index in the rack: 0 is the channel's own chain.
	int chain = 0;
	//! Index of the effect in that chain, i.e. the fx-<n> id.
	int effect = 0;
	//! The parameter's display name, exactly as plugin.param_get reports it.
	QString parameter;
	//! Window start, as a fraction of the parameter's own min..max.
	float low = 0.0f;
	//! Window end, same units.
	float high = 1.0f;
};

//! A named, persisted scalar and the parameters it drives.
struct RackMacro
{
	QString name;
	//! 0..1. Every target's window is mapped from this one number.
	float value = 0.0f;
	std::vector<RackMacroTarget> targets;
};

//! One parameter a macro wrote, so the caller can record its inverse.
struct RackMacroWrite
{
	RackMacroTarget target;
	float previous = 0.0f;
	float written = 0.0f;
};

//! True when the target's window is a sane fraction pair. The chain, the
//! effect and the parameter name are checked against the live rack instead
//! (rackMacroTargetModel), because only the rack knows them.
LMMS_EXPORT bool isValidMacroTarget(const RackMacroTarget& target);

/**
 * @brief The model a target names on @a rack, or nullptr.
 *
 * The resolution is by NAME against the effect's own parameter list - the same
 * list, in the same order, that plugin.param_get reports - so a macro's
 * target is the same parameter to a human and to an agent. Because it resolves
 * by name it keeps working across a save/reload, where no model pointer
 * survives.
 *
 * @param why when non-null, receives the reason for a failure. Allocates:
 *        control thread only.
 */
LMMS_EXPORT AutomatableModel* rackMacroTargetModel(Rack& rack, const RackMacroTarget& target,
	QString* why);

/**
 * @brief A rack's macro list.
 *
 * Configuration is control thread only, like the rest of the rack: the audio
 * thread never reads a macro, so nothing here is on the audio path at all. A
 * macro is persisted as a `<macro>` child of the channel's existing `<rack>`
 * element - there is no second container - and its targets as `<target>`
 * children of that.
 */
class LMMS_EXPORT RackMacros
{
public:
	RackMacros() = default;

	RackMacros(const RackMacros&) = delete;
	auto operator=(const RackMacros&) -> RackMacros& = delete;

	auto macroCount() const -> int;
	auto macro(int index) const -> const RackMacro*;
	auto macro(int index) -> RackMacro*;

	//! Appends a macro and returns its index (the macro-<n> id).
	auto addMacro(const QString& name, float value) -> int;
	//! Inserts a macro at @a index, for the inverse of a removal: the list and
	//! the macro-<n> ids come back exactly, not merely the contents.
	auto insertMacro(int index, const RackMacro& macro) -> bool;
	auto removeMacro(int index) -> bool;

	//! Stores @a value clamped to 0..1. False when there is no such macro.
	auto setValue(int index, float value) -> bool;

	//! Appends a target; -1 when there is no such macro.
	auto addTarget(int index, const RackMacroTarget& target) -> int;
	//! Inserts a target at @a targetIndex, for the inverse of a removal.
	auto insertTarget(int index, int targetIndex, const RackMacroTarget& target) -> bool;
	auto removeTarget(int index, int targetIndex) -> bool;

	/**
	 * Writes the macro @a index's value onto every target's model, through
	 * that model's own range window, and returns what it wrote - one entry per
	 * target it could resolve, in target order. A target that no longer
	 * resolves (its chain, its effect or its parameter is gone) is SKIPPED
	 * rather than guessed at, so the returned size is also the applied count.
	 *
	 * Takes a journal checkpoint of every model it writes, so the one merge
	 * the command registry performs after a handler covers the whole call as a
	 * single undo step. Control thread only; allocates.
	 */
	auto apply(int index, Rack& rack) -> std::vector<RackMacroWrite>;

	//! Drops every macro.
	void clear();

	// --- persistence (children of the channel's <rack> element) ---

	//! Writes one <macro> element per macro, or nothing when there are none.
	void saveSettings(QDomDocument& doc, QDomElement& rackElement) const;
	//! Reads the <macro> children of @a rackElement; missing ones leave none.
	void loadSettings(const QDomElement& rackElement);

private:
	std::vector<RackMacro> m_macros;
};

} // namespace lmms

#endif // LMMS_RACK_MACROS_H
