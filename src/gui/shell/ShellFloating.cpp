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

#include <QAction>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "MainWindow.h"
#include "ShellIcons.h"
#include "ShellPanel.h"
#include "ShellTearOff.h"

namespace lmms::gui
{

namespace
{

const QString kLauncher = QStringLiteral("launcher");

//! A floating panel's window: closing it from its title bar docks the panel instead of deleting
//! it with the window (the window owns the frame while it floats); a move or a resize is saved
//! with the layout; a move that follows the pointer is a title-bar drag (noteFloatingMoved).
class FloatWatcher : public QObject
{
public:
	FloatWatcher(Shell* shell, QString id, QObject* parent) : QObject(parent), m_shell(shell), m_id(std::move(id)) {}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (event->type() == QEvent::Close) { return onClose(static_cast<QWidget*>(watched), event); }
		if (event->type() == QEvent::Move) { noteMove(static_cast<QWidget*>(watched)); }
		if (event->type() == QEvent::Move || event->type() == QEvent::Resize) { m_shell->schedulePersist(); }
		return QObject::eventFilter(watched, event);
	}

private:
	bool onClose(QWidget* window, QEvent* event)
	{
		if (event->spontaneous())
		{
			m_shell->attachPanel(m_id);
			return true;
		}
		// A program closing every window (File > Quit) hides this one and leaves the panel floating,
		// so the saved layout still lists it; a quit the user then cancels brings the window back.
		QPointer<QWidget> guarded(window);
		QPointer<Shell> shell(m_shell);
		QTimer::singleShot(0, m_shell, [guarded, shell]() {
			if (!guarded.isNull() && !shell.isNull() && shell->window()->isVisible()) { guarded->show(); }
		});
		return false;
	}

	void noteMove(const QWidget* window)
	{
		// The window moved and kept its place under the pointer: someone is dragging it by its title
		// bar. A program's move (a restore, Window > Tile) leaves the pointer behind.
		const QPoint offset = QCursor::pos() - window->pos();
		const bool following = window->pos() != m_lastPos && (offset - m_lastOffset).manhattanLength() <= 4;
		m_lastOffset = offset;
		m_lastPos = window->pos();
		if (following) { noteFloatingMoved(m_shell, m_id); }
	}

	Shell* m_shell;
	QString m_id;
	QPoint m_lastOffset{-100000, -100000};
	QPoint m_lastPos;
};

QRect rectIn(const QWidget* ancestor, const QWidget* widget)
{
	return {widget->mapTo(ancestor, QPoint(0, 0)), widget->size()};
}

//! A closed side region's place: the band along its edge of @a area, the width it opens at.
QRect edgeBand(const QRect& area, ShellRegion region)
{
	switch (region)
	{
	case ShellRegion::Left: return {area.left(), area.top(), tokens::size::inspectorWidth, area.height()};
	case ShellRegion::Right:
		return {area.right() - tokens::size::browserWidth + 1, area.top(), tokens::size::browserWidth, area.height()};
	case ShellRegion::Bottom:
		return {area.left(), area.bottom() - tokens::size::bottomHeight + 1, area.width(), tokens::size::bottomHeight};
	case ShellRegion::Center: break;
	}
	return area;
}

//! @a geometry made to fit @a area: no larger than it, and on it (MainWindow's clamp).
QRect fitInto(QRect geometry, const QRect& area)
{
	if (area.isEmpty()) { return geometry; }
	geometry.setSize(geometry.size().boundedTo(area.size()));
	return MainWindow::clampToVisibleArea(geometry, area);
}

QScreen* screenNamed(const QString& name)
{
	for (QScreen* screen : QGuiApplication::screens())
	{
		if (screen->name() == name) { return screen; }
	}
	return QGuiApplication::primaryScreen();
}

QString hintText(const QString& title, const QString& displaced, int mode)
{
	switch (mode)
	{
	case ShellDockHint::Hint: return Shell::tr("Drop here to dock %1").arg(title);
	case ShellDockHint::Armed:
		return displaced.isEmpty() ? Shell::tr("Release to dock %1").arg(title)
			: Shell::tr("Release to dock %1 in place of %2").arg(title, displaced);
	case ShellDockHint::Teach:
		return Shell::tr("To dock %1, drag it by its panel header and drop it here - or click Dock").arg(title);
	default: return {};
	}
}

//! What the centre shows while the view's main panel floats: where it went, and the way back.
QWidget* makePlaceholder(Shell* shell, QWidget* host)
{
	auto* box = new QWidget(host);
	box->setObjectName(QStringLiteral("shellFloatingPlaceholder"));
	auto* column = new QVBoxLayout(box);
	column->addStretch(1);
	auto* text = new QLabel(box);
	text->setAlignment(Qt::AlignCenter);
	text->setWordWrap(true);
	column->addWidget(text);
	auto* row = new QHBoxLayout();
	row->addStretch(1);
	auto* show = new QPushButton(Shell::tr("Show Its Window"), box);
	show->setProperty("controlCommand", QStringLiteral("window.toggle"));
	auto* dock = new QPushButton(Shell::tr("Dock It Here"), box);
	dock->setProperty("controlCommand", QStringLiteral("window.attach"));
	row->addWidget(show);
	row->addWidget(dock);
	row->addStretch(1);
	column->addLayout(row);
	column->addStretch(1);
	QObject::connect(show, &QPushButton::clicked, shell, [shell]() {
		dispatchWindowToggle(ShellLayout::centerOf(shell->model().view()));
	});
	QObject::connect(dock, &QPushButton::clicked, shell, [shell]() {
		shell->dispatchFloat(ShellLayout::centerOf(shell->model().view()), false);
	});
	host->layout()->addWidget(box);
	box->hide();
	return box;
}

} // namespace

