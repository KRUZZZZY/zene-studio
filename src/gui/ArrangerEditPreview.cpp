/*
 * ArrangerEditPreview.cpp - what a clip's drag will do, drawn before the drop
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

#include "ArrangerEditPreview.h"

#include <cmath>

#include <QEvent>
#include <QPainter>
#include <QScrollArea>

#include "Clip.h"
#include "ClipView.h"
#include "DesignTokens.h"
#include "SongEditor.h"
#include "TrackView.h"

namespace lmms::gui
{

namespace
{

constexpr int LabelPad = 3;
constexpr int MinLabelWidth = 30;

ArrangerEditPreview* previewFor(ClipView* view)
{
	if (view == nullptr || view->getTrackView() == nullptr) { return nullptr; }
	return view->getTrackView()->trackContainerView()->findChild<ArrangerEditPreview*>();
}

//! A small text box: @a text at @a at (its top-left), on a raised fill.
void paintTag(QPainter& p, const QPoint& at, const QString& text, const QColor& fill, const QColor& ink)
{
	const QRect box = p.fontMetrics().boundingRect(text).adjusted(-LabelPad, -1, LabelPad, 1);
	const QRect placed(at, box.size());
	p.fillRect(placed, fill);
	p.setPen(ink);
	p.drawText(placed, Qt::AlignCenter, text);
}

QString snapName(const arrangement::SnapHit& snap)
{
	switch (snap.kind)
	{
	case arrangement::SnapHit::Kind::Grid: return ArrangerEditPreview::tr("Grid");
	case arrangement::SnapHit::Kind::ClipEdge: return ArrangerEditPreview::tr("Clip edge");
	case arrangement::SnapHit::Kind::Marker: return snap.label;
	default: return QString();
	}
}

} // namespace

ArrangerEditPreview::ArrangerEditPreview(SongEditor* editor) :
	QWidget(editor->contentWidget()->viewport()),
	m_editor(editor)
{
	setObjectName(QStringLiteral("arrangerEditPreview"));
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setFocusPolicy(Qt::NoFocus);
	QWidget* viewport = editor->contentWidget()->viewport();
	setGeometry(viewport->rect());
	viewport->installEventFilter(this);
	hide();  // only while a clip is dragged
}

void ArrangerEditPreview::showDrag(ClipView* view, tick_t from, const arrangement::SnapHit& snap)
{
	ArrangerEditPreview* preview = previewFor(view);
	if (preview == nullptr) { return; }
	preview->m_view = view;
	preview->m_from = from;
	preview->m_snap = snap;
	preview->layout();
	preview->show();
	preview->raise();
	preview->update();
}

void ArrangerEditPreview::endDrag(ClipView* view)
{
	ArrangerEditPreview* preview = previewFor(view);
	if (preview == nullptr || preview->m_view != view) { return; }
	preview->m_view = nullptr;
	preview->layout();
	preview->hide();
}

bool ArrangerEditPreview::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::Resize) { setGeometry(static_cast<QWidget*>(watched)->rect()); }
	return QWidget::eventFilter(watched, event);
}

int ArrangerEditPreview::xOf(tick_t tick) const
{
	const double ppb = m_editor.isNull() ? 1.0 : m_editor->pixelsPerBar();
	const tick_t origin = m_view->getClip()->startPosition().getTicks();
	return m_row.left() + static_cast<int>(std::lround((tick - origin) * ppb / TimePos::ticksPerBar()));
}

void ArrangerEditPreview::layout()
{
	m_effects.clear();
	m_lockedEffects.clear();
	m_ghost = QRect();
	m_snapX = -1;
	m_refused = false;
	if (m_view.isNull() || m_editor.isNull()) { return; }
	const Clip* clip = m_view->getClip();
	m_row = QRect(mapFromGlobal(m_view->mapToGlobal(QPoint(0, 0))), m_view->size());
	const tick_t start = clip->startPosition().getTicks();
	if (m_from != start) { m_ghost = QRect(xOf(m_from), m_row.top(), m_row.width(), m_row.height()); }
	if (m_snap.kind != arrangement::SnapHit::Kind::None) { m_snapX = xOf(m_snap.line); }
	if (arrangement::modes().overlap == arrangement::Overlap::Free) { return; }
	for (const arrangement::OverlapEffect& effect : arrangement::overlapEffects(clip, start, clip->endPosition().getTicks()))
	{
		m_effects.append(QRect(QPoint(xOf(effect.from), m_row.top()), QPoint(xOf(effect.to) - 1, m_row.bottom())));
		m_lockedEffects.append(effect.locked);
		m_refused = m_refused || effect.locked;
	}
}

void ArrangerEditPreview::paintEffects(QPainter& p) const
{
	const tokens::Palette& pal = tokens::palette();
	const bool crossfade = arrangement::modes().overlap == arrangement::Overlap::Crossfade;
	for (int i = 0; i < m_effects.size(); ++i)
	{
		const QRect& area = m_effects.at(i);
		const QColor tone = m_lockedEffects.at(i) ? pal.record : crossfade ? pal.accent : pal.warning;
		QColor wash = tone;
		wash.setAlpha(90);
		p.fillRect(area, wash);
		p.fillRect(area, QBrush(tone, crossfade ? Qt::NoBrush : Qt::BDiagPattern));
		p.setPen(QPen(tone, 1));
		if (crossfade) { p.drawLine(area.topLeft(), area.bottomRight()); p.drawLine(area.bottomLeft(), area.topRight()); }
		const QString what = m_lockedEffects.at(i) ? tr("Locked") : crossfade ? tr("Crossfade") : tr("Trim");
		if (area.width() >= MinLabelWidth) { paintTag(p, area.topLeft() + QPoint(2, 2), what, pal.raised, pal.text); }
	}
}

void ArrangerEditPreview::paintSnap(QPainter& p) const
{
	if (m_snapX < 0) { return; }
	const tokens::Palette& pal = tokens::palette();
	p.setPen(QPen(pal.accent, 1, Qt::DashLine));
	p.drawLine(m_snapX, 0, m_snapX, height());
	paintTag(p, QPoint(m_snapX + 3, std::max(0, m_row.top() - p.fontMetrics().height() - 2)), snapName(m_snap), pal.accent, pal.textOnAccent);
}

void ArrangerEditPreview::paintEvent(QPaintEvent*)
{
	if (m_view.isNull()) { return; }
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.setFont(tokens::font(tokens::TextRole::Small));
	if (!m_ghost.isEmpty())
	{
		p.setPen(QPen(pal.textMuted, 1, Qt::DashLine));
		p.setBrush(Qt::NoBrush);
		p.drawRect(m_ghost.adjusted(0, 0, -1, -1));
	}
	paintEffects(p);
	paintSnap(p);
	if (m_refused)
	{
		p.setPen(QPen(pal.record, 2));
		p.setBrush(Qt::NoBrush);
		p.drawRect(m_row.adjusted(1, 1, -1, -1));
		paintTag(p, m_row.bottomLeft() + QPoint(2, 2), tr("A locked clip is under it: the drop will be refused"), pal.record, pal.text);
	}
}

} // namespace lmms::gui
