/*
 * ArrangerNavigator.cpp - the whole song in miniature under the arranger
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
#include "ArrangerNavigator.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include <QAbstractSlider>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QWheelEvent>

#include "ArrangementSections.h"
#include "ArrangerLandmarks.h"
#include "ArrangerZoom.h"
#include "AutomationClip.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MidiClip.h"
#include "ProjectMarkers.h"
#include "SampleClip.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

const char* const NavigatorName = "arrangerNavigator";

SongEditor* arrangerEditor()
{
	GuiApplication* g = getGUI();
	return g != nullptr && g->songEditor() != nullptr ? g->songEditor()->m_editor : nullptr;
}

QColor clipColour(const Clip* clip, const tokens::Palette& pal)
{
	if (clip->color().has_value()) { return *clip->color(); }
	if (dynamic_cast<const SampleClip*>(clip) != nullptr) { return pal.clipSample; }
	if (dynamic_cast<const MidiClip*>(clip) != nullptr) { return pal.clipMidi; }
	return dynamic_cast<const AutomationClip*>(clip) != nullptr ? pal.clipAutomation : pal.clipPattern;
}

constexpr int MinViewport = 6;   //!< the narrowest the viewport frame is drawn, however far out it zooms
constexpr int MinLabel = 14;     //!< a section narrower than this is left unnamed (its tooltip names it)
constexpr int MinRow = 2;        //!< the thinnest a track's row of clips gets; more tracks than fit share rows
constexpr int MaxRow = 6;
constexpr int LoopBand = 4;      //!< the loop's bracket along the bottom
constexpr int LandmarkHit = 4;   //!< pixels either side of a marker a pointer still names it

//! The landmark strip along the top: the sections' names in the small type, so it grows with the interface zoom.
int landmarkBand()
{
	return QFontMetrics(tokens::font(tokens::TextRole::Caption)).height() + 2;
}

//! How the navigator is used: its tooltip, after the landmark under the pointer.
QString usageText()
{
	return QObject::tr("Click to centre the arranger here, drag to scroll, turn the wheel to zoom; Alt-click or "
		"double-click a landmark to zoom to it; with the keyboard, arrows and Page Up/Down scroll, Home/End jump, +/- zoom");
}

//! Black or white, whichever reads on @a fill.
QColor inkOn(const QColor& fill)
{
	return fill.lightnessF() > 0.55 ? QColor(Qt::black) : QColor(Qt::white);
}

/*! 0.5.0 theme 9: the overview carries the song's LANDMARKS - its sections (named bands along the top, a boundary
 *  line at each start), its markers (flags), the loop (a bracket along the bottom) and the play position (a line
 *  with a head) - and the part the arranger shows is a VIEWPORT that stands out: the rest of the song is dimmed and
 *  the viewport framed. Wave 3: it never gets too small to read - its height is the landmark strip and a target's
 *  side (accessible mode makes both larger), and when there are more tracks than rows, tracks share a row; the
 *  tooltip names the landmark under the pointer, and Alt-click or a double-click zooms the arranger to it (Bitwig's
 *  Alt-click on a cue marker). The wheel zooms around the tick under the pointer, as continuously as the arranger's;
 *  with the keyboard, Left/Right scroll an eighth of the view, Page Up/Down a whole view, Home/End go to the song's
 *  start/end and +/- zoom. A screen reader finds a slider: the arranger's start, in ticks, of the song's, settable. */
class Navigator : public QAbstractSlider
{
public:
	Navigator(SongEditor* editor, QWidget* parent) :
		QAbstractSlider(parent),
		m_editor(editor)
	{
		setOrientation(Qt::Horizontal);
		setMinimumHeight(landmarkBand() + tokens::targetSize() + LoopBand + 2);
		setFocusPolicy(Qt::StrongFocus);
		setProperty("zeneFocusRing", true);
		setAccessibleName(QObject::tr("Navigator"));
		setToolTip(usageText());
		setProperty("controlCommand", QStringLiteral("arrangement.scroll"));
		connect(editor->horizontalScroll(), &QScrollBar::valueChanged, this, &Navigator::follow);
		connect(editor, &SongEditor::pixelsPerBarChanged, this, &Navigator::follow);
		auto* timer = new QTimer(this);  // clips come and go without a signal this strip could follow
		connect(timer, &QTimer::timeout, this, &Navigator::follow);
		timer->start(500);
		follow();
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const tokens::Palette& pal = tokens::palette();
		QPainter p(this);
		p.fillRect(rect(), pal.panel);
		if (m_editor.isNull()) { return; }
		m_perTick = width() / static_cast<double>(spanTicks());
		p.setFont(tokens::font(tokens::TextRole::Caption));
		paintSections(p);
		paintClips(p);
		paintMarkers(p);
		paintLoop(p);
		paintPlayhead(p);
		paintViewport(p);
	}

