/*
 * Vst3EditorSession.cpp - R4.2: a VST3 plug-in's own editor, attached to a host window
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

#include "Vst3EditorSession.h"
#include "Vst3Host.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <map>

#include "PluginEditorHost.h"
#include "PluginWindowSystem.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

namespace lmms::vst3
{

using namespace Steinberg;

namespace
{
//! Linux::IRunLoop's UID, spelled here: the SDK DEFINES the Linux interfaces' iids only when it
//! builds for Linux (public.sdk/source/common/commoniids.cpp, #if SMTG_OS_LINUX), so a reference to
//! Linux::IRunLoop::iid failed to link on macOS (hosted run 36691164314). A view on another platform
//! never asks for a run loop; the answer is the same bytes everywhere.
const TUID kRunLoopIid = INLINE_UID(0x18C35366, 0x97764F1A, 0x9C5B8385, 0x7A871389);

//! Q050-490: the IPlugView platform type of each window system (nullptr: none to embed in).
FIDString platformTypeOf(PluginWindowSystem system)
{
	switch (system)
	{
	case PluginWindowSystem::X11: return kPlatformTypeX11EmbedWindowID;
	case PluginWindowSystem::Win32: return kPlatformTypeHWND;
	case PluginWindowSystem::Cocoa: return kPlatformTypeNSView;
	case PluginWindowSystem::None: break;
	}
	return nullptr;
}
} // namespace

/*! The frame a view is given: IPlugFrame (the view asks to be resized) and Linux::IRunLoop
 *  (the view asks for its descriptors and timers to be run), both on PluginEditorHost. */
class HostPlugFrame : public IPlugFrame, public Linux::IRunLoop
{
public:
	virtual ~HostPlugFrame() = default;

	tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) SMTG_OVERRIDE
	{
		if (view == nullptr || newSize == nullptr) { return kInvalidArgument; }
		return view->onSize(newSize);
	}

	tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) SMTG_OVERRIDE
	{
		if (handler == nullptr || m_fds.count(handler) != 0) { return kInvalidArgument; }
		handler->addRef();
		if (!m_loop.registerFd(fd, PluginEditorHost::Read, [handler](int ready, unsigned) { handler->onFDIsSet(ready); }))
		{
			handler->release();
			return kResultFalse;
		}
		m_fds.emplace(handler, fd);
		return kResultOk;
	}

	tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) SMTG_OVERRIDE
	{
		const auto it = m_fds.find(handler);
		if (it == m_fds.end()) { return kInvalidArgument; }
		m_loop.unregisterFd(it->second);
		m_fds.erase(it);
		handler->release();
		return kResultOk;
	}

	tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval milliseconds) SMTG_OVERRIDE
	{
		if (handler == nullptr || m_timers.count(handler) != 0) { return kInvalidArgument; }
		handler->addRef();
		std::uint32_t id = 0;
		if (!m_loop.registerTimer(static_cast<std::uint32_t>(milliseconds), [this, handler](std::uint32_t) {
				handler->onTimer();
				m_deliveries.fetch_add(1, std::memory_order_relaxed);
			}, &id))
		{
			handler->release();
			return kResultFalse;
		}
		m_timers.emplace(handler, id);
		return kResultOk;
	}

	tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) SMTG_OVERRIDE
	{
		const auto it = m_timers.find(handler);
		if (it == m_timers.end()) { return kInvalidArgument; }
		m_loop.unregisterTimer(it->second);
		m_timers.erase(it);
		handler->release();
		return kResultOk;
	}

	//! An editor that forgets to unregister must not keep its handlers alive (or firing).
	void dropAll()
	{
		for (auto& [handler, fd] : m_fds) { m_loop.unregisterFd(fd); handler->release(); }
		for (auto& [handler, id] : m_timers) { m_loop.unregisterTimer(id); handler->release(); }
		m_fds.clear();
		m_timers.clear();
	}

	tresult PLUGIN_API queryInterface(const TUID iid, void** obj) SMTG_OVERRIDE
	{
		if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid))
		{
			*obj = static_cast<IPlugFrame*>(this);
		}
		else if (FUnknownPrivate::iidEqual(iid, kRunLoopIid))
		{
			*obj = static_cast<Linux::IRunLoop*>(this);
		}
		else
		{
			*obj = nullptr;
			return kNoInterface;
		}
		addRef();
		return kResultOk;
	}

	uint32 PLUGIN_API addRef() SMTG_OVERRIDE { return ++m_refCount; }

	uint32 PLUGIN_API release() SMTG_OVERRIDE
	{
		const auto count = --m_refCount;
		if (count == 0) { delete this; }
		return count;
	}

	int timers() const { return static_cast<int>(m_timers.size()); }
	std::uint32_t deliveries() const { return m_deliveries.load(std::memory_order_relaxed); }

