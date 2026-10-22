/*
 * SampleClipView.cpp
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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
 
#include "SampleClipView.h"

#include "ClipEdits.h"

#include <algorithm>

#include <QApplication>
#include <QHashFunctions>
#include <QMenu>
#include <QPainter>

#include "ArrangementEdits.h"
#include "ClipEditsMenu.h"
#include "ClipTransients.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "EditLocks.h"
#include "FileDialog.h"
#include "FontHelper.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "AutomationEditor.h"
#include "embed.h"
#include "PathUtil.h"
#include "lmmsconfig.h"
#include "SampleClip.h"
#include "SampleThumbnail.h"
#include "Song.h"
#ifdef LMMS_HAVE_STEM_SPLIT
#include "StemSplitController.h"
#endif
#include "StringPairDrag.h"
#include "TrackContainerView.h"
#include "TrackView.h"

namespace lmms::gui
{



SampleClipView::SampleClipView( SampleClip * _clip, TrackView * _tv ) :
	ClipView( _clip, _tv ),
	m_clip( _clip ),
	m_paintPixmap(),
	m_paintPixmapXPosition(0)
{
	// update UI and tooltip
	updateSample();

	// track future changes of SampleClip
	connect(m_clip, SIGNAL(sampleChanged()), this, SLOT(updateSample()));

	connect(m_clip, SIGNAL(wasReversed()), this, SLOT(update()));

	setStyle( QApplication::style() );
}

void SampleClipView::updateSample()
{
	update();

	m_sampleThumbnail = SampleThumbnail{m_clip->m_sample};

	// set tooltip to filename so that user can see what sample this
	// sample-clip contains
	setToolTip(
		!m_clip->m_sample.sampleFile().isEmpty()
			? PathUtil::toAbsolute(m_clip->m_sample.sampleFile())
			: tr("Double-click to open sample")
	);
}




void SampleClipView::constructContextMenu(QMenu* cm)
{
	cm->addSeparator();

	/*contextMenu.addAction( embed::getIconPixmap( "record" ),
				tr( "Set/clear record" ),
						m_clip, SLOT(toggleRecord()));*/

	cm->addAction(
		embed::getIconPixmap("flip_x"),
		tr("Reverse sample"),
		this,
		SLOT(reverseSample())
	);

	cm->addAction(
		embed::getIconPixmap("automation_ghost_note"),
		tr("Set as ghost in automation editor"),
		this,
		SLOT(setAutomationGhost())
	);

	// Clip gain, the gain envelope, the polarity, fade lengths and the fade shape
	// (clip.set_gain / clip.gain_envelope_set / clip.set_polarity / clip.set_fade).
	addClipEditsMenu(cm, m_clip);
	// The warp mode the clip renders its rate changes with (warp.stretch).
	addClipWarpModeMenu(cm, m_clip);
	// Arm the clip to take the input under Record while playing (clip.set_record).
	addClipRecordAction(cm, m_clip);
	// Trim to the playhead and slip the content by a beat (clip.trim / clip.slip).
	addClipTrimActions(cm, m_clip);

#ifdef LMMS_HAVE_STEM_SPLIT
	cm->addAction(
		tr("Split to stems"),
		this,
		SLOT(splitToStems())
	);
#endif

}




#ifdef LMMS_HAVE_STEM_SPLIT
void SampleClipView::splitToStems()
{
	StemSplitController::instance()->splitClipToStems(m_clip);
}
#endif




void SampleClipView::dragEnterEvent( QDragEnterEvent * _dee )
{
	if( StringPairDrag::processDragEnterEvent( _dee,
					"samplefile,sampledata" ) == false )
	{
		ClipView::dragEnterEvent( _dee );
	}
}






void SampleClipView::dropEvent( QDropEvent * _de )
{
	if( StringPairDrag::decodeKey( _de ) == "samplefile" )
	{
		m_clip->setSampleFile( StringPairDrag::decodeValue( _de ) );
		_de->accept();
	}
	else if( StringPairDrag::decodeKey( _de ) == "sampledata" )
	{
		m_clip->setSampleBuffer(SampleBuffer::fromBase64(StringPairDrag::decodeValue(_de)));
		m_clip->updateLength();
		update();
		_de->accept();
	}
	else
	{
		ClipView::dropEvent( _de );
	}
}




