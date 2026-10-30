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
#include <limits>

#include <QContextMenuEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "VideoFramePump.h"
#include "VideoReference.h"

namespace lmms::gui
{

namespace
{

const QSize ThumbnailBox(96, 54);  //!< what each thumbnail is decoded to fit; drawn into its tile

//! A tile's width: the lane's height at the clip's aspect (16:9 while it is unknown).
int tileWidth(int height, const VideoAttachment& clip)
{
	const int width = clip.width > 0 && clip.height > 0 ? height * clip.width / clip.height : height * 16 / 9;
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
	int ready = 0;
	for (const QVector<QImage>& row : m_thumbnails)
	{
		ready += static_cast<int>(std::count_if(row.begin(), row.end(), [](const QImage& image) { return !image.isNull(); }));
	}
	return ready;
}

void VideoLane::reload()
{
	m_generation = m_generation == std::numeric_limits<int>::max() ? 1 : m_generation + 1;
	const QVector<VideoAttachment>& clips = VideoReference::instance().clips();
	m_thumbnails = QVector<QVector<QImage>>(clips.size(), QVector<QImage>(Thumbnails));
	m_pending.clear();
	for (int clipIndex = 0; clipIndex < clips.size(); ++clipIndex)
	{
		for (int thumbnail = 0; thumbnail < Thumbnails; ++thumbnail) { m_pending.append({clipIndex, thumbnail}); }
	}
	const bool attached = !clips.isEmpty();
	setFixedHeight(attached ? LaneHeight : 0);
	setVisible(attached);
	if (attached) { askNext(); }
	else { m_pump->request(QString(), 0.0, ThumbnailBox, -1); }
	update();
	// The tracks move down or up once the layout settles: the play position line follows them.
	if (!m_editor.isNull()) { QTimer::singleShot(0, m_editor.data(), &SongEditor::updatePositionLine); }
}

void VideoLane::askNext()
{
	const QVector<VideoAttachment>& clips = VideoReference::instance().clips();
	if (m_pending.isEmpty()) { return; }
	m_asked = m_pending.takeFirst();
	const VideoAttachment& clip = clips[m_asked.first];
	const double seconds = (m_asked.second + 0.5) * clip.duration / Thumbnails;
	m_pump->request(clip.path, seconds, ThumbnailBox, m_generation);  // one in flight: the tag only says which clip set
}

void VideoLane::take(const QImage& image, int tag)
{
	if (tag != m_generation) { return; }  // asked for before the clip set changed
	m_thumbnails[m_asked.first][m_asked.second] = image;
	update();
	askNext();  // one at a time, so the window's own pump is never starved
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

int VideoLane::clipAtX(int x) const
{
	if (m_editor.isNull()) { return -1; }
	const QVector<VideoAttachment>& clips = VideoReference::instance().clips();
	for (int index = 0; index < clips.size(); ++index)
	{
		if (x >= xOf(clips[index].offset) && x < xOf(VideoReference::instance().endTick(clips[index]))) { return index; }
	}
	return -1;
}

void VideoLane::paintClippedStrip(QPainter& p, int clipIndex, const QRect& span, int startX)
{
	const QVector<VideoAttachment>& clips = VideoReference::instance().clips();
	if (clipIndex < 0 || clipIndex >= clips.size() || clipIndex >= m_thumbnails.size()) { return; }
	const VideoAttachment& clip = clips[clipIndex];
	const tokens::Palette& pal = tokens::palette();
	const int tile = tileWidth(span.height(), clip);
	const double duration = std::max(clip.duration, 1e-6);
	// Tiles are laid from the clip's start, so scrolling moves them with the music instead of re-cutting them.
	for (int x = startX + (span.left() - startX) / tile * tile; x <= span.right(); x += tile)
	{
		const double seconds = VideoReference::instance().secondsAt(tickAt(x + tile / 2), clip);
		const int index = std::clamp(static_cast<int>(seconds / duration * Thumbnails), 0, Thumbnails - 1);
		const QRect cell(x, span.top(), tile - 1, span.height());
		const QImage& image = m_thumbnails[clipIndex].at(index);
		if (image.isNull()) { p.fillRect(cell, pal.control); }
		else { p.drawImage(cell, image); }
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
	if (m_editor.isNull()) { return; }
	const QVector<VideoAttachment>& clips = VideoReference::instance().clips();
	QPair<int, int> drawn(width(), 0);
	for (int index = 0; index < clips.size(); ++index)
	{
		const int startX = xOf(clips[index].offset);
		const int endX = xOf(VideoReference::instance().endTick(clips[index]));
		const int left = std::max(contentLeft(), startX);
		const int right = std::min(width(), endX);
		if (right <= left) { continue; }
		drawn.first = std::min(drawn.first, left);
		drawn.second = std::max(drawn.second, right);
		p.setClipRect(QRect(left, 0, right - left, height()));
		paintClippedStrip(p, index, QRect(left, 1, right - left, height() - 2), startX);
		p.setPen(pal.accent);
		p.drawRect(QRect(startX, 1, endX - startX - 1, height() - 3));
		p.setClipping(false);
	}
	if (drawn.second > drawn.first) { m_span = drawn; }
	p.setPen(pal.separator);
	p.drawLine(0, height() - 1, width(), height() - 1);
}

void VideoLane::mousePressEvent(QMouseEvent* event)
{
	const int x = event->pos().x();
	if (event->button() != Qt::LeftButton || x < m_span.first || x >= m_span.second) { return QWidget::mousePressEvent(event); }
	m_pressX = x;
	const int index = clipAtX(x);
	if (index < 0)
	{
		seekTo(x);  // between clips: just the play position
		return;
	}
	// On a clip: a click (released where it was pressed) seeks, a drag moves the clip - decided on release.
	m_draggingClip = VideoReference::instance().clips()[index].id;
	m_dragStartOffset = VideoReference::instance().clips()[index].offset;
	setCursor(Qt::ClosedHandCursor);
}

void VideoLane::mouseReleaseEvent(QMouseEvent* event)
{
	if (m_draggingClip.isEmpty()) { return QWidget::mouseReleaseEvent(event); }
	const QString clip = m_draggingClip;
	m_draggingClip.clear();
	unsetCursor();
	const int delta = event->pos().x() - m_pressX;
	if (std::abs(delta) < DragThreshold)
	{
		seekTo(m_pressX);
		return;
	}
	const tick_t offset = std::max<tick_t>(0, m_dragStartOffset
		+ static_cast<tick_t>((delta * TimePos::ticksPerBar()) / std::max(1.f, m_editor->pixelsPerBar())));
	invokeOrWarn(QStringLiteral("video.set_offset"), {{QStringLiteral("clip"), clip}, {QStringLiteral("offset"), static_cast<qint64>(offset)}},
		tr("Video clip not moved"));
}

void VideoLane::seekTo(int x)
{
	ControlRegistry::instance()->invoke(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), static_cast<qint64>(tickAt(x))}});
}

void VideoLane::invokeOrWarn(const QString& command, const QJsonObject& args, const QString& title)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (!result.ok) { QMessageBox::warning(this, title, result.errorMessage); }
}