	//! A value set from outside (a screen reader, the slider's own keys) scrolls the arranger there.
	void sliderChange(SliderChange change) override
	{
		QAbstractSlider::sliderChange(change);
		if (change != SliderValueChange || m_following || m_editor.isNull()) { return; }
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), value()}});
	}

	bool event(QEvent* event) override
	{
		if (event->type() != QEvent::ToolTip || m_editor.isNull()) { return QAbstractSlider::event(event); }
		auto* help = static_cast<QHelpEvent*>(event);
		const landmarks::Landmark here = landmarkAt(help->pos().x());
		QToolTip::showText(help->globalPos(), here.kind == landmarks::Landmark::Kind::None ? usageText()
			: landmarks::describe(here) + QLatin1Char('\n') + usageText(), this);
		return true;
	}

	void mousePressEvent(QMouseEvent* event) override
	{
		if (event->button() != Qt::LeftButton) { return; }
		if (event->modifiers() & Qt::AltModifier) { zoomToLandmark(event->position().x()); }
		else { scrollTo(event->position().x()); }
	}
	void mouseDoubleClickEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::LeftButton) { zoomToLandmark(event->position().x()); }
	}
	void mouseMoveEvent(QMouseEvent* event) override
	{
		if ((event->buttons() & Qt::LeftButton) && !(event->modifiers() & Qt::AltModifier)) { scrollTo(event->position().x()); }
	}
	void wheelEvent(QWheelEvent* event) override
	{
		// The vertical wheel zooms around the tick under the pointer; a sideways (or empty) turn is not a
		// zoom out - it goes on to the parent.
		const int turn = event->angleDelta().y();
		if (turn == 0 || std::abs(event->angleDelta().x()) > std::abs(turn) || m_editor.isNull())
		{
			event->ignore();
			return;
		}
		event->accept();
		arrangerzoom::wheelZoom(m_editor, turn, event->modifiers() & Qt::ShiftModifier, tickAt(event->position().x()));
	}
	void keyPressEvent(QKeyEvent* event) override
	{
		if (m_editor.isNull() || (!keyScrolls(event->key()) && !keyZooms(event->key()))) { return QWidget::keyPressEvent(event); }
		event->accept();
	}