float SampleClipView::pixelsPerTickNow()
{
	const float ppb = fixedClips() ? (parentWidget()->width() - 2 * BORDER_WIDTH) / (float) m_clip->length().getBar() : pixelsPerBar();
	const float nom = Engine::getSong()->getTimeSigModel().getNumerator();
	const float den = Engine::getSong()->getTimeSigModel().getDenominator();
	return ppb / (DefaultTicksPerBar * nom / den);
}

ClipOverlayFrame SampleClipView::overlayFrame(int xOrigin)
{
	const int spacing = BORDER_WIDTH + 1;
	return {pixelsPerTickNow(), xOrigin, rect().width(), spacing, height() - 2 * spacing};
}

const std::vector<int>& SampleClipView::transientTicks()
{
	// The analysis - the slow part - runs once per audio and window, not once per paint. The placement (the
	// clip's rate, tempo and warp map) only re-maps its frames to ticks: a marker edit moves every transient
	// after it (review L17 #6), and a tempo change no longer analyses the audio again (UX ux06 #31).
	// The analysed range is the clip's AUDIBLE window: a reversed clip sounds the MIRROR of its window
	// (Sample::render reverses the whole buffer), so the play direction is part of what was analysed. A
	// reversal therefore has to analyse the mirror again - the placement key below only re-maps the frames
	// the cache has, and those are the frames of the range the clip played BEFORE the reversal.
	const SampleWindow window = m_clip->audibleWindow();
	const QString audio = QStringLiteral("%1:%2:%3").arg(reinterpret_cast<quintptr>(m_clip->sample().data()))
		.arg(window.sourceIn).arg(window.sourceOut);
	if (audio != m_transientAudioKey)
	{
		m_transientFrames = clipTransientFrames(*m_clip);
		m_transientAudioKey = audio;
		m_transientKey.clear();
	}
	std::size_t warp = 0;
	// A cache key only, so any mix will do; qHashMulti is Qt 6 and the hosted Qt 5 job lacks it.
	const auto mix = [](std::size_t seed, std::size_t value) { return seed ^ (value + static_cast<std::size_t>(0x9e3779b9u) + (seed << 6) + (seed >> 2)); };
	for (const WarpMarker& marker : m_clip->warpMarkers().all())
	{
		warp = mix(mix(warp, static_cast<std::size_t>(marker.sourceFrame)), static_cast<std::size_t>(marker.offsetTicks));
	}
	const QString key = QStringLiteral("%1:%2:%3:%4:%5").arg(m_clip->length().getTicks()).arg(Engine::getSong()->getTempo())
		.arg(m_clip->clipFramesPerTick()).arg(warp).arg(m_clip->sample().reversed() ? 1 : 0);
	if (key != m_transientKey)
	{
		// As clipTransientTicks: ticks from the clip's start, where the clip plays each frame now.
		const tick_t origin = m_clip->startPosition().getTicks() + m_clip->startTimeOffset().getTicks();
		m_transientTicks.clear();
		for (const f_cnt_t frame : m_transientFrames) { m_transientTicks.push_back(m_clip->timelinePosAt(frame).getTicks() - origin); }
		m_transientKey = key;
	}
	return m_transientTicks;
}

ClipEdits SampleClipView::shownEdits() const
{
	if (m_gainDrag != GainDrag::None) { return m_dragEdits; }
	ClipEdits edits = m_clip->clipEdits();
	if (m_fadeDrag == FadeHandle::In) { edits.fadeInTicks = m_fadeDragTicks; }
	if (m_fadeDrag == FadeHandle::Out) { edits.fadeOutTicks = m_fadeDragTicks; }
	return withValidOffsetFades(edits, m_clip->length().getTicks());  // what renders: an offset fade is a pair
}

FadeHandle SampleClipView::fadeHandleUnder(const QPoint& pos)
{
	// A locked clip (or a clip of a locked track) has no fade to drag: clip.set_fade would refuse it (UX ux06 #25).
	if (EditLocks::instance().locks(m_clip)) { return FadeHandle::None; }
	return fadeHandleAt(withValidOffsetFades(m_clip->clipEdits(), m_clip->length().getTicks()), m_clip->length().getTicks(), overlayFrame(0), pos);
}

