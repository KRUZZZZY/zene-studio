/*
 * FolderSummaryLane.cpp - a folder track's row draws its children's clips as summary clips
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

#include "FolderSummaryLane.h"

#include <algorithm>
#include <cmath>

#include <QContextMenuEvent>
#include <QJsonArray>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include "Clip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "GuiApplication.h"
#include "SongEditor.h"
#include "TrackContainerView.h"
#include "TrackContentWidget.h"
#include "TrackFolder.h"
#include "TrackView.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

constexpr int kWatchMs = 250;  //!< how often the lane looks for a change made elsewhere
constexpr int kMaxRows = 8;    //!< the miniature shows at most this many child tracks

ControlResult invoke(const QString& command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(command, args);
}

bool run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = invoke(command, args);
	if (result.ok) { return true; }
	if (isUnattendedRun())
	{
		qWarning("folder summary: %s refused: %s", qPrintable(command), qPrintable(result.errorMessage));
		return false;
	}
	QMessageBox::warning(nullptr, QObject::tr("Folder"), result.errorMessage);
	return false;
}

/*! The arranger's range set over @a span on the tracks it covers, for one range edit; the range the
 *  user had (or none) is put back when the scope ends - the range is interface state, not the edit's. */
class RangeScope
{
public:
	RangeScope(const TrackFolder& folder, const FolderSpan& span)
		: m_previous(invoke(QStringLiteral("range.get")).result)
	{
		QJsonArray tracks;
		for (const Track* track : folderTracksWithin(folder, span.start, span.end)) { tracks.append(control::trackIdOf(track)); }
		m_ok = !tracks.isEmpty() && run(QStringLiteral("range.set"), {{QStringLiteral("start"), static_cast<int>(span.start)},
			{QStringLiteral("end"), static_cast<int>(span.end)}, {QStringLiteral("tracks"), tracks}, {QStringLiteral("all"), false}});
	}

	~RangeScope()
	{
		if (!m_previous.value(QStringLiteral("active")).toBool())
		{
			invoke(QStringLiteral("range.clear"));
			return;
		}
		invoke(QStringLiteral("range.set"), {{QStringLiteral("start"), m_previous.value(QStringLiteral("start"))},
			{QStringLiteral("end"), m_previous.value(QStringLiteral("end"))}, {QStringLiteral("tracks"), m_previous.value(QStringLiteral("tracks"))},
			{QStringLiteral("all"), m_previous.value(QStringLiteral("all_tracks"))}});
	}

	RangeScope(const RangeScope&) = delete;
	RangeScope& operator=(const RangeScope&) = delete;

	bool ok() const { return m_ok; }

private:
	QJsonObject m_previous;
	bool m_ok = false;
};

bool sameSpans(const std::vector<FolderSpan>& a, const std::vector<FolderSpan>& b)
{
	return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const FolderSpan& x, const FolderSpan& y) {
		return x.start == y.start && x.end == y.end && x.clips == y.clips;
	});
}

} // namespace

FolderSummaryLane::FolderSummaryLane(TrackFolder* folder, TrackView* view)
	: QWidget(view->getTrackContentWidget())
	, m_folder(folder)
	, m_view(view)
{
	setObjectName(QStringLiteral("folderSummaryLane"));
	setAccessibleName(tr("Folder summary clips"));
	setMouseTracking(false);
	parentWidget()->installEventFilter(this);
	setGeometry(parentWidget()->rect());
	show();  // a child made after its parent was shown stays hidden otherwise
	auto* timer = new QTimer(this);
	timer->setInterval(kWatchMs);
	connect(timer, &QTimer::timeout, this, &FolderSummaryLane::watch);
	timer->start();
	m_seen = spans();
}

std::vector<FolderSpan> FolderSummaryLane::spans() const
{
	return folderSummary(*m_folder);
}

int FolderSummaryLane::xOfTick(tick_t tick) const
{
	const TrackContainerView* tcv = m_view->trackContainerView();
	const double ticks = static_cast<double>(tick - tcv->currentPosition().getTicks());
	return static_cast<int>(std::lround(ticks * tcv->pixelsPerBar() / TimePos::ticksPerBar()));
}

tick_t FolderSummaryLane::ticksOf(int dx, bool free) const
{
	const double ticks = dx * static_cast<double>(TimePos::ticksPerBar()) / std::max(1.f, m_view->trackContainerView()->pixelsPerBar());
	const SongEditor* editor = getGUI() != nullptr && getGUI()->songEditor() != nullptr ? getGUI()->songEditor()->m_editor : nullptr;
	const double snap = free || editor == nullptr ? 1.0 : std::max(1.0, std::round(static_cast<double>(editor->getSnapSize()) * TimePos::ticksPerBar()));
	return static_cast<tick_t>(std::lround(ticks / snap) * snap);
}

QRect FolderSummaryLane::spanRect(const FolderSpan& span) const
{
	const int left = xOfTick(span.start);
	const int right = std::max(left + 2, xOfTick(span.end));
	return QRect(left, 1, right - left, std::max(4, height() - 3));
}

int FolderSummaryLane::spanAt(const QPoint& pos) const
{
	const std::vector<FolderSpan> all = spans();
	for (std::size_t i = 0; i < all.size(); ++i)
	{
		if (spanRect(all[i]).contains(pos)) { return static_cast<int>(i); }
	}
	return -1;
}

bool FolderSummaryLane::moveSpan(const FolderSpan& span, tick_t delta)
{
	const bool done = run(QStringLiteral("track.folder_move"), {{QStringLiteral("folder"), control::trackIdOf(m_folder)},
		{QStringLiteral("delta"), static_cast<int>(delta)}, {QStringLiteral("start"), static_cast<int>(span.start)},
		{QStringLiteral("end"), static_cast<int>(span.end)}});
	update();
	return done;
}