QSize Shell::floatSize(const QWidget* frame)
{
	return frame->size().expandedTo(QSize(480, 320)).boundedTo(QSize(1200, 800));
}

void Shell::buildFloating()
{
	m_persistTimer = new QTimer(this);
	m_persistTimer->setSingleShot(true);
	m_persistTimer->setInterval(300);
	connect(m_persistTimer, &QTimer::timeout, this, [this]() { persist(); });
	m_dockHint = new ShellDockHint(this);
	m_centrePlaceholder = makePlaceholder(this, m_hosts.value(static_cast<int>(ShellRegion::Center)));
	m_floatAction = new QAction(tr("Float or Dock Panel"), this);
	m_floatAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
	m_floatAction->setToolTip(tr("Show the focused panel in a window of its own, or dock it back"));
	m_floatAction->setProperty("controlCommand", QStringLiteral("window.detach"));
	connect(m_floatAction, &QAction::triggered, this, [this]() {
		// The focused panel when it is on screen; else the view's main panel.
		const QString focused = m_layout.focused();
		const bool shown = !focused.isEmpty() && (m_layout.isVisible(focused) || isDetached(focused));
		const QString id = shown ? focused : ShellLayout::centerOf(m_layout.view());
		dispatchFloat(id, !isDetached(id));
	});
	addAction(m_floatAction);
}

void Shell::dispatchFloat(const QString& id, bool floating)
{
	const MainWindow* main = controlMainWindow();
	if (main == nullptr || main->shell() != this)
	{
		if (floating) { detachPanel(id); }
		else { attachPanel(id); }
		return;
	}
	ControlRegistry::instance()->invoke(floating ? QStringLiteral("window.detach") : QStringLiteral("window.attach"),
		{{QStringLiteral("panel"), id}});
}

void Shell::setNextFloatGeometry(const QString& id, const QRect& geometry) { m_nextFloatGeometry.insert(id, geometry); }

void Shell::schedulePersist() { m_persistTimer->start(); }

bool Shell::detachPanel(const QString& id, QString* reason)
{
	ShellPanel* frame = m_panels.value(id);
	if (frame == nullptr)
	{
		if (reason != nullptr) { *reason = tr("there is no panel '%1'").arg(id); }
		return false;
	}
	if (m_floating.contains(id)) { return true; }
	// It opens where it was (or where a drag dropped it): a pop-out never sends the panel far from the eye.
	const QRect asked = m_nextFloatGeometry.take(id);
	const QPoint at = frame->isVisible() ? frame->mapToGlobal(QPoint(0, 0)) : window()->geometry().center() - QPoint(240, 160);
	auto* own = new QWidget(this, Qt::Window);
	own->setObjectName(QStringLiteral("floatingPanel_") + id);
	own->setWindowTitle(tr("%1 - Zene Studio").arg(ShellLayout::spec(id).title));
	auto* box = new QVBoxLayout(own);
	box->setContentsMargins(0, 0, 0, 0);
	m_floating.insert(id, own);
	frame->setParent(own);
	box->addWidget(frame);
	frame->show();
	own->addAction(m_floatAction);  // the key docks it from its own window too
	own->installEventFilter(new FloatWatcher(this, id, own));
	own->setGeometry(asked.isValid() ? asked : QRect(at, floatSize(frame)));
	own->show();
	syncFloatButton(id);
	apply();
	return true;
}

