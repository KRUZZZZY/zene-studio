/*
 * ArrangerRulerLanes.cpp - the arranger's sections, markers and tempo, each in a lane of its own
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

#include "ArrangerRulerLanes.h"

#include <algorithm>
#include <array>

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include "ArrangementSections.h"
#include "ChordTrack.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "ImportDetectionDsp.h"
#include "ProjectMarkers.h"
#include "Song.h"
#include "SongEditor.h"
#include "TempoMap.h"
#include "TimeLineWidget.h"

namespace lmms::gui
{

namespace
{

constexpr std::array<int, ArrangerRulerLanes::LaneCount> LaneHeights{16, 16, 20, 18};
constexpr int ChordPollMs = 250;  //!< how often the chord lane looks at the chord track's revision
constexpr int MarkerHit = 6;     //!< pixels either side of a marker's line a click still takes
constexpr int LabelGap = 4;      //!< pixels kept between a label and the next object
constexpr int MinLabel = 14;     //!< narrower than this, a label is left out (its tooltip still has it)

const QString& preferenceName()
{
	static const QString name = QStringLiteral("rulerlanes");
	return name;
}

//! The shown lanes, by name: every lane while the preference was never set.
QStringList shownNames()
{
	const QString value = ConfigManager::inst()->value(QStringLiteral("ui"), preferenceName(), QStringLiteral("*"));
	return value == QStringLiteral("*") ? ArrangerRulerLanes::laneNames() : value.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

//! Black or white, whichever reads on @a fill.
QColor inkOn(const QColor& fill)
{
	return fill.lightnessF() > 0.55 ? QColor(Qt::black) : QColor(Qt::white);
}

QString laneTitle(int lane)
{
	static const std::array<const char*, ArrangerRulerLanes::LaneCount> titles{
		QT_TRANSLATE_NOOP("lmms::gui::ArrangerRulerLanes", "Sections"), QT_TRANSLATE_NOOP("lmms::gui::ArrangerRulerLanes", "Markers"),
		QT_TRANSLATE_NOOP("lmms::gui::ArrangerRulerLanes", "Tempo"), QT_TRANSLATE_NOOP("lmms::gui::ArrangerRulerLanes", "Chords")};
	return ArrangerRulerLanes::tr(titles[lane]);
}

ArrangerRulerLanes* lanesWidget()
{
	GuiApplication* g = getGUI();
	return g != nullptr && g->songEditor() != nullptr ? g->songEditor()->m_editor->findChild<ArrangerRulerLanes*>() : nullptr;
}

//! The lowest and highest tempo of @a map, the song's @a global tempo among them.
std::pair<int, int> tempoRange(const TempoMap& map, int global)
{
	int low = global;
	int high = global;
	for (const TempoMapEvent& event : map.all())
	{
		if (event.hasTempo) { low = std::min(low, event.tempo); high = std::max(high, event.tempo); }
	}
	return {low, high};
}

QString markerName(const Marker& marker)
{
	return marker.name.isEmpty() ? ArrangerRulerLanes::tr("Marker") : marker.name;
}

const ChordTrack& chordTrack()
{
	return Engine::getSong()->chordTrack();
}

//! "C Major": the chord's root and its name, as the lane labels it.
QString chordName(const ChordEvent& event)
{
	return QStringLiteral("%1 %2").arg(QString::fromLatin1(detection::pitchClassName(event.root)), event.chord);
}

} // namespace

ArrangerRulerLanes::ArrangerRulerLanes(SongEditor* editor) :
	QWidget(editor),
	m_editor(editor)
{
	setObjectName(QStringLiteral("arrangerRulerLanes"));
	setAccessibleName(tr("Ruler lanes"));
	setAccessibleDescription(tr("Sections, markers and tempo along the song; click a section to select it, a marker to jump there"));
	setFocusPolicy(Qt::NoFocus);
	setMouseTracking(true);
	if (auto* box = qobject_cast<QVBoxLayout*>(editor->layout())) { box->insertWidget(box->indexOf(editor->timeLine()) + 1, this); }
	editor->timeLine()->installEventFilter(this);
	connect(&ProjectMarkers::instance(), &ProjectMarkers::changed, this, qOverload<>(&QWidget::update));
	connect(editor, &SongEditor::pixelsPerBarChanged, this, qOverload<>(&QWidget::update));
	connect(editor->horizontalScroll(), &QScrollBar::valueChanged, this, qOverload<>(&QWidget::update));
	auto* chordPoll = new QTimer(this);
	connect(chordPoll, &QTimer::timeout, this, &ArrangerRulerLanes::followChordTrack);
	chordPoll->start(ChordPollMs);
	relayout();
}

const QStringList& ArrangerRulerLanes::laneNames()
{
	static const QStringList names{QStringLiteral("sections"), QStringLiteral("markers"), QStringLiteral("tempo"),
		QStringLiteral("chords")};
	return names;
}

bool ArrangerRulerLanes::laneShown(Lane lane)
{
	return shownNames().contains(laneNames().at(static_cast<int>(lane)));
}

bool ArrangerRulerLanes::laneDrawn(Lane lane)
{
	return laneShown(lane) && (lane != Lane::Chords || !chordTrack().empty());
}

void ArrangerRulerLanes::followChordTrack()
{
	if (chordTrack().revision() == m_chordRevision) { return; }
	m_chordRevision = chordTrack().revision();
	if (laneDrawn(Lane::Chords) != m_chordsDrawn) { relayout(); } else { update(); }
}

void ArrangerRulerLanes::setLaneShown(Lane lane, bool shown)
{
	QStringList names;
	for (int i = 0; i < LaneCount; ++i)
	{
		const bool on = i == static_cast<int>(lane) ? shown : laneShown(static_cast<Lane>(i));
		if (on) { names << laneNames().at(i); }
	}
	ConfigManager::inst()->setValue(QStringLiteral("ui"), preferenceName(), names.join(QLatin1Char(',')));
}

void ArrangerRulerLanes::relayout()
{
	int height = 0;
	for (int i = 0; i < LaneCount; ++i) { height += laneDrawn(static_cast<Lane>(i)) ? LaneHeights[i] : 0; }
	m_chordsDrawn = laneDrawn(Lane::Chords);
	setFixedHeight(std::max(height, 0));
	setVisible(height > 0);
	update();
	// The tracks move up or down once the layout settles: the play position line follows them.
	if (!m_editor.isNull()) { QTimer::singleShot(0, m_editor.data(), &SongEditor::updatePositionLine); }
}

QRect ArrangerRulerLanes::laneRect(Lane lane) const
{
	int top = 0;
	for (int i = 0; i < LaneCount; ++i)
	{
		if (!laneDrawn(static_cast<Lane>(i))) { continue; }
		if (i == static_cast<int>(lane)) { return QRect(0, top, width(), LaneHeights[i]); }
		top += LaneHeights[i];
	}
	return QRect();
}

bool ArrangerRulerLanes::eventFilter(QObject* watched, QEvent* event)
{
	// The ruler repaints when the song's timing, its position or its loop changes - so do the lanes.
	if (event->type() == QEvent::Paint) { update(); }
	return QWidget::eventFilter(watched, event);
}

int ArrangerRulerLanes::contentLeft() const
{
	return m_editor.isNull() ? 0 : m_editor->timeLine()->markerX(m_editor->currentPosition());
}

int ArrangerRulerLanes::xOf(tick_t tick) const
{
	return m_editor.isNull() ? 0 : m_editor->timeLine()->markerX(TimePos(tick));
}

tick_t ArrangerRulerLanes::tickAt(int x) const
{
	return m_editor->currentPosition().getTicks()
		+ static_cast<tick_t>((x - contentLeft()) * TimePos::ticksPerBar() / std::max(1.f, m_editor->pixelsPerBar()));
}

void ArrangerRulerLanes::paintSections(QPainter& p, const QRect& lane) const
{
	const tokens::Palette& pal = tokens::palette();
	for (const ArrangementSection& section : ArrangementSections::instance().sections())
	{
		const int left = std::max(contentLeft(), xOf(section.start));
		const int right = std::min(width(), xOf(section.end));
		if (right <= left) { continue; }
		QColor band = section.color.isEmpty() ? pal.selected : QColor(section.color);
		band.setAlpha(200);
		const QRect area(left, lane.top() + 1, right - left - 1, lane.height() - 2);
		p.fillRect(area, band);
		p.setPen(inkOn(band));
		const QString label = p.fontMetrics().elidedText(section.name, Qt::ElideRight, area.width() - 2 * LabelGap);
		if (area.width() >= MinLabel) { p.drawText(area.adjusted(LabelGap, 0, -LabelGap, 0), Qt::AlignVCenter | Qt::AlignLeft, label); }
	}
}

void ArrangerRulerLanes::paintMarkers(QPainter& p, const QRect& lane)
{
	const tokens::Palette& pal = tokens::palette();
	const QVector<Marker>& markers = ProjectMarkers::instance().markers();
	for (int i = 0; i < markers.size(); ++i)
	{
		const int x = xOf(markers.at(i).tick);
		if (x < contentLeft() || x >= width()) { continue; }
		const QColor colour = markers.at(i).color.isEmpty() ? pal.accent : QColor(markers.at(i).color);
		QPainterPath flag;
		flag.moveTo(x, lane.top());
		flag.lineTo(x + 7, lane.top());
		flag.lineTo(x, lane.top() + 7);
		flag.closeSubpath();
		p.fillPath(flag, colour);
		p.setPen(QPen(colour, 1));
		p.drawLine(x, lane.top(), x, lane.bottom());
		// The name gets the room up to the next marker, and no more: labels never overwrite each other.
		const int next = i + 1 < markers.size() ? xOf(markers.at(i + 1).tick) : width();
		const int room = next - x - 9 - LabelGap;
		if (room >= MinLabel)
		{
			p.setPen(pal.text);
			p.drawText(QRect(x + 9, lane.top(), room, lane.height()), Qt::AlignVCenter | Qt::AlignLeft,
				p.fontMetrics().elidedText(markerName(markers.at(i)), Qt::ElideRight, room));
		}
		m_drawn.append(x);
	}
}

void ArrangerRulerLanes::paintTempo(QPainter& p, const QRect& lane) const
{
	const tokens::Palette& pal = tokens::palette();
	const TempoMap& map = Engine::getSong()->tempoMap().map();
	const int global = Engine::getSong()->getTempo();
	p.setPen(pal.textMuted);
	if (!map.active() || map.empty())
	{
		p.drawText(lane.adjusted(contentLeft() + LabelGap, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, tr("%1 BPM (no tempo map)").arg(global));
		return;
	}
	const auto [low, high] = tempoRange(map, global);
	// ED-050's curve - holds flat, ramps slope - over the lane, with each tempo change's value beside it.
	const double perTick = (xOf(TimePos::ticksPerBar()) - xOf(0)) / static_cast<double>(TimePos::ticksPerBar());
	if (perTick <= 0.0) { return; }
	const int band = lane.height() - 4;
	QPolygonF curve;
	for (int x = contentLeft(); x <= width(); x += 2)
	{
		const double tempo = map.exactTempoAtTick(static_cast<tick_t>(std::max(0.0, (x - xOf(0)) / perTick)), global);
		curve << QPointF(x, lane.bottom() - 2 - (high > low ? (tempo - low) / (high - low) : 0.5) * band);
	}
	p.setPen(QPen(pal.play, 1.25));
	p.drawPolyline(curve);
	paintTempoValues(p, lane, map);
}

void ArrangerRulerLanes::paintTempoValues(QPainter& p, const QRect& lane, const TempoMap& map) const
{
	// Each tempo change's value beside it, unless the last one's label is still in the way.
	p.setPen(tokens::palette().text);
	int freeFrom = contentLeft();
	for (const TempoMapEvent& event : map.all())
	{
		const int x = xOf(event.tick);
		if (!event.hasTempo || x < freeFrom || x >= width()) { continue; }
		const QString value = QString::number(event.tempo);
		p.drawText(QPoint(x + 2, lane.top() + p.fontMetrics().ascent()), value);
		freeFrom = x + 2 + p.fontMetrics().horizontalAdvance(value) + LabelGap;
	}
}

void ArrangerRulerLanes::paintChords(QPainter& p, const QRect& lane) const
{
	const tokens::Palette& pal = tokens::palette();
	const ChordTrack& track = chordTrack();
	for (int i = 0; i < track.size(); ++i)
	{
		const ChordEvent& event = track.at(i);
		const int left = std::max(contentLeft(), xOf(event.pos));
		const int right = std::min(width(), xOf(event.pos + track.effectiveLength(i, ChordTrack::DefaultEventTicks)));
		if (right <= left) { continue; }
		const QRect area(left, lane.top() + 1, right - left - 1, lane.height() - 2);
		p.fillRect(area, pal.raised);
		p.setPen(pal.accent);
		p.drawLine(area.topLeft(), area.bottomLeft());
		p.setPen(pal.text);
		const QString label = p.fontMetrics().elidedText(chordName(event), Qt::ElideRight, area.width() - 2 * LabelGap);
		if (area.width() >= MinLabel) { p.drawText(area.adjusted(LabelGap, 0, -LabelGap, 0), Qt::AlignVCenter | Qt::AlignLeft, label); }
	}
}

bool ArrangerRulerLanes::chordAt(const QPoint& pos, QString* command, QJsonObject* args, QString* name) const
{
	if (!laneRect(Lane::Chords).contains(pos)) { return false; }
	const ChordEvent* event = chordTrack().soundingAt(tickAt(pos.x()));
	if (event == nullptr) { return false; }
	*command = QStringLiteral("transport.seek");
	*args = {{QStringLiteral("ticks"), static_cast<qint64>(event->pos)}};
	*name = event->scale.isEmpty() ? chordName(*event) : tr("%1 (in %2)").arg(chordName(*event), event->scale);
	return true;
}

void ArrangerRulerLanes::paintEvent(QPaintEvent*)
{
	m_drawn.clear();
	if (m_editor.isNull()) { return; }
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setFont(tokens::font(tokens::TextRole::Small));
	p.fillRect(rect(), pal.panel);
	for (int i = 0; i < LaneCount; ++i)
	{
		const QRect lane = laneRect(static_cast<Lane>(i));
		if (lane.isEmpty()) { continue; }
		p.fillRect(QRect(0, lane.top(), contentLeft(), lane.height()), pal.raised);
		p.setPen(pal.textMuted);
		p.drawText(QRect(LabelGap, lane.top(), contentLeft() - 2 * LabelGap, lane.height()), Qt::AlignVCenter | Qt::AlignRight, laneTitle(i));
		p.save();
		p.setClipRect(QRect(contentLeft(), lane.top(), width() - contentLeft(), lane.height()));
		if (i == 0) { paintSections(p, lane); }
		else if (i == 1) { paintMarkers(p, lane); }
		else if (i == 2) { paintTempo(p, lane); }
		else { paintChords(p, lane); }
		p.restore();
		p.setPen(pal.separator);
		p.drawLine(0, lane.bottom(), width(), lane.bottom());
	}
}

bool ArrangerRulerLanes::itemAt(const QPoint& pos, QString* command, QJsonObject* args, QString* name) const
{
	if (pos.x() < contentLeft()) { return false; }
	if (laneRect(Lane::Sections).contains(pos))
	{
		const ArrangementSection* section = ArrangementSections::instance().at(tickAt(pos.x()));
		if (section == nullptr) { return false; }
		*command = QStringLiteral("section.select");
		*args = {{QStringLiteral("section"), QStringLiteral("sec-%1").arg(section->id)}};
		*name = section->name;
		return true;
	}
	if (!laneRect(Lane::Markers).contains(pos)) { return chordAt(pos, command, args, name); }
	const QVector<Marker>& markers = ProjectMarkers::instance().markers();
	for (int i = static_cast<int>(markers.size()) - 1; i >= 0; --i)  // the latest marker at or before the pointer
	{
		const int x = xOf(markers.at(i).tick);
		if (pos.x() < x - MarkerHit) { continue; }
		if (pos.x() > x + MarkerHit + fontMetrics().horizontalAdvance(markerName(markers.at(i))) + 9) { return false; }
		*command = QStringLiteral("marker.jump");
		*args = {{QStringLiteral("marker"), QStringLiteral("mk-%1").arg(markers.at(i).id)}};
		*name = markerName(markers.at(i));
		return true;
	}
	return false;
}

bool ArrangerRulerLanes::event(QEvent* event)
{
	if (event->type() != QEvent::ToolTip) { return QWidget::event(event); }
	// A label cut short shows in full here.
	auto* help = static_cast<QHelpEvent*>(event);
	QString command;
	QJsonObject args;
	QString name;
	if (itemAt(help->pos(), &command, &args, &name)) { QToolTip::showText(help->globalPos(), name, this); }
	else { QToolTip::hideText(); }
	return true;
}

void ArrangerRulerLanes::mousePressEvent(QMouseEvent* event)
{
	QString command;
	QJsonObject args;
	QString name;
	if (event->button() != Qt::LeftButton || !itemAt(event->pos(), &command, &args, &name)) { return QWidget::mousePressEvent(event); }
	ControlRegistry::instance()->invoke(command, args);
}

void ArrangerRulerLanes::contextMenuEvent(QContextMenuEvent* event)
{
	QMenu menu(this);
	menu.addSection(tr("Ruler lanes"));
	for (int i = 0; i < LaneCount; ++i)
	{
		QAction* action = menu.addAction(laneTitle(i));
		action->setCheckable(true);
		action->setChecked(laneShown(static_cast<Lane>(i)));
		action->setProperty("controlCommand", QStringLiteral("arrangement.ruler_lanes"));
		const QString lane = laneNames().at(i);
		connect(action, &QAction::toggled, this, [lane](bool on) {
			ControlRegistry::instance()->invoke(QStringLiteral("arrangement.ruler_lanes"), {{lane, on}});
		});
	}
	menu.exec(event->globalPos());
}

} // namespace lmms::gui

namespace lmms
{

namespace
{

ControlResult rulerLanes(const QJsonObject& args)
{
	using Lanes = gui::ArrangerRulerLanes;
	QJsonObject shown;
	for (int i = 0; i < Lanes::LaneCount; ++i)
	{
		const QString name = Lanes::laneNames().at(i);
		if (args.contains(name)) { Lanes::setLaneShown(static_cast<Lanes::Lane>(i), args.value(name).toBool()); }
		shown.insert(name, Lanes::laneShown(static_cast<Lanes::Lane>(i)));
	}
	if (Lanes* lanes = gui::lanesWidget()) { lanes->relayout(); }
	return ControlResult::success(shown);
}

} // namespace

void registerRulerLaneCommands(ControlRegistry& registry)
{
	ControlCommand cmd = guiCommand("arrangement", "ruler_lanes");
	cmd.description = QStringLiteral("Show or hide the arranger's ruler lanes under the bar ruler: 'sections' (named bands), "
		"'markers' (flags and names), 'tempo' (the tempo map's curve and values) and 'chords' (the chord track's chords, "
		"which takes room only while the chord track holds one). A lane left out keeps its state; with no arguments it "
		"reports them. A preference (config ui/rulerlanes; all four shown by default), not project state.");
	const QJsonObject lanes{{QStringLiteral("sections"), control::booleanProperty()},
		{QStringLiteral("markers"), control::booleanProperty()}, {QStringLiteral("tempo"), control::booleanProperty()},
		{QStringLiteral("chords"), control::booleanProperty()}};
	cmd.argsSchema = control::objectSchema(lanes);
	cmd.resultSchema = control::objectSchema(lanes);
	cmd.handler = rulerLanes;
	registry.registerCommand(cmd);
}

} // namespace lmms
