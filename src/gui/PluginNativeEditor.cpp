/*
 * PluginNativeEditor.cpp - R4.4: the window a plug-in's own editor opens in
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

#include "PluginNativeEditor.h"

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Instrument.h"
#include "InstrumentTrack.h"

#include <QCloseEvent>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace lmms::gui
{

namespace
{

//! The window: a close from the window manager detaches the editor first.
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


PluginNativeEditor::~PluginNativeEditor()
{
	close();
}


bool PluginNativeEditor::open(Hooks hooks, const QString& title, QString* error)
{
	if (isOpen())
	{
		m_window->raise();
		m_window->activateWindow();
		return true;
	}
	auto* window = new EditorWindow([this] {
		if (m_hooks.detach) { m_hooks.detach(); }
		m_hooks = Hooks{};
		if (m_window != nullptr) { m_window->deleteLater(); }
	});
	window->setWindowTitle(title);
	window->setAttribute(Qt::WA_NativeWindow);
	const WId id = window->winId();
	if (!hooks.attach || !hooks.attach(reinterpret_cast<void*>(id), error))
	{
		delete window;
		return false;
	}
	int width = 0;
	int height = 0;
	if (hooks.size) { hooks.size(&width, &height); }
	if (width > 0 && height > 0) { window->resize(width, height); }
	m_hooks = std::move(hooks);
	m_window = window;
	window->show();
	return true;
}


void PluginNativeEditor::close()
{
	if (m_hooks.detach) { m_hooks.detach(); }
	m_hooks = Hooks{};
	if (!m_window.isNull()) { delete m_window.data(); }
}

QWidget* makeNativeEditorButton(Instrument* instrument, QWidget* parent)
{
	auto* box = new QWidget(parent);
	auto* layout = new QVBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	auto* button = new QToolButton(box);
	button->setText(QObject::tr("Show plugin editor"));
	button->setAccessibleName(QObject::tr("Show the plug-in's own editor"));
	button->setProperty("controlCommand", QStringLiteral("plugin.editor_open"));
	auto* reason = new QLabel(box);
	reason->setWordWrap(true);
	reason->hide();
	layout->addWidget(button);
	layout->addWidget(reason);
	QObject::connect(button, &QToolButton::clicked, box, [instrument, reason] {
		const QString track = instrument->instrumentTrack() != nullptr
			? control::trackIdOf(instrument->instrumentTrack()) : QString();
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("plugin.editor_open"),
			{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), QStringLiteral("inst")}});
		reason->setText(result.ok ? QString() : result.errorMessage);
		reason->setVisible(!result.ok);
	});
	return box;
}

} // namespace lmms::gui
