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

#include <QString>

namespace Steinberg::Vst { class IEditController; }
namespace Steinberg { class IPlugView; }

namespace lmms::vst3
{

class HostPlugFrame;

/*! One open editor of one plug-in (relief plan R4.2). open() asks the controller for its
 *  "editor" view, requires X11 embedding, hands the view a frame - which answers
 *  IPlugFrame::resizeView and, through queryInterface, Linux::IRunLoop on the host's
 *  PluginEditorHost (R4.1), so the editor's X11 descriptor and repaint timers run on the Qt
 *  loop - and attaches it to @a parentWindow (an X11 window id). close() detaches in the
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

	//! Timers the editor registered through the run loop, and the onTimer calls delivered.
	int runLoopTimers() const;
	std::uint32_t timerDeliveries() const;

private:
	Steinberg::Vst::IEditController* m_controller;
	Steinberg::IPlugView* m_view = nullptr;
	HostPlugFrame* m_frame = nullptr;
};

} // namespace lmms::vst3

#endif // LMMS_VST3_EDITOR_SESSION_H
