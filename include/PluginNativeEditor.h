/*
 * PluginNativeEditor.h - R4.4: the window a plug-in's own editor opens in
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

#ifndef LMMS_GUI_PLUGIN_NATIVE_EDITOR_H
#define LMMS_GUI_PLUGIN_NATIVE_EDITOR_H

#include <functional>

#include <QPointer>
#include <QString>

#include "lmms_export.h"

class QWidget;

namespace lmms
{
class Effect;
class EffectChain;
class Instrument;
}

namespace lmms::gui
{

/*! One plug-in's editor WINDOW, for every format: a native top-level window whose id the
 *  plug-in's view is attached to, sized to the view. The format supplies three hooks - attach
 *  to a parent window id, detach, report the view's size (VST3: Vst3EditorSession; CLAP: the
 *  host's gui extension). Closing the window detaches the editor; closing the editor deletes
 *  the window. Main thread. */
class LMMS_EXPORT PluginNativeEditor
{
public:
	struct Hooks
	{
		std::function<bool(void* parentWindow, QString* error)> attach;
		std::function<void()> detach;
		std::function<void(int* width, int* height)> size;
	};

	~PluginNativeEditor();
	bool open(Hooks hooks, const QString& title, QString* error);
	void close();
	bool isOpen() const { return !m_window.isNull(); }

private:
	Hooks m_hooks;
	QPointer<QWidget> m_window;
};

/*! R4.4's button, for an instrument's generated control view: "Show plugin editor" runs
 *  plugin.editor_open for @a instrument's own track through the registry, and a refusal (no
 *  editor, or one that can only float) is shown in place of the button's hint. One helper, so
 *  every format's view offers the same control. */
LMMS_EXPORT QWidget* makeNativeEditorButton(Instrument* instrument, QWidget* parent);

/*! The same button for an effect's control dialog: it names the effect as plugin.editor_open
 *  does - the track or mixer channel whose chain holds it, and its stable fx-<n> - at the
 *  moment it is clicked, so a chain edited since the dialog opened is still addressed right. */
LMMS_EXPORT QWidget* makeNativeEditorButton(Effect* effect, QWidget* parent);

//! The plugin.* / oop.* target id (trk-<n> or ch-<n>) of the track or mixer channel that owns
//! @a chain; empty if none does.
LMMS_EXPORT QString targetIdOfChain(const EffectChain* chain);

} // namespace lmms::gui

#endif // LMMS_GUI_PLUGIN_NATIVE_EDITOR_H
