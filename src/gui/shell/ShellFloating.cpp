/*
 * ShellFloating.cpp - a shell panel in a window of its own (a second monitor's)
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

#include "Shell.h"

#include <QEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "ShellIcons.h"
#include "ShellPanel.h"

namespace lmms::gui
{

namespace
{

//! Closing a panel's own window puts the panel back in the shell instead of deleting it with
//! the window (the window owns the frame while it floats).
class FloatCloser : public QObject
{
public:
	FloatCloser(Shell* shell, QString id, QObject* parent) : QObject(parent), m_shell(shell), m_id(std::move(id)) {}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (event->type() != QEvent::Close) { return QObject::eventFilter(watched, event); }
		m_shell->attachPanel(m_id);
		return true;
	}

private:
	Shell* m_shell;
	QString m_id;
};

} // namespace

bool Shell::detachPanel(const QString& id, QString* reason)
{
	ShellPanel* frame = m_panels.value(id);
	if (frame == nullptr)
	{
		if (reason != nullptr) { *reason = tr("there is no panel '%1'").arg(id); }
		return false;
	}
	if (m_floating.contains(id)) { return true; }
	auto* window = new QWidget(this, Qt::Window);
	window->setObjectName(QStringLiteral("floatingPanel_") + id);
	window->setWindowTitle(ShellLayout::spec(id).title);
	auto* box = new QVBoxLayout(window);
	box->setContentsMargins(0, 0, 0, 0);
	m_floating.insert(id, window);
	frame->setParent(window);
	box->addWidget(frame);
	frame->show();
	window->installEventFilter(new FloatCloser(this, id, window));
	window->resize(frame->size().expandedTo(QSize(480, 320)));
	window->show();
	apply();
	return true;
}

bool Shell::attachPanel(const QString& id)
{
	const QPointer<QWidget> window = m_floating.take(id);
	if (window.isNull()) { return false; }
	ShellPanel* frame = m_panels.value(id);
	frame->hide();
	frame->setParent(m_parking);
	window->hide();
	window->deleteLater();
	apply();
	return true;
}

void Shell::addFloatButton(ShellPanel* frame)
{
	auto* button = new QToolButton(frame);
	button->setObjectName(QStringLiteral("panelFloat"));
	button->setIcon(shellicons::icon(QStringLiteral("float")));
	button->setAutoRaise(true);
	button->setToolTip(tr("Show in its own window"));
	button->setAccessibleName(tr("Pop out the %1 panel").arg(ShellLayout::spec(frame->id()).title));
	button->setProperty("controlCommand", QStringLiteral("window.detach"));
	connect(button, &QToolButton::clicked, this, [this, id = frame->id()] {
		ControlRegistry::instance()->invoke(isDetached(id) ? QStringLiteral("window.attach") : QStringLiteral("window.detach"),
			{{QStringLiteral("panel"), id}});
	});
	frame->toolArea()->addWidget(button);
}

} // namespace lmms::gui