bool SampleClipView::startFadeDrag(QMouseEvent* me)
{
	// 0.5.0 L05 (ED-173): a plain press on a fade handle drags that fade (clip.set_fade on release).
	if (me->button() != Qt::LeftButton || me->modifiers() != Qt::NoModifier) { return false; }
	const FadeHandle handle = fadeHandleUnder(me->pos());
	if (handle == FadeHandle::None) { return false; }
	m_fadeDrag = handle;
	m_fadeDragTicks = handle == FadeHandle::In ? m_clip->clipEdits().fadeInTicks : m_clip->clipEdits().fadeOutTicks;
	return true;
}

bool SampleClipView::startGainDrag(QMouseEvent* me)
{
	// Q050-125: the same plain-press rule as a fade handle - a locked clip takes neither (the command
	// would refuse it), and a modified press keeps its inherited meaning (copy, unquantized, range).
	if (me->button() != Qt::LeftButton || me->modifiers() != Qt::NoModifier) { return false; }
	if (EditLocks::instance().locks(m_clip)) { return false; }
	const ClipEdits edits = withValidOffsetFades(m_clip->clipEdits(), m_clip->length().getTicks());
	const ClipOverlayFrame frame = overlayFrame(0);
	m_dragEdits = edits;
	const bool onHandle = clipGainHandleAt(edits, frame, me->pos());
	const int point = onHandle ? -1 : gainPointAt(edits, frame, me->pos());
	if (!onHandle && point < 0) { return false; }
	if (onHandle)
	{
		m_gainDrag = GainDrag::Handle;
		m_gainDragStartDb = gainLinearToDb(edits.gain);
		m_gainDragPressY = me->pos().y();
	}
	else
	{
		// A point drag also starts an ADD (a press on empty waveform where no point is, but inside the
		// clip's body, is deliberately not one: that is still the clip's move gesture). To add a point
		// the user Alt-clicks; to move one, plain-drags it.
		m_gainDrag = GainDrag::Point;
		m_gainDragPoint = point;
		m_gainDragStartDb = edits.envelope[point].gainDb;
	}
	return true;
}

void SampleClipView::finishGainDrag()
{
	if (m_gainDrag == GainDrag::None) { return; }
	const GainDrag kind = m_gainDrag;
	const ClipEdits after = m_dragEdits;
	m_gainDrag = GainDrag::None;
	m_gainDragPoint = -1;
	setNeedsUpdate(true);
	update();
	if (kind == GainDrag::Handle)
	{
		ControlRegistry::instance()->invoke(QStringLiteral("clip.set_gain"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
			{QStringLiteral("gain_db"), static_cast<double>(gainLinearToDb(after.gain))}});
		return;
	}
	QJsonArray points;
	for (int i = 0; i < gainPointCount(after); ++i)
	{
		points.append(QJsonObject{{QStringLiteral("tick"), after.envelope[i].tick},
			{QStringLiteral("gain_db"), static_cast<double>(after.envelope[i].gainDb)}});
	}
	// The envelope command replaces the WHOLE list, which is what the drag previewed: `point` (the
	// index the gesture started on) is not part of the wire format, so the sorted list is the edit.
	ControlRegistry::instance()->invoke(QStringLiteral("clip.gain_envelope_set"),
		{{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("points"), points}});
}

float SampleClipView::gainAtPixel(int x)
{
	// The effective multiplier the waveform is drawn with at @a x: the drawn edits' curve - clip gain,
	// envelope and fades - times the polarity sign, both of which the AUDIO applies. The pixel is
	// converted back to a tick from the clip's start, so the drawing and the render agree.
	const ClipOverlayFrame frame = overlayFrame(0);
	const tick_t length = std::max<tick_t>(1, m_clip->length().getTicks());
	const double tick = std::clamp(static_cast<double>(x - frame.xOrigin) / std::max(0.001f, frame.pixelsPerTick),
		0.0, static_cast<double>(length));
	const ClipEdits edits = shownEdits();
	return clipGainCurveAt(edits, length, tick) * edits.polaritySign();
}