private:
	int xOf(tick_t tick) const { return static_cast<int>(tick * m_perTick); }
	tick_t tickAt(double x) const { return static_cast<tick_t>(std::max(0.0, x) / std::max(1, width()) * spanTicks()); }

	//! The slider's range and value, and what a screen reader says, from the arranger (not from a paint: a value
	//! set there would paint again).
	void follow()
	{
		if (m_editor.isNull()) { return; }
		const tick_t view = arrangerzoom::visibleTicks(m_editor);
		const tick_t start = m_editor->currentPosition().getTicks();
		m_following = true;
		setRange(0, static_cast<int>(std::max<tick_t>(start, spanTicks() - view)));
		setPageStep(static_cast<int>(std::max<tick_t>(1, view)));
		setSingleStep(static_cast<int>(std::max<tick_t>(1, view / 8)));
		setValue(static_cast<int>(start));
		m_following = false;
		// The view's end is exclusive: the last bar named is the one the view ends INSIDE.
		const tick_t lastTick = view > 0 ? start + view - 1 : start;
		const QString text = QObject::tr("Showing bars %1 to %2 of %3").arg(start / TimePos::ticksPerBar() + 1)
			.arg(lastTick / TimePos::ticksPerBar() + 1).arg(Engine::getSong()->length());
		if (accessibleDescription() != text) { setAccessibleDescription(text); }
		update();
	}

	landmarks::Landmark landmarkAt(double x) const
	{
		return landmarks::at(tickAt(x), static_cast<tick_t>(LandmarkHit / std::max(m_perTick, 1e-9)));
	}

	void paintSections(QPainter& p) const
	{
		const tokens::Palette& pal = tokens::palette();
		const int band = landmarkBand();
		for (const ArrangementSection& section : ArrangementSections::instance().sections())
		{
			const QColor colour = section.color.isEmpty() ? pal.selected : QColor(section.color);
			const QRect area(xOf(section.start), 0, std::max(1, xOf(section.end) - xOf(section.start)), band);
			p.fillRect(area, colour);
			p.setPen(QPen(pal.separator, 1));
			p.drawLine(area.left(), 0, area.left(), height());
			if (area.width() < MinLabel) { continue; }
			p.setPen(inkOn(colour));
			const QRect text = area.adjusted(tokens::space::xxs, 0, -tokens::space::xxs, 0);
			p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(section.name, Qt::ElideRight, text.width()));
		}
	}

	void paintClips(QPainter& p) const
	{
		const tokens::Palette& pal = tokens::palette();
		const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
		const int count = std::max<int>(1, static_cast<int>(tracks.size()));
		const int top = landmarkBand() + 1;
		const int room = std::max(1, height() - top - LoopBand - 1);
		const int rows = std::clamp(room / MinRow, 1, count);
		const int rowHeight = std::clamp(room / rows, 1, MaxRow);
		for (int track = 0; track < static_cast<int>(tracks.size()); ++track)
		{
			const int y = top + track * rows / count * rowHeight;
			for (const Clip* clip : tracks[track]->getClips())
			{
				const int w = std::max(1, static_cast<int>(clip->length().getTicks() * m_perTick));
				p.fillRect(QRect(xOf(clip->startPosition().getTicks()), y, w, std::max(1, rowHeight - 1)), clipColour(clip, pal));
			}
		}
	}

	void paintMarkers(QPainter& p) const
	{
		const tokens::Palette& pal = tokens::palette();
		for (const Marker& marker : ProjectMarkers::instance().markers())
		{
			const QColor colour = marker.color.isEmpty() ? pal.accent : QColor(marker.color);
			const int x = xOf(marker.tick);
			QPainterPath flag;  // a flag's shape, so a marker is not told apart by its colour alone
			flag.moveTo(x, 0);
			flag.lineTo(x + 5, 0);
			flag.lineTo(x, 5);
			flag.closeSubpath();
			p.fillPath(flag, colour);
			p.setPen(QPen(colour, 1));
			p.drawLine(x, 0, x, height());
		}
	}

	void paintLoop(QPainter& p) const
	{
		const Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		if (!timeline.loopEnabled()) { return; }
		// A bracket: the bar along the bottom and a tick up at each end.
		const int left = xOf(timeline.loopBegin().getTicks());
		const int right = std::max(left + 1, xOf(timeline.loopEnd().getTicks()));
		const QColor colour = tokens::palette().accent;
		p.fillRect(QRect(left, height() - 2, right - left, 2), colour);
		p.fillRect(QRect(left, height() - LoopBand - 2, 1, LoopBand + 2), colour);
		p.fillRect(QRect(right - 1, height() - LoopBand - 2, 1, LoopBand + 2), colour);
	}

	void paintPlayhead(QPainter& p) const
	{
		const QColor colour = tokens::palette().playhead;
		const int x = xOf(arrangerzoom::playhead(m_editor));
		p.setPen(QPen(colour, 1));
		p.drawLine(x, 0, x, height());
		QPainterPath head;  // a head on the line, so it reads apart from a marker
		head.moveTo(x - 4, 0);
		head.lineTo(x + 4, 0);
		head.lineTo(x, 5);
		head.closeSubpath();
		p.fillPath(head, colour);
	}

	void paintViewport(QPainter& p) const
	{
		const tokens::Palette& pal = tokens::palette();
		const int left = xOf(m_editor->currentPosition().getTicks());
		const int shown = std::max(MinViewport, static_cast<int>(arrangerzoom::visibleTicks(m_editor) * m_perTick));
		QColor dim = pal.base;
		dim.setAlpha(150);
		p.fillRect(QRect(0, 0, left, height()), dim);
		p.fillRect(QRect(left + shown, 0, width() - left - shown, height()), dim);
		p.setPen(QPen(hasFocus() ? pal.focusRing : pal.accent, 2));
		p.setBrush(Qt::NoBrush);
		p.drawRect(QRect(left + 1, 1, shown - 2, height() - 2));
	}

	bool keyScrolls(int key)
	{
		const tick_t view = arrangerzoom::visibleTicks(m_editor);
		const tick_t start = m_editor->currentPosition().getTicks();
		const tick_t song = static_cast<tick_t>(Engine::getSong()->length()) * TimePos::ticksPerBar();
		tick_t to = start;
		switch (key)
		{
		case Qt::Key_Left: to = start - view / 8; break;
		case Qt::Key_Right: to = start + view / 8; break;
		case Qt::Key_PageUp: to = start - view; break;
		case Qt::Key_PageDown: to = start + view; break;
		case Qt::Key_Home: to = 0; break;
		case Qt::Key_End: to = song - view; break;
		default: return false;
		}
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), std::max<int>(0, to)}});
		return true;
	}

	bool keyZooms(int key)
	{
		if (key != Qt::Key_Plus && key != Qt::Key_Minus && key != Qt::Key_Equal) { return false; }
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.zoom"),
			{{QStringLiteral("step"), key == Qt::Key_Minus ? -4 : 4}, {QStringLiteral("anchor"), QStringLiteral("view")}});
		return true;
	}

	//! The song's length with a bar to spare, or the end of what the arranger shows when that is further.
	tick_t spanTicks() const
	{
		const std::int64_t song = (static_cast<std::int64_t>(Engine::getSong()->length()) + 1) * TimePos::ticksPerBar();
		const std::int64_t viewEnd = static_cast<std::int64_t>(m_editor->currentPosition().getTicks()) + arrangerzoom::visibleTicks(m_editor);
		return static_cast<tick_t>(std::clamp(std::max(song, viewEnd), std::int64_t{0},
			static_cast<std::int64_t>(std::numeric_limits<tick_t>::max())));
	}

	//! Centre the arranger on the tick under @a x.
	void scrollTo(double x)
	{
		if (m_editor.isNull()) { return; }
		const tick_t tick = tickAt(x) - arrangerzoom::visibleTicks(m_editor) / 2;
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), std::max<int>(0, tick)}});
	}

	//! Fill the arranger with the landmark under @a x (arrangement.zoom's span); nothing where there is none.
	void zoomToLandmark(double x)
	{
		const landmarks::Landmark here = landmarkAt(x);
		if (here.kind == landmarks::Landmark::Kind::None || here.end <= here.start) { return; }
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.zoom"),
			{{QStringLiteral("start"), static_cast<int>(here.start)}, {QStringLiteral("end"), static_cast<int>(here.end)}});
	}

	QPointer<SongEditor> m_editor;
	double m_perTick = 1.0;
	bool m_following = false;
};

