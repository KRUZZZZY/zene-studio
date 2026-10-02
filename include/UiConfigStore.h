/*
 * UiConfigStore.h - interface state kept as JSON in the config file, written as one undo step
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
 *
 */

#ifndef LMMS_GUI_UI_CONFIG_STORE_H
#define LMMS_GUI_UI_CONFIG_STORE_H

#include <functional>

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms::gui::uistore
{

/*! The interface's own customisations - shortcut overrides, the header's buttons, saved
 *  layouts, macros - each kept as one JSON object under `ui/<key>` in the config file. They
 *  are not project state, so no journal checkpoint holds them; a write is instead ONE action
 *  step on the engine's undo stack that puts the previous object back (the settings.set
 *  precedent), so Ctrl+Z, control.undo and the transaction record agree. */

//! The object stored under ui/@a key; empty when there is none or it does not parse.
LMMS_EXPORT QJsonObject read(const QString& key);

//! What a write records as its inverse: the command to run and its arguments.
struct Inverse
{
	QString op;
	QJsonObject args;
};

/*! Store @a value under ui/@a key and save the config file; record one undo step that writes
 *  the previous object back (and a redo that writes @a value again), running @a apply after
 *  each so the live interface follows; run @a apply now. Returns @a result with the
 *  transaction attached (before = the previous object, inverse = @a inverse). */
LMMS_EXPORT ControlResult write(const QString& key, const QJsonObject& value, QJsonObject result,
	const Inverse& inverse, const std::function<void()>& apply);

} // namespace lmms::gui::uistore

#endif // LMMS_GUI_UI_CONFIG_STORE_H