void SampleClipView::mouseMoveEvent(QMouseEvent* me)
{
	if (m_gainDrag != GainDrag::None)
	{
		const ClipOverlayFrame frame = overlayFrame(0);
		if (m_gainDrag == GainDrag::Handle)
		{
			// The handle follows the pointer: its level moves by the drag's own distance from the press,
			// not by where on the line the press landed.
			const float db = clipGainDraggedDb(m_gainDragStartDb, me->pos().y() - m_gainDragPressY, frame);
			m_dragEdits.gain = gainDbToLinear(std::clamp(db, -60.f, 24.f));
		}
		else
		{
			// A point moves in both axes: its tick from the pointer's x, its gain from the pointer's y -
			// the envelope's own -60..+24 scale, so the drawn dot sits under the pointer.
			const int clipTicks = m_clip->length().getTicks();
			const int tick = std::clamp(gainTickAtX(me->pos().x(), frame), 0, clipTicks);
			const float db = gainDbAtY(me->pos().y(), frame);
			if (!withGainPoint(&m_dragEdits, m_gainDragPoint, tick, db, clipTicks))
			{
				// Out of range or full: the drag keeps showing the last accepted position.
				return;
			}
			// withGainPoint re-sorted the envelope, so track the point by its new index.
			for (int i = 0; i < m_dragEdits.envelopeCount; ++i)
			{
				if (m_dragEdits.envelope[i].tick == tick && m_dragEdits.envelope[i].gainDb == db) { m_gainDragPoint = i; break; }
			}
		}
		setNeedsUpdate(true);
		update();
		return;
	}
	if (m_fadeDrag == FadeHandle::None)
	{
		ClipView::mouseMoveEvent(me);
		// Over a fade handle the pointer shows that it drags sideways, as over a resize edge (UX ux06 #27);
		// over the clip-gain handle (or a point, with Alt held) it shows that it drags a level.
		if (!me->buttons())
		{
			if (fadeHandleUnder(me->pos()) != FadeHandle::None) { setCursor(Qt::SizeHorCursor); }
			else if (!EditLocks::instance().locks(m_clip)) { setCursor(clipGainHandleAt(shownEdits(), overlayFrame(0), me->pos()) ? Qt::SizeVerCursor
				: Qt::PointingHandCursor); }
		}
		return;
	}
	// fade_in + fade_out must fit in the clip (clip.set_fade refuses more), so a fade stops where the other begins.
	const int clipTicks = m_clip->length().getTicks();
	// With an offset fade (a crossfade into a contained clip) the ramps sit apart, each only bounded by the clip.
	const ClipEdits edits = withValidOffsetFades(m_clip->clipEdits(), clipTicks);
	const bool offsets = edits.fadeInOffsetTicks != 0 || edits.fadeOutOffsetTicks != 0;
	const int offset = m_fadeDrag == FadeHandle::In ? edits.fadeInOffsetTicks : edits.fadeOutOffsetTicks;
	const int other = m_fadeDrag == FadeHandle::In ? edits.fadeOutTicks : edits.fadeInTicks;
	const int room = offsets ? clipTicks - offset : clipTicks - other;
	m_fadeDragTicks = std::max(0, std::min(fadeTicksAt(m_fadeDrag, me->pos().x(), clipTicks, overlayFrame(0), offset), room));
	setNeedsUpdate(true);
	update();
}

void SampleClipView::mousePressEvent( QMouseEvent * _me )
{
	if (startFadeDrag(_me)) { return; }
	// Q050-125: Alt+click on the waveform adds an envelope point there (and starts dragging it); a plain
	// press on a point or on the clip-gain handle takes that. A plain press anywhere else is still the
	// clip's move gesture, which is why this runs after the fade handles and before ClipView's press.
	if (_me->button() == Qt::LeftButton && _me->modifiers() == Qt::AltModifier && !EditLocks::instance().locks(m_clip))
	{
		const ClipOverlayFrame frame = overlayFrame(0);
		const ClipEdits edits = withValidOffsetFades(m_clip->clipEdits(), m_clip->length().getTicks());
		const int tick = std::clamp(gainTickAtX(_me->pos().x(), frame), 0, m_clip->length().getTicks());
		const float db = gainDbAtY(_me->pos().y(), frame);
		m_dragEdits = edits;
		if (withGainPoint(&m_dragEdits, gainPointCount(edits), tick, db, m_clip->length().getTicks()))
		{
			m_gainDrag = GainDrag::Point;
			m_gainDragPoint = gainPointCount(edits);
			m_gainDragStartDb = db;
			setNeedsUpdate(true);
			update();
			return;
		}
	}
	if (startGainDrag(_me)) { return; }
	if( _me->button() == Qt::LeftButton &&
		_me->modifiers() & Qt::ControlModifier &&
		_me->modifiers() & Qt::ShiftModifier )
	{
		m_clip->toggleRecord();
	}
	else
	{
		if( _me->button() == Qt::MiddleButton && _me->modifiers() == Qt::ControlModifier )
		{
			auto sClip = dynamic_cast<SampleClip*>(getClip());
			if( sClip )
			{
				sClip->updateTrackClips();
			}
		}
		ClipView::mousePressEvent( _me );
	}
}




