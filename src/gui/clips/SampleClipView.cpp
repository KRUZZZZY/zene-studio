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

	// Clip gain, fade lengths and the fade shape (clip.set_gain / clip.set_fade).
	addClipEditsMenu(cm, m_clip);
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
	const SampleWindow window = m_clip->sampleWindow();
	const QString audio = QStringLiteral("%1:%2:%3").arg(reinterpret_cast<quintptr>(m_clip->sample().data()))
		.arg(window.sourceIn).arg(window.sourceOut);
	if (audio != m_transientAudioKey)
	{
		m_transientFrames = clipTransientFrames(*m_clip);
		m_transientAudioKey = audio;
		m_transientKey.clear();
	}
	std::size_t warp = 0;
	for (const WarpMarker& marker : m_clip->warpMarkers().all()) { warp = qHashMulti(warp, marker.sourceFrame, marker.offsetTicks); }
	const QString key = QStringLiteral("%1:%2:%3:%4").arg(m_clip->length().getTicks()).arg(Engine::getSong()->getTempo())
		.arg(m_clip->clipFramesPerTick()).arg(warp);
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
	ClipEdits edits = m_clip->clipEdits();
	if (m_fadeDrag == FadeHandle::In) { edits.fadeInTicks = m_fadeDragTicks; }
	if (m_fadeDrag == FadeHandle::Out) { edits.fadeOutTicks = m_fadeDragTicks; }
	return edits;
}

FadeHandle SampleClipView::fadeHandleUnder(const QPoint& pos)
{
	// A locked clip (or a clip of a locked track) has no fade to drag: clip.set_fade would refuse it (UX ux06 #25).
	if (EditLocks::instance().locks(m_clip)) { return FadeHandle::None; }
	return fadeHandleAt(m_clip->clipEdits(), m_clip->length().getTicks(), overlayFrame(0), pos);
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

void SampleClipView::mouseMoveEvent(QMouseEvent* me)
{
	if (m_fadeDrag == FadeHandle::None)
	{
		ClipView::mouseMoveEvent(me);
		// Over a fade handle the pointer shows that it drags sideways, as over a resize edge (UX ux06 #27).
		if (!me->buttons() && fadeHandleUnder(me->pos()) != FadeHandle::None) { setCursor(Qt::SizeHorCursor); }
		return;
	}
	// fade_in + fade_out must fit in the clip (clip.set_fade refuses more), so a fade stops where the other begins.
	const int clipTicks = m_clip->length().getTicks();
	const int other = m_fadeDrag == FadeHandle::In ? m_clip->clipEdits().fadeOutTicks : m_clip->clipEdits().fadeInTicks;
	m_fadeDragTicks = std::max(0, std::min(fadeTicksAt(m_fadeDrag, me->pos().x(), clipTicks, overlayFrame(0)), clipTicks - other));
	setNeedsUpdate(true);
	update();
}

void SampleClipView::mousePressEvent( QMouseEvent * _me )
{
	if (startFadeDrag(_me)) { return; }
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
			.reversed = sample.reversed()
		};

		m_sampleThumbnail.visualize(param, p);
	}
	// The gain envelope, the fades and their handles (SampleClipOverlays.cpp).
	const ClipOverlayFrame frame = overlayFrame(static_cast<int>(-m_paintPixmapXPosition));
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