private:
	PluginEditorHost m_loop;
	std::map<Linux::IEventHandler*, int> m_fds;
	std::map<Linux::ITimerHandler*, std::uint32_t> m_timers;
	std::atomic<std::uint32_t> m_deliveries{0};
	std::atomic<uint32> m_refCount{1};
};


Vst3EditorSession::Vst3EditorSession(Vst::IEditController* controller) :
	m_controller(controller)
{
}


Vst3EditorSession::~Vst3EditorSession()
{
	close();
}


bool Vst3EditorSession::open(void* parentWindow, QString* error)
{
	if (isOpen()) { return true; }
	if (m_controller == nullptr)
	{
		*error = QStringLiteral("the plug-in has no edit controller");
		return false;
	}
	// Q050-490: the platform's own window type - an X11 id, an HWND or an NSView, as winId() gives.
	const PluginWindowSystem system = m_windowSystem.value_or(currentPluginWindowSystem());
	const FIDString type = platformTypeOf(system);
	if (type == nullptr)
	{
		*error = noPluginWindowSystemError();
		return false;
	}
	IPlugView* view = m_controller->createView(Vst::ViewType::kEditor);
	if (view == nullptr)
	{
		*error = QStringLiteral("the plug-in offers no editor view (createView returned nothing)");
		return false;
	}
	if (view->isPlatformTypeSupported(type) != kResultTrue)
	{
		view->release();
		*error = QStringLiteral("the plug-in's editor cannot embed on %1 (VST3 platform type %2)")
			.arg(pluginWindowSystemName(system), QLatin1String(type));
		return false;
	}
	auto* frame = new HostPlugFrame;
	view->setFrame(frame);
	if (view->attached(parentWindow, type) != kResultOk)
	{
		view->setFrame(nullptr);
		frame->dropAll();
		frame->release();
		view->release();
		*error = QStringLiteral("the plug-in's editor refused to attach");
		return false;
	}
	m_view = view;
	m_frame = frame;
	applyScale(system);
	return true;
}


/*! 0.5.0 MX-094: the device's zoom, after the view is attached (the SDK allows before or after). A view
 *  that scales itself is told the factor and asks for its new size through the frame, as the SDK
 *  prescribes; any other view that can resize is given the scaled size, through its own constraint. A
 *  view that can do neither keeps its size. At 1 nothing is asked of the view, so an editor opens
 *  exactly as it did before the option existed. On macOS the SDK keeps IPlugViewContentScaleSupport
 *  out (an NSView's coordinates are logical: the OS applies the screen's factor itself), so there the
 *  zoom is only ever the window's (Q050-490). */
