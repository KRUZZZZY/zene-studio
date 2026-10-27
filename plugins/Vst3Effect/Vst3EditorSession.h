/*
 * Vst3EditorSession.h - R4.2: a VST3 plug-in's own editor, attached to a host window
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

#ifndef LMMS_VST3_EDITOR_SESSION_H
#define LMMS_VST3_EDITOR_SESSION_H

#include <cstdint>
#include <memory>
#include <optional>

#include <QString>

#include "PluginWindowSystem.h"
#include "Vst3Host.h"

namespace Steinberg::Vst { class IEditController; }
namespace Steinberg { class IPlugView; }

namespace lmms::vst3
{

class HostPlugFrame;

/*! One open editor of one plug-in (relief plan R4.2). open() asks the controller for its
 *  "editor" view, requires the platform's window type (Q050-490: X11EmbedWindowID, HWND or
 *  NSView, read at open), hands the view a frame - which answers IPlugFrame::resizeView and,
 *  through queryInterface, Linux::IRunLoop on the host's PluginEditorHost (R4.1), so an X11
 *  editor's descriptor and repaint timers run on the Qt loop - and attaches it to
 *  @a parentWindow (the native window: an X11 id, an HWND, an NSView*). close() detaches in the
 *  order the SDK specifies (removed, then setFrame(nullptr)) and releases both. Main thread.
 *
 *  The knob grid stays the fallback and the agent-visible parameter view: an editor is an
 *  addition beside it, never a replacement. */
class Vst3EditorSession
{
public:
	explicit Vst3EditorSession(Steinberg::Vst::IEditController* controller);
	~Vst3EditorSession();

	Vst3EditorSession(const Vst3EditorSession&) = delete;
	Vst3EditorSession& operator=(const Vst3EditorSession&) = delete;

	bool open(void* parentWindow, QString* error);
	void close();
	bool isOpen() const { return m_view != nullptr; }

	//! Ask the view for a new size; false when it cannot resize or refused the size.
	bool resize(int width, int height);
	//! The view's current size, in pixels (0 x 0 when closed).
	void size(int* width, int* height) const;

	//! 0.5.0 MX-094: the zoom the next open() applies (1: none), and how the open view took it.
	void setScale(double scale) { m_scale = scale > 0.0 ? scale : 1.0; }
	EditorScaling scaling() const { return m_scaling; }

	//! Q050-490: the window system the next open() embeds in, instead of the running one (tests).
	void setWindowSystem(PluginWindowSystem system) { m_windowSystem = system; }

	//! Timers the editor registered through the run loop, and the onTimer calls delivered.
	int runLoopTimers() const;
	std::uint32_t timerDeliveries() const;

private:
	void applyScale(PluginWindowSystem system);

	Steinberg::Vst::IEditController* m_controller;
	Steinberg::IPlugView* m_view = nullptr;
	HostPlugFrame* m_frame = nullptr;
	double m_scale = 1.0;
	EditorScaling m_scaling = EditorScaling::Unscaled;
	std::optional<PluginWindowSystem> m_windowSystem;
};

} // namespace lmms::vst3

#endif // LMMS_VST3_EDITOR_SESSION_H
