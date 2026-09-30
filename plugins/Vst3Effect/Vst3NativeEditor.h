/*
 * Vst3NativeEditor.h - R4.4: the window a VST3 plug-in's own editor opens in
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

#ifndef LMMS_VST3_NATIVE_EDITOR_H
#define LMMS_VST3_NATIVE_EDITOR_H

#include <QPointer>
#include <QString>

class QWidget;

namespace lmms::vst3
{

class HostedPlugin;

/*! One plug-in's editor WINDOW: a native top-level window whose X11 id the plug-in's view is
 *  attached to (Vst3EditorSession), sized to the view. Closing the window closes the editor;
 *  closing the editor deletes the window. Shared by the effect and the instrument. Main
 *  thread. */
class Vst3NativeEditor
{
public:
	~Vst3NativeEditor();
	bool open(HostedPlugin& plugin, const QString& title, QString* error);
	void close();
	bool isOpen() const { return !m_window.isNull(); }

private:
	HostedPlugin* m_plugin = nullptr;
	QPointer<QWidget> m_window;
};

} // namespace lmms::vst3

#endif // LMMS_VST3_NATIVE_EDITOR_H
