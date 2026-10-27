/*
 * ClapHostGui.cpp - R4.3: a CLAP plug-in's own editor, embedded in a host window
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

/*! The clap.gui life cycle the extension's own header prescribes (clap/ext/gui.h, "Showing
 *  the GUI"): is_api_supported(api, embedded) -> create -> set_scale -> get_size -> set_parent
 *  -> show, and on close hide -> destroy. The api is the platform's (Q050-490): x11, win32 or
 *  cocoa, read when the editor opens; on cocoa set_scale is not called, as the header says
 *  (cocoa sizes are logical: the OS applies the screen's factor). Embedded only: a plug-in that
 *  can only float is refused with that reason rather than given a window this host does not manage. The host
 *  half (clap.gui: request_resize, request_show/hide, closed) is in ClapHostInternals.h.
 */

#include "ClapHost.h"
#include "ClapHostInternals.h"

#include <clap/clap.h>

#include <cmath>

namespace lmms::clap
{

namespace
{
//! Q050-490: the clap.gui window API of each window system (nullptr: none to embed in).
const char* windowApiOf(PluginWindowSystem system)
{
	switch (system)
	{
	case PluginWindowSystem::X11: return CLAP_WINDOW_API_X11;
	case PluginWindowSystem::Win32: return CLAP_WINDOW_API_WIN32;
	case PluginWindowSystem::Cocoa: return CLAP_WINDOW_API_COCOA;
	case PluginWindowSystem::None: break;
	}
	return nullptr;
}

//! The api the plug-in's editor embeds in on @a system, or nullptr with the refusal in @a error.
const char* embeddableApi(const clap_plugin_gui_t* gui, const clap_plugin_t* plugin, PluginWindowSystem system,
	QString* error)
{
	const char* api = windowApiOf(system);
	if (api == nullptr)
	{
		*error = noPluginWindowSystemError();
		return nullptr;
	}
	if (!gui->is_api_supported(plugin, api, false))
	{
		*error = QStringLiteral("the plug-in's editor cannot embed on %1 (CLAP window API %2)")
			.arg(pluginWindowSystemName(system), QLatin1String(api));
		return nullptr;
	}
	return api;
}

//! MX-094: the device's zoom; a plug-in that cannot scale refuses it and keeps its own size. Never on cocoa: its
//! sizes are logical and clap/ext/gui.h says not to call set_scale there.
void askForScale(const clap_plugin_gui_t* gui, const clap_plugin_t* plugin, PluginWindowSystem system, double scale)
{
	if (system != PluginWindowSystem::Cocoa) { gui->set_scale(plugin, scale); }
}

//! @a parent (QWidget::winId()) in the union member of @a system's api.
clap_window_t parentWindowOf(PluginWindowSystem system, const char* api, void* parent)
{
	clap_window_t window{};
	window.api = api;
	switch (system)
	{
	case PluginWindowSystem::Win32: window.win32 = parent; break;
	case PluginWindowSystem::Cocoa: window.cocoa = parent; break;
	default: window.x11 = static_cast<clap_xwnd>(reinterpret_cast<std::uintptr_t>(parent)); break;
	}
	return window;
}
} // namespace

auto HostedPlugin::openEditor(void* parentWindow, QString* error) -> bool
{
	auto& impl = *m_impl;
	const auto* gui = impl.guiExt();
	if (impl.guiCreated)
	{
		// The plug-in closed its own editor but did not destroy it: the GUI is alive, so a reopen
		// shows it again rather than reporting success while nothing is on screen. (When the
		// plug-in destroyed it, guiClosed() cleared guiCreated and the ladder below creates a new one.)
		if (!impl.guiClosedByPlugin) { return true; }
		if (gui == nullptr || !gui->show(impl.plugin))
		{
			*error = QStringLiteral("the plug-in's editor refused to show again");
			return false;
		}
		impl.guiClosedByPlugin = false;
		return true;
	}
	if (gui == nullptr)
	{
		*error = QStringLiteral("the plug-in has no editor (no clap.gui extension)");
		return false;
	}
	const PluginWindowSystem system = impl.editorWindowSystem.value_or(currentPluginWindowSystem());
	const char* api = embeddableApi(gui, impl.plugin, system, error);
	if (api == nullptr) { return false; }
	if (!gui->create(impl.plugin, api, false))
	{
		*error = QStringLiteral("the plug-in refused to create its editor");
		return false;
	}
	impl.guiCreated = true;
	impl.guiClosedByPlugin = false;
	askForScale(gui, impl.plugin, system, impl.editorScale);
	const clap_window_t window = parentWindowOf(system, api, parentWindow);
	if (!gui->set_parent(impl.plugin, &window) || !gui->show(impl.plugin))
	{
		closeEditor();
		*error = QStringLiteral("the plug-in's editor refused to attach");
		return false;
	}
	return true;
}

void HostedPlugin::setEditorWindowSystem(PluginWindowSystem system)
{
	m_impl->editorWindowSystem = system;
}

void HostedPlugin::setEditorScale(double scale)
{
	m_impl->editorScale = std::isfinite(scale) && scale > 0.0 ? scale : 1.0;
}

void HostedPlugin::closeEditor()
{
	auto& impl = *m_impl;
	if (!impl.guiCreated) { return; }
	if (const auto* gui = impl.guiExt(); gui != nullptr)
	{
		gui->hide(impl.plugin);
		gui->destroy(impl.plugin);
	}
	impl.guiCreated = false;
}

auto HostedPlugin::editorOpen() const -> bool
{
	return m_impl->guiCreated && !m_impl->guiClosedByPlugin;
}

auto HostedPlugin::resizeEditor(int width, int height) -> bool
{
	auto& impl = *m_impl;
	const auto* gui = impl.guiExt();
	if (!impl.guiCreated || gui == nullptr || !gui->can_resize(impl.plugin) || width <= 0 || height <= 0)
	{
		return false;
	}
	auto w = static_cast<std::uint32_t>(width);
	auto h = static_cast<std::uint32_t>(height);
	gui->adjust_size(impl.plugin, &w, &h);
	return gui->set_size(impl.plugin, w, h);
}

void HostedPlugin::editorSize(int* width, int* height) const
{
	std::uint32_t w = 0;
	std::uint32_t h = 0;
	const auto* gui = m_impl->guiExt();
	if (!m_impl->guiCreated || gui == nullptr || !gui->get_size(m_impl->plugin, &w, &h)) { w = 0; h = 0; }
	*width = static_cast<int>(w);
	*height = static_cast<int>(h);
}

} // namespace lmms::clap