QToolBar* navigatorBar()
{
	GuiApplication* g = getGUI();
	return g != nullptr && g->songEditor() != nullptr ? g->songEditor()->findChild<QToolBar*>(QLatin1String(NavigatorName)) : nullptr;
}

ControlResult scroll(const QJsonObject& args)
{
	SongEditor* editor = arrangerEditor();
	if (editor == nullptr) { return noInterface(QStringLiteral("arrangement.scroll")); }
	editor->horizontalScroll()->setValue(std::max(0, args.value(QStringLiteral("position")).toInt()));
	return ControlResult::success({{QStringLiteral("position"), static_cast<int>(editor->currentPosition().getTicks())},
		{QStringLiteral("visible_ticks"), static_cast<int>(arrangerzoom::visibleTicks(editor))}});
}

ControlResult navigator(const QJsonObject& args)
{
	if (args.contains(QStringLiteral("visible")))
	{
		const bool visible = args.value(QStringLiteral("visible")).toBool();
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("navigator"), QString::number(visible ? 1 : 0));
		if (QToolBar* bar = navigatorBar()) { bar->setVisible(visible); }
	}
	return ControlResult::success({{QStringLiteral("visible"),
		ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("navigator")).toInt() != 0}});
}

} // namespace

void installArrangerNavigator(SongEditorWindow* window)
{
	if (window == nullptr || window->m_editor == nullptr || navigatorBar() != nullptr) { return; }
	auto* bar = new QToolBar(QObject::tr("Navigator"), window);
	bar->setObjectName(QLatin1String(NavigatorName));
	bar->setMovable(false);
	bar->addWidget(new Navigator(window->m_editor, bar));
	window->addToolBar(Qt::BottomToolBarArea, bar);
	bar->setVisible(ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("navigator")).toInt() != 0);
}

} // namespace lmms::gui

namespace lmms
{

void registerArrangerNavigatorCommands(ControlRegistry& registry)
{
	ControlCommand scrollCmd = guiCommand("arrangement", "scroll");
	scrollCmd.description = QStringLiteral("Scroll the arranger so it starts at 'position' (ticks) - the navigator's click. Reports the "
		"position it reached and how many ticks the arranger shows. Interface state; nothing in the project changes.");
	scrollCmd.argsSchema = control::objectSchema({{QStringLiteral("position"), control::integerProperty()}}, {QStringLiteral("position")});
	scrollCmd.resultSchema = control::objectProperty();
	scrollCmd.handler = gui::scroll;
	registry.registerCommand(scrollCmd);
	ControlCommand navigatorCmd = guiCommand("arrangement", "navigator");
	navigatorCmd.description = QStringLiteral("Show or hide the navigator under the arranger ('visible'), the whole song in miniature "
		"with the part on screen outlined. A preference (config ui/navigator); with no arguments it reports it.");
	navigatorCmd.argsSchema = control::objectSchema({{QStringLiteral("visible"), control::booleanProperty()}});
	navigatorCmd.resultSchema = control::objectProperty();
	navigatorCmd.handler = gui::navigator;
	registry.registerCommand(navigatorCmd);
}

} // namespace lmms