void VideoLane::addClipFromDialog(QWidget* parent, tick_t tick)
{
	const QString path = QFileDialog::getOpenFileName(parent, tr("Add Video Clip"),
		QFileInfo(Engine::getSong()->projectFileName()).absolutePath(), tr("Video files (*.mp4 *.mov *.mkv *.avi *.webm);;All files (*)"));
	if (path.isEmpty()) { return; }
	const ControlResult added = ControlRegistry::instance()->invoke(QStringLiteral("video.clip_add"),
		{{QStringLiteral("path"), path}, {QStringLiteral("offset"), static_cast<qint64>(tick)}});
	if (!added.ok) { QMessageBox::warning(parent, tr("Video clip not added"), added.errorMessage); }
}

void VideoLane::renderLoopRange()
{
	const Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
	const QString folder = QFileDialog::getExistingDirectory(this, tr("Render Video Frames To"),
		QFileInfo(Engine::getSong()->projectFileName()).absolutePath());
	if (folder.isEmpty()) { return; }
	invokeOrWarn(QStringLiteral("video.render_frames"), {{QStringLiteral("from"), static_cast<qint64>(timeline.loopBegin().getTicks())},
		{QStringLiteral("to"), static_cast<qint64>(timeline.loopEnd().getTicks())}, {QStringLiteral("out"), folder}},
		tr("Video frames not rendered"));
}

void VideoLane::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton || m_editor.isNull()) { return QWidget::mouseDoubleClickEvent(event); }
	if (event->pos().x() < contentLeft() || clipAtX(event->pos().x()) >= 0)
	{
		return QWidget::mouseDoubleClickEvent(event);
	}
	addClipFromDialog(this, std::max<tick_t>(0, tickAt(event->pos().x())));
}

void VideoLane::contextMenuEvent(QContextMenuEvent* event)
{
	if (m_editor.isNull() || event->pos().x() < contentLeft()) { return QWidget::contextMenuEvent(event); }
	const int index = clipAtX(event->pos().x());
	const QString id = index >= 0 ? VideoReference::instance().clips()[index].id : QString();
	const tick_t at = std::max<tick_t>(0, tickAt(event->pos().x()));
	const tick_t playhead = Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks();
	QMenu menu(this);
	menu.setObjectName(QStringLiteral("videoLaneMenu"));
	menu.addAction(tr("Add Video Clip Here..."), this, [this, at] { addClipFromDialog(this, at); });
	if (!id.isEmpty())
	{
		menu.addAction(tr("Move Clip Start to Play Position"), this, [this, id, playhead] {
			invokeOrWarn(QStringLiteral("video.set_offset"), {{QStringLiteral("clip"), id}, {QStringLiteral("offset"),
				static_cast<qint64>(playhead)}}, tr("Video clip not moved"));
		});
		menu.addAction(tr("Remove Video Clip"), this, [this, id] {
			invokeOrWarn(QStringLiteral("video.clip_remove"), {{QStringLiteral("clip"), id}}, tr("Video clip not removed"));
		});
	}
	menu.addSeparator();
	menu.addAction(tr("Render Video Frames of Loop Range..."), this, &VideoLane::renderLoopRange);
	menu.exec(event->globalPos());
}

} // namespace lmms::gui