void SampleClipView::mouseReleaseEvent(QMouseEvent *_me)
{
	if (m_gainDrag != GainDrag::None)
	{
		finishGainDrag();
		return;
	}
	if (m_fadeDrag != FadeHandle::None)
	{
		const QString key = m_fadeDrag == FadeHandle::In ? QStringLiteral("fade_in") : QStringLiteral("fade_out");
		m_fadeDrag = FadeHandle::None;
		ControlRegistry::instance()->invoke(QStringLiteral("clip.set_fade"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
			{key, m_fadeDragTicks}});
		setNeedsUpdate(true);
		update();
		return;
	}
	if( _me->button() == Qt::MiddleButton && !_me->modifiers() )
	{
		auto sClip = dynamic_cast<SampleClip*>(getClip());
		if( sClip )
		{
			sClip->playbackPositionChanged();
		}
	}
	ClipView::mouseReleaseEvent( _me );
}




void SampleClipView::mouseDoubleClickEvent( QMouseEvent * )
{
	if (m_trackView->trackContainerView()->knifeMode()) { return; }

	const QString selectedAudioFile = FileDialog::openAudioFile();

	if (selectedAudioFile.isEmpty()) { return; }
	
	if (!m_clip->hasSampleFileLoaded(selectedAudioFile))
	{
		auto sampleBuffer = SampleBuffer::fromFile(selectedAudioFile);
		if (sampleBuffer != SampleBuffer::emptyBuffer())
		{
			m_clip->setSampleBuffer(sampleBuffer);
		}
	}
	m_clip->updateLength();
}




