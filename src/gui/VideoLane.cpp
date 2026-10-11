/*
 * VideoLane.cpp - the arranger's video lane: a strip of thumbnails along the video's span
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

#include "VideoLane.h"

#include <algorithm>

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "VideoFramePump.h"
#include "VideoReference.h"

namespace lmms::gui
{

namespace
{

const QSize ThumbnailBox(96, 54);  //!< what each thumbnail is decoded to fit; drawn into its tile

const VideoAttachment& video()
{
	return VideoReference::instance().attachment();
}

//! A tile's width: the lane's height at the video's aspect (16:9 while it is unknown).
int tileWidth(int height)
{
	const VideoAttachment& v = video();
	const int width = v.width > 0 && v.height > 0 ? height * v.width / v.height : height * 16 / 9;
	return std::max(8, width);
}

} // namespace

VideoLane::VideoLane(SongEditor* editor) :
	QWidget(editor),
	m_editor(editor),
	m_pump(new VideoFramePump(this))
{
	setObjectName(QStringLiteral("arrangerVideoLane"));
	setAccessibleName(tr("Video lane"));
	setAccessibleDescription(tr("The project's video along the song; click to move the play position there"));
	setFocusPolicy(Qt::NoFocus);
	if (auto* box = qobject_cast<QVBoxLayout*>(editor->layout()))
	{
		// Under the ruler lanes when they are there, else under the ruler itself.
		auto* lanes = editor->findChild<QWidget*>(QStringLiteral("arrangerRulerLanes"));
		const int above = box->indexOf(lanes != nullptr && box->indexOf(lanes) >= 0 ? lanes : editor->timeLine());
		box->insertWidget(above + 1, this);
	}
	editor->timeLine()->installEventFilter(this);
	connect(m_pump, &VideoFramePump::frameReady, this, [this](const QImage& image, double, int tag) { take(image, tag); });
	connect(&VideoReference::instance(), &VideoReference::changed, this, &VideoLane::reload);
	connect(editor, &SongEditor::pixelsPerBarChanged, this, qOverload<>(&QWidget::update));
	connect(editor->horizontalScroll(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
	reload();
}

int VideoLane::thumbnailsReady() const
{
	return static_cast<int>(std::count_if(m_thumbnails.begin(), m_thumbnails.end(), [](const QImage& i) { return !i.isNull(); }));
}

void VideoLane::reload()
{
	++m_generation;
	m_thumbnails = QVector<QImage>(Thumbnails);
	const bool attached = video().attached();
	setFixedHeight(attached ? LaneHeight : 0);
	setVisible(attached);
	if (attached) { ask(0); }
	else { m_pump->request(QString(), 0.0, ThumbnailBox, m_generation * Thumbnails - 1); }
	update();
	// The tracks move down or up once the layout settles: the play position line follows them.
	if (!m_editor.isNull()) { QTimer::singleShot(0, m_editor.data(), &SongEditor::updatePositionLine); }
}

void VideoLane::ask(int index)
{
	const double seconds = (index + 0.5) * video().duration / Thumbnails;
	m_pump->request(video().path, seconds, ThumbnailBox, m_generation * Thumbnails + index);
}

void VideoLane::take(const QImage& image, int tag)
{
	if (tag / Thumbnails != m_generation) { return; }  // asked for before the video changed
	const int index = tag % Thumbnails;
	m_thumbnails[index] = image;
	update();
	if (index + 1 < Thumbnails) { ask(index + 1); }  // one at a time, so the window's own pump is never starved
}

bool VideoLane::eventFilter(QObject* watched, QEvent* event)
{
	// The ruler repaints when the song's timing, its position or its loop changes - so does the strip.
	if (event->type() == QEvent::Paint) { update(); }
	return QWidget::eventFilter(watched, event);
}

int VideoLane::contentLeft() const
{
	return m_editor.isNull() ? 0 : m_editor->timeLine()->markerX(m_editor->currentPosition());
}

int VideoLane::xOf(tick_t tick) const
{
	return m_editor.isNull() ? 0 : m_editor->timeLine()->markerX(TimePos(tick));
}

tick_t VideoLane::tickAt(int x) const
{
	return m_editor->currentPosition().getTicks()
		+ static_cast<tick_t>((x - contentLeft()) * TimePos::ticksPerBar() / std::max(1.f, m_editor->pixelsPerBar()));
}

void VideoLane::paintStrip(QPainter& p, const QRect& span, int startX)
{
	const tokens::Palette& pal = tokens::palette();
	const int tile = tileWidth(span.height());
	const double duration = std::max(video().duration, 1e-6);
	// Tiles are laid from the video's start, so scrolling moves them with the music instead of re-cutting them.
	for (int x = startX + (span.left() - startX) / tile * tile; x <= span.right(); x += tile)
	{
		const double seconds = VideoReference::instance().secondsAt(tickAt(x + tile / 2));
		const int index = std::clamp(static_cast<int>(seconds / duration * Thumbnails), 0, Thumbnails - 1);
		const QRect cell(x, span.top(), tile - 1, span.height());
		if (m_thumbnails.at(index).isNull()) { p.fillRect(cell, pal.control); }
		else { p.drawImage(cell, m_thumbnails.at(index)); }
	}
}

void VideoLane::paintEvent(QPaintEvent*)
{
	m_span = {};
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.setFont(tokens::font(tokens::TextRole::Small));
	p.fillRect(rect(), pal.panel);
	p.fillRect(QRect(0, 0, contentLeft(), height()), pal.raised);
	p.setPen(pal.textMuted);
	p.drawText(QRect(4, 0, contentLeft() - 8, height()), Qt::AlignVCenter | Qt::AlignRight, tr("Video"));
	if (m_editor.isNull() || !video().attached()) { return; }
	const int startX = xOf(video().offset);
	const int endX = xOf(VideoReference::instance().endTick());
	const int left = std::max(contentLeft(), startX);
	const int right = std::min(width(), endX);
	if (right > left)
	{
		m_span = {left, right};
		p.setClipRect(QRect(left, 0, right - left, height()));
		paintStrip(p, QRect(left, 1, right - left, height() - 2), startX);
		p.setPen(pal.accent);
		p.drawRect(QRect(startX, 1, endX - startX - 1, height() - 3));
		p.setClipping(false);
	}
	p.setPen(pal.separator);
	p.drawLine(0, height() - 1, width(), height() - 1);
}

void VideoLane::mousePressEvent(QMouseEvent* event)
{
	const int x = event->pos().x();
	if (event->button() != Qt::LeftButton || x < m_span.first || x >= m_span.second) { return QWidget::mousePressEvent(event); }
	ControlRegistry::instance()->invoke(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), static_cast<qint64>(tickAt(x))}});
}

} // namespace lmms::gui
