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
#include <cstdlib>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QTimer>
#include <QToolBar>
#include <QWheelEvent>

#include "ArrangementSections.h"
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

constexpr int LandmarkBand = 5;  //!< the sections' strip along the top
constexpr int MinViewport = 6;   //!< the narrowest the viewport frame is drawn, however far out it zooms
constexpr int WheelStep = 1;

/*! 0.5.0 theme 9: the overview carries the song's LANDMARKS - its sections (a coloured strip along the
 *  top, a boundary line at each start), its markers, the loop and the play position - and the part the
 *  arranger shows is a VIEWPORT that stands out: the rest of the song is dimmed and the viewport framed.
 *  The wheel zooms around the tick under the pointer; with the keyboard, Left/Right scroll an eighth of
 *  the view, Page Up/Down a whole view, Home/End go to the song's start/end and +/- zoom. */
class Navigator : public QWidget
{
public:
	Navigator(SongEditor* editor, QWidget* parent) :
		QWidget(parent),
		m_editor(editor)
	{
		setMinimumHeight(40);
		setFocusPolicy(Qt::StrongFocus);
		setAccessibleName(QObject::tr("Navigator"));
		setToolTip(QObject::tr("The whole song: click to centre the arranger there, drag to scroll, turn the wheel to zoom "
			"around the pointer; with the keyboard, arrows and Page Up/Down scroll, Home/End jump, +/- zoom"));
		setProperty("controlCommand", QStringLiteral("arrangement.scroll"));
		connect(editor->horizontalScroll(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
		connect(editor, &SongEditor::pixelsPerBarChanged, this, qOverload<>(&QWidget::update));
		auto* timer = new QTimer(this);  // clips come and go without a signal this strip could follow
		connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
		timer->start(500);
	}

protected:
	void paintEvent(QPaintEvent*) override
	{
		const tokens::Palette& pal = tokens::palette();
		QPainter p(this);
		p.fillRect(rect(), pal.panel);
		if (m_editor.isNull()) { return; }
		m_perTick = width() / static_cast<double>(spanTicks());
		paintSections(p);
		paintClips(p);
		paintMarkersAndLoop(p);
		p.setPen(QPen(pal.playhead, 1));
		const int play = xOf(arrangerzoom::playhead(m_editor));
		p.drawLine(play, 0, play, height());
		paintViewport(p);
		describe();
	}

	void mousePressEvent(QMouseEvent* event) override
	{
		if (event->button() == Qt::LeftButton) { scrollTo(event->position().x()); }
	}
	void mouseMoveEvent(QMouseEvent* event) override
	{
		if (event->buttons() & Qt::LeftButton) { scrollTo(event->position().x()); }
	}
	void wheelEvent(QWheelEvent* event) override
	{
		// The vertical wheel zooms around the tick under the pointer; a sideways (or empty) turn is not a
		// zoom out - it goes on to the parent.
		const int turn = event->angleDelta().y();
		if (turn == 0 || std::abs(event->angleDelta().x()) > std::abs(turn))
		{
			event->ignore();
			return;
		}
		event->accept();
		const auto anchor = static_cast<int>(event->position().x() / std::max(1, width()) * spanTicks());
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.zoom"),
			{{QStringLiteral("step"), turn > 0 ? WheelStep : -WheelStep}, {QStringLiteral("anchor_tick"), anchor}});
	}
	void keyPressEvent(QKeyEvent* event) override
	{
		if (!keyScrolls(event->key()) && !keyZooms(event->key())) { return QWidget::keyPressEvent(event); }
		event->accept();
	}

private:
	int xOf(tick_t tick) const { return static_cast<int>(tick * m_perTick); }

	void paintSections(QPainter& p) const
	{
		const tokens::Palette& pal = tokens::palette();
		for (const ArrangementSection& section : ArrangementSections::instance().sections())
		{
			const QColor colour = section.color.isEmpty() ? pal.selected : QColor(section.color);
			p.fillRect(QRect(xOf(section.start), 0, std::max(1, xOf(section.end) - xOf(section.start)), LandmarkBand), colour);
			p.setPen(QPen(pal.separator, 1));
			p.drawLine(xOf(section.start), 0, xOf(section.start), height());
		}
	}

	void paintClips(QPainter& p) const
	{
		const tokens::Palette& pal = tokens::palette();
		const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
		const int top = LandmarkBand + 1;
		const int rowHeight = std::clamp((height() - top - 2) / std::max<int>(1, static_cast<int>(tracks.size())), 1, 6);
		for (int row = 0; row < static_cast<int>(tracks.size()); ++row)
		{
			for (const Clip* clip : tracks[row]->getClips())
			{
				const int w = std::max(1, static_cast<int>(clip->length().getTicks() * m_perTick));
				p.fillRect(QRect(xOf(clip->startPosition().getTicks()), top + row * rowHeight, w, std::max(1, rowHeight - 1)), clipColour(clip, pal));
			}
		}
	}

	void paintMarkersAndLoop(QPainter& p) const
	{
		const tokens::Palette& pal = tokens::palette();
		for (const Marker& marker : ProjectMarkers::instance().markers())
		{
			p.setPen(QPen(marker.color.isEmpty() ? pal.accent : QColor(marker.color), 1));
			p.drawLine(xOf(marker.tick), 0, xOf(marker.tick), height());
		}
		const Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		if (!timeline.loopEnabled()) { return; }
		p.fillRect(QRect(xOf(timeline.loopBegin().getTicks()), height() - 3,
			std::max(1, xOf(timeline.loopEnd().getTicks()) - xOf(timeline.loopBegin().getTicks())), 3), pal.accent);
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

	//! What a screen reader says of it: the bars on screen of the song's.
	void describe()
	{
		const tick_t start = m_editor->currentPosition().getTicks();
		const QString text = QObject::tr("Showing bars %1 to %2 of %3").arg(start / TimePos::ticksPerBar() + 1)
			.arg((start + arrangerzoom::visibleTicks(m_editor)) / TimePos::ticksPerBar() + 1).arg(Engine::getSong()->length());
		if (accessibleDescription() != text) { setAccessibleDescription(text); }
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
		const tick_t song = (Engine::getSong()->length() + 1) * TimePos::ticksPerBar();
		return std::max<tick_t>(song, m_editor->currentPosition().getTicks() + arrangerzoom::visibleTicks(m_editor));
	}

	//! Centre the arranger on the tick under @a x.
	void scrollTo(double x)
	{
		if (m_editor.isNull()) { return; }
		const auto tick = static_cast<tick_t>(x / std::max(1, width()) * spanTicks()) - arrangerzoom::visibleTicks(m_editor) / 2;
		ControlRegistry::instance()->invoke(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), std::max<int>(0, tick)}});
	}

	QPointer<SongEditor> m_editor;
	double m_perTick = 1.0;
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
