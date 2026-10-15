/*
 * ShellTearOff.cpp - drag a shell panel's header out to float it, and back onto its dock zone
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

#include "ShellTearOff.h"

#include <utility>

#include <QAbstractButton>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include "DeprecationHelper.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Shell.h"
#include "ShellPanel.h"

namespace lmms::gui
{

namespace
{

//! Set on a floating window while its header moves it, so the title-bar watcher keeps quiet.
const char* const kHeaderMove = "shellHeaderMove";

//! Wayland gives a program neither its windows' positions nor the pointer's outside them.
bool onWayland() { return QGuiApplication::platformName().startsWith(QLatin1String("wayland")); }

QRect globalRect(const QWidget* widget, const QRect& local)
{
	return {widget->mapToGlobal(local.topLeft()), local.size()};
}

//! The drop look the preview and the dock highlight share: an accent outline (dashed until a
//! release would act), a tinted fill, and the sentence saying what a release does.
void paintDropFrame(QPainter& p, const QRect& area, const QString& text, bool strong)
{
	const tokens::Palette& pal = tokens::palette();
	p.setRenderHint(QPainter::Antialiasing);
	QColor fill = pal.accent;
	fill.setAlpha(strong ? 90 : 35);
	p.setPen(QPen(pal.accent, 2.0, strong ? Qt::SolidLine : Qt::DashLine));
	p.setBrush(fill);
	p.drawRoundedRect(QRectF(area).adjusted(1, 1, -1, -1), tokens::radius::lg, tokens::radius::lg);
	if (text.isEmpty()) { return; }
	p.setFont(tokens::font(tokens::TextRole::Body));
	const int inset = tokens::space::xxl;
	const QRect bounds = p.fontMetrics().boundingRect(area.adjusted(inset, inset, -inset, -inset),
		Qt::AlignCenter | Qt::TextWordWrap, text);
	p.setPen(Qt::NoPen);
	p.setBrush(pal.raised);
	p.drawRoundedRect(bounds.adjusted(-tokens::space::lg, -tokens::space::sm, tokens::space::lg, tokens::space::sm),
		tokens::radius::md, tokens::radius::md);
	p.setPen(pal.text);
	p.drawText(bounds, Qt::AlignCenter | Qt::TextWordWrap, text);
}

//! The tear-off's preview: the window the panel will become, where it will open.
class TearPreview : public QWidget
{
public:
	explicit TearPreview(QWidget* owner)
		: QWidget(owner, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
	{
		setObjectName(QStringLiteral("shellTearPreview"));
		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_ShowWithoutActivating);
	}

	void setState(bool armed, const QString& text)
	{
		if (armed == m_armed && text == m_text) { return; }
		m_armed = armed;
		m_text = text;
		update();
	}
	QString text() const { return m_text; }

protected:
	void paintEvent(QPaintEvent*) override
	{
		QPainter p(this);
		paintDropFrame(p, rect(), m_text, m_armed);
	}

private:
	bool m_armed = false;
	QString m_text;
};

} // namespace

ShellDockHint::ShellDockHint(QWidget* parent)
	: QWidget(parent)
	, m_fade(new QTimer(this))
{
	setObjectName(QStringLiteral("shellDockHint"));
	setAttribute(Qt::WA_TransparentForMouseEvents);
	m_fade->setSingleShot(true);
	m_fade->setInterval(1500);
	connect(m_fade, &QTimer::timeout, this, &ShellDockHint::clear);
	hide();
}

void ShellDockHint::showOver(const QRect& zone, const QString& text, Mode mode)
{
	m_text = text;
	m_mode = mode;
	setGeometry(zone);
	raise();
	show();
	update();
	if (mode == Teach) { m_fade->start(); }
	else { m_fade->stop(); }
}

void ShellDockHint::clear()
{
	m_mode = Hidden;
	m_text.clear();
	m_fade->stop();
	hide();
}

void ShellDockHint::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	paintDropFrame(p, rect(), m_text, m_mode == Armed);
}

ShellTearOff::ShellTearOff(Shell* shell, ShellPanel* frame)
	: QObject(frame->header())
	, m_shell(shell)
	, m_frame(frame)
	, m_id(frame->id())
{
	QWidget* header = frame->header();
	header->installEventFilter(this);
	// The open hand says the header can be picked up; its buttons and an editor's controls keep the arrow.
	header->setCursor(Qt::OpenHandCursor);
	for (QObject* child : header->children()) { keepArrow(child); }
}

void ShellTearOff::keepArrow(QObject* child)
{
	auto* widget = qobject_cast<QWidget*>(child);
	if (widget == nullptr || qobject_cast<QLabel*>(widget) != nullptr || widget->testAttribute(Qt::WA_SetCursor)) { return; }
	widget->setCursor(Qt::ArrowCursor);
}

bool ShellTearOff::eventFilter(QObject* watched, QEvent* event)
{
	switch (event->type())
	{
	case QEvent::MouseButtonPress: return onPress(event);
	case QEvent::MouseMove: return onMouseMove(event);
	case QEvent::MouseButtonRelease: return onRelease(event);
	case QEvent::MouseButtonDblClick: return onDoubleClick(event);
	case QEvent::KeyPress: return onKey(event);
	case QEvent::ContextMenu:
		showMenu(static_cast<QContextMenuEvent*>(event)->globalPos());
		return true;
	case QEvent::ChildPolished: keepArrow(static_cast<QChildEvent*>(event)->child()); break;
	default: break;
	}
	return QObject::eventFilter(watched, event);
}

bool ShellTearOff::onPress(QEvent* event)
{
	auto* mouse = static_cast<QMouseEvent*>(event);
	if (mouse->button() != Qt::LeftButton) { return false; }
	press(globalPosition(mouse), positionF(mouse).toPoint());
	return true;
}

bool ShellTearOff::onMouseMove(QEvent* event)
{
	auto* mouse = static_cast<QMouseEvent*>(event);
	if (m_drag == Drag::None) { return false; }
	if (!(mouse->buttons() & Qt::LeftButton))
	{
		cancel();
		return false;
	}
	move(globalPosition(mouse));
	return true;
}

bool ShellTearOff::onRelease(QEvent* event)
{
	auto* mouse = static_cast<QMouseEvent*>(event);
	if (mouse->button() != Qt::LeftButton || m_drag == Drag::None) { return false; }
	const bool dragged = m_drag != Drag::Pending;
	release(globalPosition(mouse));
	return dragged;
}

bool ShellTearOff::onDoubleClick(QEvent* event)
{
	if (static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton) { return false; }
	cancel();
	m_shell->dispatchFloat(m_id, !m_shell->isDetached(m_id));
	return true;
}

bool ShellTearOff::onKey(QEvent* event)
{
	const bool dragging = m_drag == Drag::Tearing || m_drag == Drag::Moving;
	if (!dragging || static_cast<QKeyEvent*>(event)->key() != Qt::Key_Escape) { return false; }
	cancel();
	return true;
}

void ShellTearOff::press(const QPoint& global, const QPoint& local)
{
	finish();
	m_drag = Drag::Pending;
	m_pressGlobal = global;
	m_pressLocal = local;
}

void ShellTearOff::move(const QPoint& global)
{
	if (m_drag == Drag::Pending) { begin(global); }
	if (m_drag == Drag::Tearing) { followPreview(global); }
	else if (m_drag == Drag::Moving) { followWindow(global); }
}

void ShellTearOff::begin(const QPoint& global)
{
	if ((global - m_pressGlobal).manhattanLength() < QApplication::startDragDistance()) { return; }
	QApplication::setOverrideCursor(Qt::ClosedHandCursor);
	m_cursorSet = true;
	m_frame->header()->grabKeyboard();  // Esc cancels the drag
	if (m_shell->isDetached(m_id)) { beginMove(); }
	else { beginTear(); }
}

void ShellTearOff::beginTear()
{
	m_drag = Drag::Tearing;
	m_size = Shell::floatSize(m_frame);
	// Wayland would put the preview wherever it liked: the window simply opens where the compositor puts it.
	if (onWayland()) { return; }
	m_preview = new TearPreview(m_shell);
	m_preview->show();
}

void ShellTearOff::beginMove()
{
	QWidget* own = m_shell->floatingWindow(m_id);
	if (own == nullptr)
	{
		finish();
		return;
	}
	if (onWayland() && own->windowHandle() != nullptr)
	{
		// The compositor moves it; there is no pointer position to dock by, so Dock docks.
		finish();
		own->windowHandle()->startSystemMove();
		return;
	}
	m_drag = Drag::Moving;
	m_windowStart = own->pos();
	own->setProperty(kHeaderMove, true);
	own->setWindowOpacity(0.75);  // the dock highlight shows through the window being dropped
}

void ShellTearOff::followPreview(const QPoint& global)
{
	auto* preview = static_cast<TearPreview*>(m_preview.data());
	if (preview == nullptr) { return; }
	preview->setGeometry(QRect(global - m_pressLocal, m_size));
	const QString title = ShellLayout::spec(m_id).title;
	const bool out = !nearHeader(global);
	preview->setState(out, out ? tr("Release to float %1 in its own window\n(Esc cancels)").arg(title)
		: tr("Keep dragging to float %1").arg(title));
}

void ShellTearOff::followWindow(const QPoint& global)
{
	QWidget* own = m_shell->floatingWindow(m_id);
	if (own == nullptr)
	{
		cancel();
		return;
	}
	own->move(m_windowStart + (global - m_pressGlobal));
	const QWidget* main = m_shell->window();
	if (m_shell->dockZone(m_id).isEmpty() || !main->frameGeometry().contains(global))
	{
		m_shell->showDockHint(QString(), ShellDockHint::Hidden);
		return;
	}
	m_shell->showDockHint(m_id, overDockZone(global) ? ShellDockHint::Armed : ShellDockHint::Hint);
}

void ShellTearOff::release(const QPoint& global)
{
	const Drag was = m_drag;
	const bool dock = was == Drag::Moving && overDockZone(global);
	finish();
	if (was == Drag::Tearing && !nearHeader(global))
	{
		m_shell->setNextFloatGeometry(m_id, QRect(global - m_pressLocal, m_size));
		m_shell->dispatchFloat(m_id, true);
	}
	else if (dock) { m_shell->dispatchFloat(m_id, false); }
	else if (was == Drag::Moving) { m_shell->schedulePersist(); }
}

void ShellTearOff::cancel()
{
	QWidget* own = m_drag == Drag::Moving ? m_shell->floatingWindow(m_id) : nullptr;
	if (own != nullptr) { own->move(m_windowStart); }
	finish();
}

void ShellTearOff::finish()
{
	m_drag = Drag::None;
	if (!m_preview.isNull()) { m_preview->deleteLater(); }
	m_preview = nullptr;
	if (QWidget::keyboardGrabber() == m_frame->header()) { m_frame->header()->releaseKeyboard(); }
	if (std::exchange(m_cursorSet, false)) { QApplication::restoreOverrideCursor(); }
	if (QWidget* own = m_shell->floatingWindow(m_id))
	{
		own->setWindowOpacity(1.0);
		own->setProperty(kHeaderMove, false);
	}
	m_shell->showDockHint(QString(), ShellDockHint::Hidden);
}

QString ShellTearOff::previewText() const
{
	const auto* preview = static_cast<const TearPreview*>(m_preview.data());
	return preview != nullptr ? preview->text() : QString();
}

bool ShellTearOff::nearHeader(const QPoint& global) const
{
	// As a browser tab tears off its strip: pulled a header's height away, or past its ends.
	const QWidget* header = m_frame->header();
	const int reach = header->height();
	return globalRect(header, header->rect().adjusted(0, -reach, 0, reach)).contains(global);
}

bool ShellTearOff::overDockZone(const QPoint& global) const
{
	const QWidget* main = m_shell->window();
	const QRect zone = m_shell->dockZone(m_id);
	return !zone.isEmpty() && main->isVisible() && !main->isMinimized() && globalRect(m_shell, zone).contains(global);
}

void ShellTearOff::showMenu(const QPoint& global)
{
	QMenu menu;
	const bool floating = m_shell->isDetached(m_id);
	QAction* toggle = menu.addAction(floating ? tr("Dock in the Main Window") : tr("Float in Its Own Window"));
	toggle->setProperty("controlCommand", floating ? QStringLiteral("window.attach") : QStringLiteral("window.detach"));
	connect(toggle, &QAction::triggered, this, [this, floating]() { m_shell->dispatchFloat(m_id, !floating); });
	if (floating) { addScreenMenu(menu); }
	menu.exec(global);
}

void ShellTearOff::addScreenMenu(QMenu& menu)
{
	const QList<QScreen*> screens = QGuiApplication::screens();
	const QWidget* own = m_shell->floatingWindow(m_id);
	// The move is window.detach's `screen`; a registry whose window.detach has none offers no move.
	const ControlCommand* detach = ControlRegistry::instance()->command(QStringLiteral("window.detach"));
	const bool movable = detach != nullptr
		&& detach->argsSchema.value(QStringLiteral("properties")).toObject().contains(QStringLiteral("screen"));
	if (screens.size() < 2 || own == nullptr || !movable) { return; }
	QMenu* sub = menu.addMenu(tr("Move to Screen"));
	for (int i = 0; i < screens.size(); ++i)
	{
		const QRect area = screens.at(i)->geometry();
		QAction* action = sub->addAction(tr("%1 (%2 x %3)").arg(screens.at(i)->name()).arg(area.width()).arg(area.height()));
		action->setCheckable(true);
		action->setChecked(own->screen() == screens.at(i));
		action->setProperty("controlCommand", QStringLiteral("window.detach"));
		connect(action, &QAction::triggered, this, [this, i]() {
			ControlRegistry::instance()->invoke(QStringLiteral("window.detach"),
				{{QStringLiteral("panel"), m_id}, {QStringLiteral("screen"), i}});
		});
	}
}

void noteFloatingMoved(Shell* shell, const QString& id)
{
	const QWidget* own = shell->floatingWindow(id);
	if (own == nullptr || own->property(kHeaderMove).toBool() || onWayland()) { return; }
	const QRect zone = shell->dockZone(id);
	if (zone.isEmpty() || !globalRect(shell, zone).contains(QCursor::pos())) { return; }
	shell->showDockHint(id, ShellDockHint::Teach);
}

} // namespace lmms::gui
