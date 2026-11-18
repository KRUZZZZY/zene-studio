/*
 * AccessibleMode.cpp - focus rings, reduced motion and announcements (AccessibleWidgets.h)
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

#include "AccessibleWidgets.h"
#include "DirectChildOfType.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QApplication>
#include <QFocusEvent>
#include <QFocusFrame>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPainter>
#include <QPointer>

#include "DesignTokens.h"
#include "ModelView.h"

namespace lmms::a11y
{

namespace
{

const char* const RingName = "zeneFocusRing";

//! Whether @a widget needs the ring drawn for it: a custom control (a parameter view, or one that asks with the
//! "zeneFocusRing" property) - the stylesheet already rings the stock fields, buttons and lists.
bool wantsRing(const QWidget* widget)
{
	if (widget->property(RingName).toBool()) { return true; }
	return dynamic_cast<const gui::ModelView*>(widget) != nullptr && qobject_cast<const QLineEdit*>(widget) == nullptr
		&& qobject_cast<const QAbstractSpinBox*>(widget) == nullptr && qobject_cast<const QAbstractItemView*>(widget) == nullptr;
}

//! A focus the keyboard moved shows the ring (CSS's :focus-visible); in accessible mode every focus does.
bool ringShown(Qt::FocusReason reason)
{
	return gui::tokens::accessibleMode() || reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
		|| reason == Qt::ShortcutFocusReason;
}

//! The ring: a child laid over the focused control, painting only its outline, clicks passing through. It is a
//! QFocusFrame (whose own widget tracking is not used) because Qt's accessibility leaves focus frames out of the
//! tree, so the control does not gain a nameless child for a screen reader.
class FocusRing : public QFocusFrame
{
public:
	FocusRing()
	{
		setObjectName(QLatin1String(RingName));
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		setFocusPolicy(Qt::NoFocus);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const qreal width = gui::tokens::focusRingWidth();
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(gui::tokens::palette().focusRing, width));
		p.setBrush(Qt::NoBrush);
		const qreal inset = width / 2.0;
		p.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), gui::tokens::radius::md, gui::tokens::radius::md);
	}
};

//! Accessible mode's runtime part, an application event filter: the focus ring follows the keyboard focus over the
//! custom controls, and with reduced motion each main window's docks and the menus, combo boxes and tooltips stop
//! animating. It only looks at focus, resize and show events and never stops one.
class AccessibleMode : public QObject
{
public:
	explicit AccessibleMode(QObject* parent)
		: QObject(parent)
	{
		setObjectName(QStringLiteral("zeneAccessibleMode"));
	}

	~AccessibleMode() override { delete m_ring; }

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		switch (event->type())
		{
			case QEvent::FocusIn: focusIn(watched, static_cast<QFocusEvent*>(event)->reason()); break;
			case QEvent::FocusOut:
				if (watched == m_target) { hideRing(); }
				break;
			case QEvent::Resize:
				if (watched == m_target && m_ring != nullptr) { m_ring->setGeometry(m_target->rect()); }
				break;
			case QEvent::Show: mainWindowShown(qobject_cast<QMainWindow*>(watched)); break;
			default: break;
		}
		return false;
	}

private:
	void focusIn(QObject* watched, Qt::FocusReason reason)
	{
		auto* widget = qobject_cast<QWidget*>(watched);
		if (widget == nullptr || widget == m_ring || !wantsRing(widget) || !ringShown(reason)) { return; }
		if (m_ring == nullptr) { m_ring = new FocusRing; }
		m_target = widget;
		m_ring->setParent(widget);
		m_ring->setGeometry(widget->rect());
		m_ring->raise();
		m_ring->show();
	}

	void hideRing()
	{
		m_target = nullptr;
		if (m_ring == nullptr) { return; }
		m_ring->hide();
		m_ring->setParent(nullptr);  // never deleted with a control that goes away
	}

	//! A main window shows after the engine made the song: its transitions are followed from then (theme 17).
	void mainWindowShown(QMainWindow* window)
	{
		if (window == nullptr) { return; }
		watchSongTransitions();
		reduceMotion(window);
	}

	void reduceMotion(QMainWindow* window)
	{
		if (!gui::tokens::reducedMotion()) { return; }
		window->setAnimated(false);
		for (const Qt::UIEffect effect : {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip,
			Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox})
		{
			QApplication::setEffectEnabled(effect, false);
		}
	}

	QPointer<FocusRing> m_ring;
	QPointer<QWidget> m_target;
};

//! The startup hook runs inside QCoreApplication's constructor, before the QApplication part exists (a cast to it
//! fails there), so the installation is queued to the first turn of the event loop.
void startAccessibility()
{
	installAccessibleWidgets();  // the descriptions need no QApplication
	QMetaObject::invokeMethod(QCoreApplication::instance(), [] { installAccessibility(); }, Qt::QueuedConnection);
}

// Every binary that links the interface - the application and each test - describes its custom controls and
// shows its focus rings from the moment its application object exists.
Q_COREAPP_STARTUP_FUNCTION(startAccessibility)

} // namespace

void installAccessibility()
{
	installAccessibleWidgets();
	auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
	if (app == nullptr || directChildOfType<AccessibleMode>(app, QStringLiteral("zeneAccessibleMode")) != nullptr)
	{
		return;
	}
	app->installEventFilter(new AccessibleMode(app));
}

namespace
{

//! The window's live region: a one-pixel label that is never painted over anything and takes no focus. Its
//! name is the message being announced, which is what a pre-6.8 alert makes the screen reader read.
QLabel* liveRegion(QWidget* window)
{
	static QPointer<QLabel> region;
	if (region.isNull())
	{
		region = new QLabel(window);
		region->setObjectName(QStringLiteral("zeneLiveRegion"));
		region->setFocusPolicy(Qt::NoFocus);
		region->setAttribute(Qt::WA_TransparentForMouseEvents);
		region->setFixedSize(1, 1);
	}
	else if (region->parentWidget() != window)
	{
		region->setParent(window);
		region->setFixedSize(1, 1);
	}
	return region;
}

QWidget* liveRegionWindow()
{
	if (QWidget* active = QApplication::activeWindow()) { return active; }
	for (QWidget* top : QApplication::topLevelWidgets())
	{
		if (qobject_cast<QMainWindow*>(top) != nullptr) { return top; }
	}
	return nullptr;
}

} // namespace

QObject* announcementSource(QObject* source, const QString& message)
{
	if (source == nullptr) { return nullptr; }
	if (QAccessibleInterface* shown = QAccessible::queryAccessibleInterface(source))
	{
		if (shown->text(QAccessible::Name) == message) { return source; }
	}
	QWidget* window = liveRegionWindow();
	if (window == nullptr) { return source; }
	QLabel* region = liveRegion(window);
	region->setText(message);
	region->setAccessibleName(message);
	region->show();
	return region;
}

void announceStatus(QObject* source, const QString& message, Politeness politeness, Repeat repeat)
{
	// The policy first (AccessibleAnnouncements.cpp), so a test with no screen reader still sees what would be said.
	if (source == nullptr || !shouldAnnounce(message, politeness, repeat) || !QAccessible::isActive()) { return; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	QAccessibleAnnouncementEvent event(source, message);
	event.setPoliteness(politeness == Politeness::Assertive ? QAccessible::AnnouncementPoliteness::Assertive
		: QAccessible::AnnouncementPoliteness::Polite);
	QAccessible::updateAccessibility(&event);
#else
	Q_UNUSED(politeness)
	QAccessibleEvent event(announcementSource(source, message), QAccessible::Alert);
	QAccessible::updateAccessibility(&event);
#endif
}

} // namespace lmms::a11y