void SampleClipView::paintEvent( QPaintEvent * pe )
{
	QPainter painter( this );

	if( !needsUpdate() )
	{
		painter.drawPixmap(m_paintPixmapXPosition, 0, m_paintPixmap);
		return;
	}

	setNeedsUpdate( false );

	const auto trackViewWidth = getTrackView()->rect().width();

	// Use the clip's height to avoid artifacts when rendering while something else is overlaying the clip.
	const auto viewPortRect = QRect(0, 0, trackViewWidth * 2, rect().height());

	m_paintPixmapXPosition = std::max(0, pe->rect().x() - trackViewWidth);

	if (m_paintPixmap.isNull() || m_paintPixmap.size() != viewPortRect.size())
	{
		m_paintPixmap = QPixmap(viewPortRect.size());
	}

	QPainter p( &m_paintPixmap );

	bool muted = m_clip->getTrack()->isMuted() || m_clip->isMuted();
	bool selected = isSelected();

	QLinearGradient lingrad(0, 0, 0, height());
	QColor c = painter.background().color();
	if (muted) { c = c.darker(150); }
	if (selected) { c = c.darker(150); }

	lingrad.setColorAt( 1, c.darker( 300 ) );
	lingrad.setColorAt( 0, c );

	// paint a black rectangle under the clip to prevent glitches with transparent backgrounds
	p.fillRect( rect(), QColor( 0, 0, 0 ) );

	if( gradient() )
	{
		p.fillRect( rect(), lingrad );
	}
	else
	{
		p.fillRect( rect(), c );
	}

	auto clipColor = m_clip->color().value_or(m_clip->getTrack()->color().value_or(painter.pen().brush().color()));

	p.setPen(clipColor);

	if (muted)
	{
		QColor penColor = p.pen().brush().color();
		penColor.setHsv(penColor.hsvHue(), penColor.hsvSaturation() / 4, penColor.value());
		p.setPen(penColor.darker(250));
	}
	if (selected)
	{
		p.setPen(p.pen().brush().color().darker(150));
	}

	const int spacing = BORDER_WIDTH + 1;
	const float ppb = fixedClips() ?
			( parentWidget()->width() - 2 * BORDER_WIDTH )
					/ (float) m_clip->length().getBar() :
								pixelsPerBar();

	float nom = Engine::getSong()->getTimeSigModel().getNumerator();
	float den = Engine::getSong()->getTimeSigModel().getDenominator();
	float ticksPerBar = DefaultTicksPerBar * nom / den;
	float offsetStart = m_clip->startTimeOffset() / ticksPerBar * pixelsPerBar();
	float sampleLength = m_clip->sampleLength() * ppb / ticksPerBar;

	const auto& sample = m_clip->m_sample;

	const auto sampleRextX = static_cast<int>(offsetStart) - m_paintPixmapXPosition;

	if (sample.sampleSize() > 0)
	{
		const auto param = SampleThumbnail::VisualizeParameters{
			.sampleRect = QRect(sampleRextX, spacing, sampleLength, height() - spacing),
			.viewportRect = viewPortRect,
			.amplification = sample.amplification(),
			.reversed = sample.reversed(),
			// Q050-125: the drawn amplitude follows the effective gain (clip gain, envelope, fades)
			// and the polarity sign, so the waveform shows the level the clip renders at.
			.gainAt = [this, sampleRextX, sampleLength](float t) {
				const int x = sampleRextX + static_cast<int>(t * sampleLength);
				return gainAtPixel(x);
			}
		};

		m_sampleThumbnail.visualize(param, p);
	}
	// The gain envelope, the clip-gain line and handle, the fades and their handles (SampleClipOverlays.cpp).
	const ClipOverlayFrame frame = overlayFrame(static_cast<int>(-m_paintPixmapXPosition));
	paintClipGain(p, shownEdits(), frame, textColor());  // Q050-125: the level line and its drag handle
	paintGainEnvelope(p, shownEdits(), frame, textColor());
	paintFades(p, shownEdits(), m_clip->length().getTicks(), frame, textColor());
	if (arrangement::modes().showTransients) { paintTransients(p, transientTicks(), frame, textColor()); }
	paintSnapPoint(p, shownEdits(), frame, textColor());  // ED-022

	QString name = PathUtil::cleanName(m_clip->m_sample.sampleFile());
	paintTextLabel(name, p);

	// disable antialiasing for borders, since its not needed
	p.setRenderHint( QPainter::Antialiasing, false );

	// inner border
	p.setPen( c.lighter( 135 ) );
	p.drawRect(
		-m_paintPixmapXPosition + 1,
		1,
		rect().right() - BORDER_WIDTH,
		rect().bottom() - BORDER_WIDTH );

	// outer border
	p.setPen( c.darker( 200 ) );
	p.drawRect(-m_paintPixmapXPosition, 0, rect().right(), rect().bottom());

	// draw the 'muted' pixmap only if the clip was manually muted
	if( m_clip->isMuted() )
	{
		const int spacing = BORDER_WIDTH;
		const int size = 14;
		p.drawPixmap( spacing, height() - ( size + spacing ),
			embed::getIconPixmap( "muted", size, size ) );
	}

	if ( m_marker )
	{
		p.setPen(markerColor());
		p.drawLine(m_markerPos, rect().bottom(), m_markerPos, rect().top());
	}
	// A clip armed to record (clip.set_record) says so until its take lands: recording a sample
	// track works since R2.2 / BUGS_FOUND 11.8 and 11.17, which is why upstream's badge is back.
	if( m_clip->isRecord() )
	{
		p.setFont( adjustedToPixelSize( p.font(), SMALL_FONT_SIZE ) );

		p.setPen( textShadowColor() );
		p.drawText( 10, p.fontMetrics().height()+1, "Rec" );
		p.setPen( textColor() );
		p.drawText( 9, p.fontMetrics().height(), "Rec" );

		p.setBrush( QBrush( textColor() ) );
		p.drawEllipse( 4, 5, 4, 4 );
	}

	p.end();

	painter.drawPixmap(m_paintPixmapXPosition, 0, m_paintPixmap);
}




void SampleClipView::reverseSample()
{
	m_clip->m_sample.setReversed(!m_clip->m_sample.reversed());
	// The direction decides WHICH audio the clip plays, so the analysed range moves with it: the
	// analysis cache is dropped and the next paint analyses the new audible window (the mirror of
	// the window for a reversed clip). Without this the overlay kept the transients of the range
	// the clip played before the reversal, and only re-mapped them.
	m_transientAudioKey.clear();
	m_transientKey.clear();
	m_clip->setStartTimeOffset(m_clip->length() - m_clip->startTimeOffset() - m_clip->sampleLength());
	Engine::getSong()->setModified();
	update();
}



void SampleClipView::setAutomationGhost()
{
	auto aEditor = gui::getGUI()->automationEditor();
	aEditor->setGhostSample(m_clip);
	gui::getGUI()->mainWindow()->revealPanel(QStringLiteral("automation"));
}

} // namespace lmms::gui