void Vst3EditorSession::applyScale(PluginWindowSystem system)
{
	m_scaling = EditorScaling::Unscaled;
	if (m_scale == 1.0) { return; }
	IPlugViewContentScaleSupport* scalable = nullptr;
	if (system != PluginWindowSystem::Cocoa && m_view->queryInterface(IPlugViewContentScaleSupport::iid, reinterpret_cast<void**>(&scalable)) == kResultOk
		&& scalable != nullptr)
	{
		const bool handled = scalable->setContentScaleFactor(static_cast<float>(m_scale)) == kResultTrue;
		scalable->release();
		if (handled)
		{
			m_scaling = EditorScaling::ByView;
			return;
		}
	}
	int width = 0;
	int height = 0;
	size(&width, &height);
	if (width > 0 && height > 0
		&& resize(static_cast<int>(std::lround(width * m_scale)), static_cast<int>(std::lround(height * m_scale))))
	{
		m_scaling = EditorScaling::ByWindow;
	}
}


void Vst3EditorSession::close()
{
	if (m_view == nullptr) { return; }
	// The SDK's order: removed() first (the view unregisters its handlers through the frame),
	// then the frame is taken away - and anything the view forgot is dropped here.
	m_view->removed();
	m_view->setFrame(nullptr);
	m_frame->dropAll();
	m_frame->release();
	m_view->release();
	m_view = nullptr;
	m_frame = nullptr;
	m_scaling = EditorScaling::Unscaled;
}


bool Vst3EditorSession::resize(int width, int height)
{
	if (m_view == nullptr || m_view->canResize() != kResultTrue) { return false; }
	ViewRect rect(0, 0, width, height);
	if (m_view->checkSizeConstraint(&rect) != kResultTrue) { return false; }
	return m_view->onSize(&rect) == kResultTrue;
}


void Vst3EditorSession::size(int* width, int* height) const
{
	ViewRect rect;
	if (m_view == nullptr || m_view->getSize(&rect) != kResultTrue) { *width = 0; *height = 0; return; }
	*width = rect.getWidth();
	*height = rect.getHeight();
}


int Vst3EditorSession::runLoopTimers() const
{
	return m_frame != nullptr ? m_frame->timers() : 0;
}


std::uint32_t Vst3EditorSession::timerDeliveries() const
{
	return m_frame != nullptr ? m_frame->deliveries() : 0;
}

// ---- the HostedPlugin wrappers (Vst3Host.h), here so Vst3Host.cpp does not grow ------------

auto HostedPlugin::openEditor(void* parentWindow, QString* error) -> bool
{
	if (m_editor == nullptr) { m_editor = std::make_unique<Vst3EditorSession>(editController()); }
	if (!m_editor->isOpen())
	{
		m_editor->setScale(m_editorScale);
		if (m_editorWindowSystem) { m_editor->setWindowSystem(*m_editorWindowSystem); }
	}
	return m_editor->open(parentWindow, error);
}

void HostedPlugin::setEditorWindowSystem(PluginWindowSystem system)
{
	m_editorWindowSystem = system;
}

void HostedPlugin::setEditorScale(double scale)
{
	m_editorScale = scale > 0.0 ? scale : 1.0;
}

auto HostedPlugin::editorScaling() const -> EditorScaling
{
	return m_editor != nullptr ? m_editor->scaling() : EditorScaling::Unscaled;
}

void HostedPlugin::closeEditor()
{
	if (m_editor != nullptr) { m_editor->close(); }
}

auto HostedPlugin::editorOpen() const -> bool
{
	return m_editor != nullptr && m_editor->isOpen();
}

auto HostedPlugin::resizeEditor(int width, int height) -> bool
{
	return m_editor != nullptr && m_editor->resize(width, height);
}

void HostedPlugin::editorSize(int* width, int* height) const
{
	if (m_editor == nullptr) { *width = 0; *height = 0; return; }
	m_editor->size(width, height);
}

auto HostedPlugin::editorTimerCount() const -> int
{
	return m_editor != nullptr ? m_editor->runLoopTimers() : 0;
}

auto HostedPlugin::editorTimerDeliveries() const -> std::uint32_t
{
	return m_editor != nullptr ? m_editor->timerDeliveries() : 0;
}

} // namespace lmms::vst3