bool FolderSummaryLane::copySpan(const FolderSpan& span, tick_t position)
{
	bool done = false;
	{
		const RangeScope scope(*m_folder, span);
		// range.copy changes nothing; range.paste is the one undo step.
		done = scope.ok() && run(QStringLiteral("range.copy"), {})
			&& run(QStringLiteral("range.paste"), {{QStringLiteral("position"), static_cast<int>(position)}});
	}
	update();
	return done;
}

bool FolderSummaryLane::deleteSpan(const FolderSpan& span)
{
	bool done = false;
	{
		const RangeScope scope(*m_folder, span);
		done = scope.ok() && run(QStringLiteral("range.delete"), {});
	}
	update();
	return done;
}

bool FolderSummaryLane::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == parentWidget() && event->type() == QEvent::Resize) { setGeometry(parentWidget()->rect()); }
	return QWidget::eventFilter(watched, event);
}

void FolderSummaryLane::watch()
{
	std::vector<FolderSpan> now = spans();
	if (sameSpans(now, m_seen)) { return; }
	m_seen = std::move(now);
	update();
}

void FolderSummaryLane::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	const std::vector<FolderSpan> all = spans();
	for (std::size_t i = 0; i < all.size(); ++i)
	{
		const bool dragged = static_cast<int>(i) == m_dragged;
		paintSpan(p, all[i], 0, false);
		if (dragged && m_dragDx != 0) { paintSpan(p, all[i], m_dragDx, true); }
	}
}

void FolderSummaryLane::paintSpan(QPainter& p, const FolderSpan& span, int dx, bool dragged) const
{
	const tokens::Palette& pal = tokens::palette();
	const QRect rect = spanRect(span).translated(dx, 0);
	if (!rect.intersects(this->rect())) { return; }
	QColor fill = m_folder->color().value_or(pal.clipPattern);
	fill.setAlpha(dragged ? 90 : 150);
	p.setPen(QPen(dragged ? pal.accent : pal.separator, 1, dragged ? Qt::DashLine : Qt::SolidLine));
	p.setBrush(fill);
	p.drawRoundedRect(rect.adjusted(0, 0, -1, -1), tokens::radius::sm, tokens::radius::sm);
	if (!dragged) { paintMiniature(p, span, rect.adjusted(2, 2, -2, -2)); }
}

void FolderSummaryLane::paintMiniature(QPainter& p, const FolderSpan& span, const QRect& rect) const
{
	const std::vector<Track*> tracks = folderTracksWithin(*m_folder, span.start, span.end);
	const int rows = std::min<int>(kMaxRows, static_cast<int>(tracks.size()));
	if (rows == 0 || rect.height() < rows) { return; }
	const int rowHeight = std::max(1, rect.height() / rows);
	QColor bar = tokens::palette().text;
	bar.setAlpha(140);
	for (int row = 0; row < rows; ++row)
	{
		for (const Clip* clip : tracks[static_cast<std::size_t>(row)]->getClips())
		{
			const tick_t start = clip->startPosition().getTicks();
			if (start < span.start || start >= span.end) { continue; }
			const int left = xOfTick(start);
			const int right = std::max(left + 1, xOfTick(clip->endPosition().getTicks()));
			p.fillRect(QRect(left, rect.top() + row * rowHeight, right - left, std::max(1, rowHeight - 1)), bar);
		}
	}
}

void FolderSummaryLane::mousePressEvent(QMouseEvent* event)
{
	const int index = event->button() == Qt::LeftButton ? spanAt(event->pos()) : -1;
	if (index < 0)
	{
		event->ignore();  // the folder's own content widget handles it, as before
		return;
	}
	m_dragged = index;
	m_pressPos = event->pos();
	m_dragDx = 0;
	m_copy = (event->modifiers() & Qt::ControlModifier) != 0;
	event->accept();
}

void FolderSummaryLane::mouseMoveEvent(QMouseEvent* event)
{
	if (m_dragged < 0) { return event->ignore(); }
	const tick_t delta = ticksOf(event->pos().x() - m_pressPos.x(), (event->modifiers() & Qt::AltModifier) != 0);
	m_dragDx = xOfTick(delta) - xOfTick(0);
	update();
}

void FolderSummaryLane::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_dragged < 0) { return event->ignore(); }
	const std::vector<FolderSpan> all = spans();
	const tick_t delta = ticksOf(event->pos().x() - m_pressPos.x(), (event->modifiers() & Qt::AltModifier) != 0);
	const int index = m_dragged;
	m_dragged = -1;
	m_dragDx = 0;
	if (index >= static_cast<int>(all.size()) || delta == 0) { return update(); }
	const FolderSpan& span = all[static_cast<std::size_t>(index)];
	if (m_copy) { copySpan(span, std::max<tick_t>(0, span.start + delta)); }
	else { moveSpan(span, delta); }
}

void FolderSummaryLane::contextMenuEvent(QContextMenuEvent* event)
{
	const int index = spanAt(event->pos());
	if (index < 0) { return event->ignore(); }
	const FolderSpan span = spans()[static_cast<std::size_t>(index)];
	QMenu menu(this);
	QAction* duplicate = menu.addAction(tr("Duplicate summary clip"));
	duplicate->setData(QStringLiteral("range.paste"));
	connect(duplicate, &QAction::triggered, this, [this, span] { copySpan(span, span.end); });
	QAction* remove = menu.addAction(tr("Delete summary clip"));
	remove->setData(QStringLiteral("range.delete"));
	connect(remove, &QAction::triggered, this, [this, span] { deleteSpan(span); });
	menu.exec(event->globalPos());
}

} // namespace lmms::gui
