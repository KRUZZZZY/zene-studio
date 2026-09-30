/*
 * Vst3NativeEditor.cpp - R4.4: the window a VST3 plug-in's own editor opens in
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

#include "Vst3NativeEditor.h"

#include <functional>

#include <QCloseEvent>
#include <QWidget>

#include "Vst3Host.h"

namespace lmms::vst3
{

namespace
{

//! The window: a close from the window manager closes the editor first.
class EditorWindow : public QWidget
{
public:
	explicit EditorWindow(std::function<void()> onClose) : m_onClose(std::move(onClose)) {}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		if (m_onClose) { m_onClose(); }
		QWidget::closeEvent(event);
	}

private:
	std::function<void()> m_onClose;
};

} // namespace


Vst3NativeEditor::~Vst3NativeEditor()
{
	close();
}


bool Vst3NativeEditor::open(HostedPlugin& plugin, const QString& title, QString* error)
{
	if (isOpen())
	{
		m_window->raise();
		m_window->activateWindow();
		return true;
	}
	auto* window = new EditorWindow([this] {
		if (m_plugin != nullptr) { m_plugin->closeEditor(); }
		if (m_window != nullptr) { m_window->deleteLater(); }
	});
	window->setWindowTitle(title);
	window->setAttribute(Qt::WA_NativeWindow);
	const WId id = window->winId();
	if (!plugin.openEditor(reinterpret_cast<void*>(id), error))
	{
		delete window;
		return false;
	}
	int width = 0;
	int height = 0;
	plugin.editorSize(&width, &height);
	if (width > 0 && height > 0) { window->resize(width, height); }
	m_plugin = &plugin;
	m_window = window;
	window->show();
	return true;
}


void Vst3NativeEditor::close()
{
	if (m_plugin != nullptr) { m_plugin->closeEditor(); }
	if (!m_window.isNull()) { delete m_window.data(); }
	m_plugin = nullptr;
}

} // namespace lmms::vst3
