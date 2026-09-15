/*
 * ControllerSurface.h - mapping templates and controller-surface state for
 *                       MIDI controller surfaces (soft-takeover, LED feedback).
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_CONTROLLER_SURFACE_H
#define LMMS_CONTROLLER_SURFACE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "lmms_export.h"

namespace lmms
{

class AutomatableModel;
class MidiController;

/*! One binding in a mapping template: the MIDI address, the control it drives
 *  and the two per-binding flags a surface needs to be restored whole.
 *
 *  The target is stored by the model's fullDisplayName() - the same string
 *  MidiPort::setName() receives when MIDI learn builds a binding - so a
 *  template is readable as a mapping list and survives the project it was
 *  saved from. Applying one resolves each name against the OPEN project and
 *  reports what it could not resolve; a template is a saved mapping set, not a
 *  claim that the targets exist.
 */
struct ControllerTemplateBinding
{
	int channel = 1;        //!< 1-based MIDI channel
	int controller = 0;     //!< MIDI controller number
	QString targetName;     //!< The model's fullDisplayName() hint
	bool softTakeover = false; //!< restore soft-takeover for this binding
	bool feedback = false;     //!< restore LED/feedback for this binding

	QJsonObject toJson() const;
	static ControllerTemplateBinding fromJson(const QJsonObject& obj);
};

//! A saved mapping template: a named set of bindings.
struct ControllerTemplate
{
	QString name;
	QVector<ControllerTemplateBinding> bindings;

	QJsonObject toJson() const;
	static ControllerTemplate fromJson(const QJsonObject& obj);
};

//! The controller-surface template store and helpers.
class LMMS_EXPORT ControllerSurface
{
public:
	static ControllerSurface& instance();

	//! Where templates live: <userConfig>/controller-templates/
	static QString templateDirectory();

	//! The file one template name maps to. Public because controller.*
	//! reports it, and because the name-to-file rule (one bare file name,
	//! refused when it carries a separator or a "..") has to be checkable
	//! from outside this class.
	QString templatePathFor(const QString& name) const;

	//! Save the current project's MIDI bindings as a named template.
	bool saveTemplate(const QString& name);

	//! List saved template names, sorted.
	QStringList listTemplates() const;

	//! Read one template without applying it.
	ControllerTemplate loadTemplate(const QString& name) const;

	/*! Apply a template: create MidiController + ControllerConnection for each
	 *  binding whose target model can be resolved, and restore that binding's
	 *  soft-takeover and feedback flags. Returns the count bound; the ones it
	 *  could not resolve are simply not bound (controller.template_apply
	 *  reports them).
	 */
	int applyTemplate(const QString& name);

	//! Remove a saved template file.
	bool removeTemplate(const QString& name);

	//! Enumerate every MidiController currently bound in the project, with the
	//! fullDisplayName() of the model it drives.
	QVector<ControllerTemplateBinding> currentBindings() const;

	//! Resolve a target name to a live AutomatableModel, or nullptr.
	AutomatableModel* resolveTarget(const QString& targetName) const;

private:
	ControllerSurface() = default;

	//! The model a connection drives, found by the connection's identity in the
	//! song's object tree (a display name is not unique).
	static AutomatableModel* modelOfConnection(const class ControllerConnection* connection);
};

} // namespace lmms

#endif // LMMS_CONTROLLER_SURFACE_H