bool Shell::attachPanel(const QString& id)
{
	if (!dockQuietly(id)) { return false; }
	// Docking shows the panel: in its home region (taking the place of the one there) or, for a
	// view's main panel, the centre it already owns.
	m_layout.open(id, nullptr);
	apply();
	if (m_layout.isVisible(id)) { focusPanel(id); }
	return true;
}

bool Shell::dockQuietly(const QString& id)
{
	const QPointer<QWidget> own = m_floating.take(id);
	if (own.isNull()) { return false; }
	ShellPanel* frame = m_panels.value(id);
	frame->hide();
	frame->setParent(m_parking);
	own->hide();
	own->deleteLater();
	syncFloatButton(id);
	return true;
}

bool Shell::raiseFloating(const QString& id)
{
	QWidget* own = floatingWindow(id);
	if (own == nullptr) { return false; }
	if (own->isMinimized()) { own->showNormal(); }
	own->show();
	own->raise();
	own->activateWindow();
	return true;
}

bool Shell::moveFloatingToScreen(const QString& id, int index)
{
	QWidget* own = floatingWindow(id);
	const QList<QScreen*> screens = QGuiApplication::screens();
	if (own == nullptr || index < 0 || index >= screens.size()) { return false; }
	const QRect from = own->screen() != nullptr ? own->screen()->availableGeometry() : QRect();
	const QRect to = screens.at(index)->availableGeometry();
	QRect geometry = own->geometry();
	geometry.moveTopLeft(to.topLeft() + (from.isValid() ? geometry.topLeft() - from.topLeft() : QPoint(24, 24)));
	own->setGeometry(fitInto(geometry, to));
	return true;
}

QRect Shell::dockZone(const QString& id) const
{
	const ShellView view = m_layout.view();
	if (!ShellLayout::offers(view, id)) { return {}; }
	if (id == ShellLayout::centerOf(view)) { return rectIn(this, m_hosts.value(static_cast<int>(ShellRegion::Center))); }
	if (id == kLauncher)
	{
		QRect beside = rectIn(this, m_centre);
		beside.setWidth(beside.width() / 3);
		return beside;
	}
	const ShellRegion home = ShellLayout::spec(id).home;
	const QWidget* host = m_hosts.value(static_cast<int>(home));
	if (host->isVisible()) { return rectIn(this, host); }
	return edgeBand(rectIn(this, home == ShellRegion::Bottom ? m_rows : m_columns), home);
}

QString Shell::displacedBy(const QString& id) const
{
	const ShellView view = m_layout.view();
	if (!ShellLayout::offers(view, id) || id == ShellLayout::centerOf(view) || id == kLauncher) { return {}; }
	const QString there = m_layout.openIn(ShellLayout::spec(id).home);
	return there == id || isDetached(there) ? QString() : there;
}

void Shell::showDockHint(const QString& id, int mode)
{
	const QRect zone = id.isEmpty() ? QRect() : dockZone(id);
	if (zone.isEmpty() || mode == ShellDockHint::Hidden)
	{
		m_dockHint->clear();
		return;
	}
	const QString displaced = displacedBy(id);
	const QString text = hintText(ShellLayout::spec(id).title, displaced.isEmpty() ? QString() : ShellLayout::spec(displaced).title, mode);
	m_dockHint->showOver(zone, text, static_cast<ShellDockHint::Mode>(mode));
}

void Shell::syncCentrePlaceholder()
{
	const QString centre = ShellLayout::centerOf(m_layout.view());
	const bool away = isDetached(centre);
	if (away)
	{
		m_centrePlaceholder->findChild<QLabel*>()->setText(
			tr("%1 is in its own window. Drag its header back here, or dock it.").arg(ShellLayout::spec(centre).title));
	}
	m_centrePlaceholder->setVisible(away);
}

