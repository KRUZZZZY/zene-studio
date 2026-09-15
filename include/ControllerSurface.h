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

//! One binding in a template: the MIDI address and a hint of what it controls.
struct ControllerTemplateBinding
{
	int channel = 1;        //!< 1-based MIDI channel
	int controller = 0;     //!< MIDI controller number
	QString targetName;     //!< The model's fullDisplayName() hint

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

	//! Where templates live: <userData>/controller-templates/
	static QString templateDirectory();

	//! Save the current project's MIDI bindings as a named template.
	bool saveTemplate(const QString& name);

	//! List saved template names.
	QStringList listTemplates() const;

	//! Read one template without applying it.
	ControllerTemplate loadTemplate(const QString& name) const;

	//! Apply a template: create MidiController + ControllerConnection for each
	//! binding whose target model can be resolved. Returns the count created.
	int applyTemplate(const QString& name);

	//! Remove a saved template file.
	bool removeTemplate(const QString& name);

	//! Enumerate every MidiController currently bound in the project.
	QVector<ControllerTemplateBinding> currentBindings() const;

	//! Resolve a target name to a live AutomatableModel, or nullptr.
	AutomatableModel* resolveTarget(const QString& targetName) const;

private:
	ControllerSurface() = default;

	QString templatePath(const QString& name) const;
};

} // namespace lmms

#endif // LMMS_CONTROLLER_SURFACE_H