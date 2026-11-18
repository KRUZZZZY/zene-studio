/*
 * ClipLockFeedback.cpp - a locked clip shown as locked, and a refused edit of one said out loud
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

#include "ClipLockFeedback.h"

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>

#include "Clip.h"
#include "ClipView.h"
#include "DesignTokens.h"
#include "EditLocks.h"
#include "TextFloat.h"
#include "Track.h"
#include "embed.h"

namespace lmms::gui::cliplocks
{

namespace
{

const char* const BadgeName = "clipLockBadge";
constexpr int BadgeSize = 14;
constexpr int BadgeMargin = 3;
constexpr int MessageMs = 3000;

//! The padlock in a clip's top-right corner: transparent to the mouse, so every press still reaches the
//! clip (to select it), and kept in the corner as the clip view resizes.
class LockBadge : public QWidget
{
public:
	explicit LockBadge(QWidget* clipView) :
		QWidget(clipView)
	{
		setObjectName(QLatin1String(BadgeName));
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		setFocusPolicy(Qt::NoFocus);
		setFixedSize(BadgeSize, BadgeSize);
		clipView->installEventFilter(this);
		place();
	}

	//! In the corner, shown while the clip is locked - but too narrow a clip shows no badge rather
	//! than one that covers it.
	void place()
	{
		const QWidget* clip = parentWidget();
		move(clip->width() - BadgeSize - BadgeMargin, BadgeMargin);
		setVisible(clip->width() >= 2 * BadgeSize && property("locked").toBool());
	}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (watched == parentWidget() && event->type() == QEvent::Resize) { place(); }
		return QWidget::eventFilter(watched, event);
	}

	void paintEvent(QPaintEvent*) override
	{
		const tokens::Palette& pal = tokens::palette();
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		QColor disc = pal.base;
		disc.setAlpha(210);
		p.setPen(Qt::NoPen);
		p.setBrush(disc);
		p.drawEllipse(rect());
		// The shackle, then the body: a padlock at any theme's text colour.
		p.setPen(QPen(pal.text, 1.4));
		p.setBrush(Qt::NoBrush);
		p.drawArc(QRectF(4.5, 2.5, 5, 6), 0, 180 * 16);
		p.setPen(Qt::NoPen);
		p.setBrush(pal.text);
		p.drawRoundedRect(QRectF(3.5, 6, 7, 5.5), 1, 1);
	}
};

LockBadge* badgeOf(const QWidget* view)
{
	return static_cast<LockBadge*>(view->findChild<QWidget*>(QLatin1String(BadgeName), Qt::FindDirectChildrenOnly));
}

void refreshAll()
{
	if (qApp == nullptr) { return; }  // a lock set cleared while the application goes away
	for (QWidget* top : QApplication::topLevelWidgets())
	{
		for (ClipView* view : top->findChildren<ClipView*>()) { syncBadge(view); }
	}
}

//! "this clip" or "its track", whichever holds the lock.
QString lockHolder(const Clip* clip)
{
	return EditLocks::instance().clipLocked(clip->id()) ? ClipView::tr("This clip is locked")
		: ClipView::tr("This clip's track is locked");
}

} // namespace

void syncBadge(ClipView* view)
{
	// A closed view waits for its deferred delete, and its clip may be gone already (a project clear or
	// load closes every view before the lock set changes): never ask about it.
	if (view->isHidden()) { return; }
	const bool locked = EditLocks::instance().locks(view->getClip());
	LockBadge* badge = badgeOf(view);
	if (badge == nullptr && !locked) { return; }
	if (badge == nullptr) { badge = new LockBadge(view); }
	if (badge->property("locked").toBool() != locked)
	{
		badge->setProperty("locked", locked);
	}
	// The holder moves from the track to the clip (and back) while the badge stays locked, so the
	// tooltip follows the holder on every sync, not only when the locked state itself flips.
	badge->setToolTip(locked ? lockHolder(view->getClip()) : QString());
	badge->place();
}

bool badgeShown(const ClipView* view)
{
	const LockBadge* badge = badgeOf(view);
	return badge != nullptr && badge->property("locked").toBool() && !badge->isHidden();
}

TextFloat* refuse(const Clip* clip, const QString& action, int timeoutMs)
{
	const QString how = EditLocks::instance().clipLocked(clip->id())
		? ClipView::tr("Unlock it from its menu (Unlock Clip) to edit it.")
		: ClipView::tr("Unlock the track (Unlock Track) to edit it.");
	return TextFloat::displayMessage(ClipView::tr("Locked"),
		ClipView::tr("%1, so it is not %2. It can still be selected and inspected.\n%3").arg(lockHolder(clip), action, how),
		embed::getIconPixmap("hint"), timeoutMs);
}

void refuseDropOnto(const Clip* under)
{
	TextFloat::displayMessage(ClipView::tr("Locked"),
		ClipView::tr("The clip went back: dropped there it would trim \"%1\", which is locked.\nUnlock it, or set Overlap to "
			"Free (Tools > Overlap) to let clips overlap.").arg(under->name().isEmpty() ? ClipView::tr("a clip") : under->name()),
		embed::getIconPixmap("hint"), MessageMs);
}

void announceKept(int kept)
{
	if (kept <= 0) { return; }
	TextFloat::displayMessage(ClipView::tr("Locked"),
		ClipView::tr("%n locked clip(s) stayed where they were; the others were changed.", nullptr, kept),
		embed::getIconPixmap("hint"), MessageMs);
}

void install()
{
	EditLocks::setChangeHook(&refreshAll);
}

} // namespace lmms::gui::cliplocks