QJsonArray Shell::floatingSnapshot() const
{
	if (!m_pendingFloats.isEmpty()) { return m_pendingFloats; }
	QJsonArray out;
	for (const ShellPanelSpec& spec : ShellLayout::panels())
	{
		const QWidget* own = floatingWindow(spec.id);
		if (own == nullptr) { continue; }
		const QRect g = own->geometry();
		out.append(QJsonObject{{QStringLiteral("panel"), spec.id},
			{QStringLiteral("screen"), own->screen() != nullptr ? own->screen()->name() : QString()},
			{QStringLiteral("x"), g.x()}, {QStringLiteral("y"), g.y()},
			{QStringLiteral("width"), g.width()}, {QStringLiteral("height"), g.height()}});
	}
	return out;
}

void Shell::restoreFloating(const QJsonArray& floating)
{
	if (!window()->isVisible())
	{
		m_pendingFloats = floating;
		return;
	}
	QStringList listed;
	for (const QJsonValue& value : floating) { listed << value.toObject().value(QStringLiteral("panel")).toString(); }
	for (const QString& id : m_floating.keys())
	{
		if (!listed.contains(id)) { dockQuietly(id); }
	}
	for (const QJsonValue& value : floating) { floatOne(value.toObject()); }
	apply();
}

void Shell::floatOne(const QJsonObject& entry)
{
	const QString id = entry.value(QStringLiteral("panel")).toString();
	if (!ShellLayout::isPanel(id)) { return; }
	const QRect saved(entry.value(QStringLiteral("x")).toInt(), entry.value(QStringLiteral("y")).toInt(),
		entry.value(QStringLiteral("width")).toInt(), entry.value(QStringLiteral("height")).toInt());
	// Its screen when that is still connected, else the primary one: never a window off every screen.
	const QScreen* screen = screenNamed(entry.value(QStringLiteral("screen")).toString());
	const QRect where = saved.isEmpty() || screen == nullptr ? QRect() : fitInto(saved, screen->availableGeometry());
	if (QWidget* own = floatingWindow(id))
	{
		if (where.isValid()) { own->setGeometry(where); }
		return;
	}
	if (where.isValid()) { m_nextFloatGeometry.insert(id, where); }
	detachPanel(id);
}

void Shell::addFloatButton(ShellPanel* frame)
{
	new ShellTearOff(this, frame);  // first, so the button below keeps the arrow pointer
	auto* button = new QToolButton(frame);
	button->setObjectName(QStringLiteral("panelFloat"));
	button->setIcon(shellicons::icon(QStringLiteral("float")));
	button->setAutoRaise(true);
	connect(button, &QToolButton::clicked, this, [this, id = frame->id()] { dispatchFloat(id, !isDetached(id)); });
	frame->toolArea()->addWidget(button);
	syncFloatButton(frame->id());
}

void Shell::syncFloatButton(const QString& id)
{
	ShellPanel* frame = m_panels.value(id);
	auto* button = frame != nullptr ? frame->findChild<QToolButton*>(QStringLiteral("panelFloat")) : nullptr;
	if (button == nullptr) { return; }
	const bool floating = isDetached(id);
	const QString title = ShellLayout::spec(id).title;
	// Floating, the button says what it does in words: "Dock" is the way back, and it must be found.
	button->setToolButtonStyle(floating ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
	button->setText(floating ? tr("Dock") : QString());
	button->setToolTip(floating ? tr("Dock %1 back into the main window").arg(title)
		: tr("Float %1 in its own window - for a second monitor").arg(title));
	button->setAccessibleName(floating ? tr("Dock the %1 panel").arg(title) : tr("Pop out the %1 panel").arg(title));
	button->setProperty("controlCommand", floating ? QStringLiteral("window.attach") : QStringLiteral("window.detach"));
	frame->header()->setToolTip(floating
		? tr("Drag this header onto %1's place in the main window to dock it; double-click to dock").arg(title)
		: tr("Drag this header out to float %1 in its own window; double-click to float").arg(title));
}

} // namespace lmms::gui
