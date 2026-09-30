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
 *  the GUI"): is_api_supported(x11, embedded) -> create -> set_scale -> get_size -> set_parent
 *  -> show, and on close hide -> destroy. Embedded only: a plug-in that can only float is
 *  refused with that reason rather than given a window this host does not manage. The host
 *  half (clap.gui: request_resize, request_show/hide, closed) is in ClapHostInternals.h.
 */

#include "ClapHost.h"
#include "ClapHostInternals.h"

#include <clap/clap.h>

namespace lmms::clap
{

auto HostedPlugin::openEditor(void* parentWindow, QString* error) -> bool
{
	auto& impl = *m_impl;
	if (impl.guiCreated) { return true; }
	const auto* gui = impl.guiExt();
	if (gui == nullptr)
	{
		*error = QStringLiteral("the plug-in has no editor (no clap.gui extension)");
		return false;
	}
	if (!gui->is_api_supported(impl.plugin, CLAP_WINDOW_API_X11, false))
	{
		*error = QStringLiteral("the plug-in's editor cannot embed in an X11 window");
		return false;
	}
	if (!gui->create(impl.plugin, CLAP_WINDOW_API_X11, false))
	{
		*error = QStringLiteral("the plug-in refused to create its editor");
		return false;
	}
	impl.guiCreated = true;
	impl.guiClosedByPlugin = false;
	gui->set_scale(impl.plugin, 1.0);
	clap_window_t window{};
	window.api = CLAP_WINDOW_API_X11;
	window.x11 = static_cast<clap_xwnd>(reinterpret_cast<std::uintptr_t>(parentWindow));
	if (!gui->set_parent(impl.plugin, &window) || !gui->show(impl.plugin))
	{
		closeEditor();
		*error = QStringLiteral("the plug-in's editor refused to attach");
		return false;
	}
	return true;
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
