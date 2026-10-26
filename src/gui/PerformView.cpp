/*
 * PerformView.cpp - the launcher's live show window: big scene buttons, the setlist, transport and tempo
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

#include "PerformView.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include "AccessibleWidgets.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "SessionGridCells.h"

namespace lmms::gui
{

namespace
{

constexpr int RefreshMs = 150;
constexpr int SetlistEvery = 7;   //!< the setlist is a config read: about once a second is enough
constexpr int TicksPerBeat = 48;  //!< a quarter note (DefaultTicksPerBar 192 / 4)

QPointer<PerformView> s_view;

ControlResult run(const QString& command, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(command, args);
}

//! The title type, @a scale times larger (the type scale is in pixels; a point-sized font is scaled in points).
QFont scaledFont(double scale, bool bold)
{
	QFont font = tokens::font(tokens::TextRole::Title);
	if (font.pixelSize() > 0) { font.setPixelSize(static_cast<int>(std::lround(font.pixelSize() * scale))); }
	else if (font.pointSizeF() > 0) { font.setPointSizeF(font.pointSizeF() * scale); }
	font.setBold(bold);
	return font;
}

//! A label in the title type, scaled by @a scale, that a screen reader names @a name.
QLabel* bigLabel(QWidget* parent, const QString& name, double scale)
{
	auto* label = new QLabel(parent);
	label->setFont(scaledFont(scale, true));
	label->setAccessibleName(name);
	return label;
}

void setText(QLabel* label, const QString& text)
{
	if (label->text() != text) { label->setText(text); }
}

//! A setlist entry's display name: its file name without the folder.
QString songName(const QJsonArray& items, int index)
{
	return index >= 0 && index < items.size() ? QFileInfo(items.at(index).toString()).completeBaseName() : QString();
}

} // namespace


PerformView::PerformView(QWidget* parent) :
	QWidget(parent, Qt::Window)
{
	setWindowTitle(tr("Perform"));
	setObjectName(QStringLiteral("PerformView"));
	setAccessibleName(tr("Perform mode"));
	auto* outer = new QVBoxLayout(this);
	buildHeader(outer);
	buildTransport(outer);
	auto* middle = new QHBoxLayout;
	// The scenes scroll, so a long set keeps its buttons big; the selected one is scrolled to (select).
	m_sceneScroll = new QScrollArea(this);
	m_sceneScroll->setWidgetResizable(true);
	m_sceneScroll->setFrameShape(QFrame::NoFrame);
	auto* sceneHost = new QWidget(m_sceneScroll);
	m_scenesBox = new QVBoxLayout(sceneHost);
	m_scenesBox->setAlignment(Qt::AlignTop);
	m_sceneScroll->setWidget(sceneHost);
	m_columnsBox = new QVBoxLayout;
	middle->addWidget(m_sceneScroll, 3);
	middle->addLayout(m_columnsBox, 2);
	outer->addLayout(middle, 1);
	m_stopAll = makeButton(tr("■ STOP ALL"), QStringLiteral("session.stop_all"), 3);
	m_stopAll->setAccessibleName(tr("Stop all clips"));
	m_stopAll->setToolTip(tr("Stop every playing clip (Escape)"));
	connect(m_stopAll, &QToolButton::clicked, this, &PerformView::pressStopAll);
	outer->addWidget(m_stopAll);
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Perform status"));
	m_status->setWordWrap(true);
	outer->addWidget(m_status);
	connect(&m_timer, &QTimer::timeout, this, &PerformView::refresh);
	m_timer.start(RefreshMs);
	refresh();
}


void PerformView::buildHeader(QVBoxLayout* outer)
{
	auto* row = new QHBoxLayout;
	m_previousSong = makeButton(tr("◀ Previous Song"), QStringLiteral("session.setlist_go"), 1);
	m_previousSong->setToolTip(tr("Open the setlist's previous song (Page Up)"));
	connect(m_previousSong, &QToolButton::clicked, this, [this] { stepSong(QStringLiteral("previous")); });
	m_song = bigLabel(this, tr("Song"), 1.4);
	m_nextSongLabel = bigLabel(this, tr("Next song"), 1.0);
	m_nextSong = makeButton(tr("Next Song ▶"), QStringLiteral("session.setlist_go"), 1);
	m_nextSong->setToolTip(tr("Open the setlist's next song (Page Down)"));
	connect(m_nextSong, &QToolButton::clicked, this, [this] { stepSong(QStringLiteral("next")); });
	row->addWidget(m_previousSong);
	row->addWidget(m_song, 1);
	row->addWidget(m_nextSongLabel, 1);
	row->addWidget(m_nextSong);
	outer->addLayout(row);
	auto* scenes = new QHBoxLayout;
	m_playing = bigLabel(this, tr("Playing scene"), 1.8);
	m_next = bigLabel(this, tr("Next scene"), 1.2);
	scenes->addWidget(m_playing, 1);
	scenes->addWidget(m_next, 1);
	outer->addLayout(scenes);
}


void PerformView::buildTransport(QVBoxLayout* outer)
{
	auto* row = new QHBoxLayout;
	m_play = makeButton(tr("▶ Play"), QStringLiteral("transport.play"), 2);
	m_play->setToolTip(tr("Start the transport (Space starts or stops it)"));
	connect(m_play, &QToolButton::clicked, this, [this] { report(run(QStringLiteral("transport.play"))); });
	m_stop = makeButton(tr("■ Stop"), QStringLiteral("transport.stop"), 2);
	m_stop->setToolTip(tr("Stop the transport (Space starts or stops it)"));
	connect(m_stop, &QToolButton::clicked, this, [this] { report(run(QStringLiteral("transport.stop"))); });
	m_position = bigLabel(this, tr("Play position"), 1.6);
	m_tempoDown = makeButton(QStringLiteral("−"), QStringLiteral("transport.set_tempo"), 2);
	m_tempoDown->setAccessibleName(tr("Tempo down one BPM"));
	connect(m_tempoDown, &QToolButton::clicked, this, [this] { stepTempo(-1); });
	m_tempo = bigLabel(this, tr("Tempo"), 1.6);
	m_tempoUp = makeButton(QStringLiteral("+"), QStringLiteral("transport.set_tempo"), 2);
	m_tempoUp->setAccessibleName(tr("Tempo up one BPM"));
	connect(m_tempoUp, &QToolButton::clicked, this, [this] { stepTempo(1); });
	m_learn = makeButton(tr("Learn MIDI"), QStringLiteral("session.bind_learn"), 1);
	m_learn->setCheckable(true);
	m_learn->setToolTip(tr("Then press a scene button or Stop All: the next note the connected controller plays "
		"is bound to it (Launcher > Controller > Connect... opens the controller)"));
	for (QWidget* widget : std::initializer_list<QWidget*>{m_play, m_stop, m_position, m_tempoDown, m_tempo, m_tempoUp, m_learn})
	{
		row->addWidget(widget, widget == m_position ? 1 : 0);
	}
	outer->addLayout(row);
}


QToolButton* PerformView::makeButton(const QString& text, const QString& command, int scale)
{
	auto* button = new QToolButton(this);
	button->setText(text);
	button->setAccessibleName(text);
	button->setProperty("controlCommand", command);
	button->setFocusPolicy(Qt::StrongFocus);
	button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	button->setMinimumHeight(tokens::targetSize() * (scale + 1));
	button->setFont(scaledFont(1.0 + 0.3 * scale, scale > 1));
	button->installEventFilter(this);  // the window's keys win over a focused button's own Space and Return
	return button;
}


PerformView* PerformView::instance(bool create)
{
	if (s_view.isNull() && create)
	{
		MainWindow* window = getGUI() != nullptr ? getGUI()->mainWindow() : nullptr;
		s_view = new PerformView(window);
		s_view->resize(1024, 720);
	}
	return s_view.data();
}


void PerformView::toggle()
{
	PerformView* view = instance(true);
	if (view->isVisible())
	{
		view->hide();
		return;
	}
	view->refresh();
	view->refreshSetlist();
	view->show();
	view->raise();
	view->activateWindow();
}


QToolButton* PerformView::sceneButton(int scene) const
{
	return scene >= 0 && scene < sceneButtonCount() ? m_sceneButtons[static_cast<std::size_t>(scene)] : nullptr;
}


void PerformView::refresh()
{
	m_readError.clear();
	const ControlResult state = run(QStringLiteral("session.get_state"));
	if (state.ok) { readSession(state.result); }
	else { m_readError = state.errorMessage; }
	readTransport();
	if (m_refreshes++ % SetlistEvery == 0) { refreshSetlist(); }
	setText(m_status, !m_readError.isEmpty() ? m_readError : m_notice.isEmpty() ? tr("Keys: 1-9 launch a scene, Up/Down select, Enter launches the selected "
		"scene, Space starts or stops, Escape stops all, Page Up/Down change song") : m_notice);
}


void PerformView::readSession(const QJsonObject& state)
{
	const QJsonObject grid = state.value(QStringLiteral("grid")).toObject();
	const int tracks = grid.value(QStringLiteral("tracks")).toInt();
	const int scenes = grid.value(QStringLiteral("scenes")).toInt();
	if (scenes != m_scenes) { rebuildScenes(scenes); }
	if (tracks != m_tracks) { rebuildColumns(tracks); }
	m_ticksPerBar = std::max(1, state.value(QStringLiteral("launch")).toObject().value(QStringLiteral("ticks_per_bar")).toInt(192));
	readNames(state.value(QStringLiteral("slots")).toArray());
	readColumns(state.value(QStringLiteral("columns")).toArray());
	paintScenes(state.value(QStringLiteral("scenes")).toArray());
	paintColumns();
}


void PerformView::readNames(const QJsonArray& cells)
{
	m_names.assign(static_cast<std::size_t>(std::max(m_tracks * m_scenes, 0)), QString());
	for (const QJsonValue& value : cells)
	{
		const QJsonObject slot = value.toObject();
		const int track = slot.value(QStringLiteral("track")).toInt(-1);
		const int scene = slot.value(QStringLiteral("scene")).toInt(-1);
		if (track < 0 || track >= m_tracks || scene < 0 || scene >= m_scenes) { continue; }
		const QString name = slot.value(QStringLiteral("name")).toString();
		m_names[static_cast<std::size_t>(track * m_scenes + scene)] = name.isEmpty() ? tr("clip") : name;
	}
}


void PerformView::readColumns(const QJsonArray& columns)
{
	m_playingScene.assign(static_cast<std::size_t>(std::max(m_tracks, 0)), -1);
	m_phases.assign(static_cast<std::size_t>(std::max(m_tracks, 0)), QString());
	for (const QJsonValue& value : columns)
	{
		const QJsonObject column = value.toObject();
		const int track = column.value(QStringLiteral("track")).toInt(-1);
		if (track < 0 || track >= m_tracks) { continue; }
		m_playingScene[static_cast<std::size_t>(track)] = column.value(QStringLiteral("scene")).toInt(-1);
		m_phases[static_cast<std::size_t>(track)] = column.value(QStringLiteral("phase")).toString();
	}
}


void PerformView::rebuildScenes(int scenes)
{
	for (QToolButton* button : m_sceneButtons)
	{
		m_scenesBox->removeWidget(button);
		button->hide();
		button->deleteLater();  // a rebuild can be reached from a scene button's own click
	}
	m_sceneButtons.clear();
	for (int scene = 0; scene < scenes; ++scene)
	{
		QToolButton* button = makeButton(QString(), QStringLiteral("session.launch_scene"), 2);
		button->setParent(m_sceneScroll->widget());
		connect(button, &QToolButton::clicked, this, [this, scene] { pressScene(scene); });
		m_scenesBox->addWidget(button);
		m_sceneButtons.push_back(button);
	}
	m_scenes = scenes;
	m_selected = std::clamp(m_selected, 0, std::max(scenes - 1, 0));
}


void PerformView::rebuildColumns(int tracks)
{
	while (QLayoutItem* item = m_columnsBox->takeAt(0))  // the labels and the stretch after them
	{
		if (item->widget() != nullptr) { item->widget()->deleteLater(); }
		delete item;
	}
	m_columns.clear();
	for (int track = 0; track < tracks; ++track)
	{
		QLabel* label = bigLabel(this, tr("Track %1").arg(track + 1), 1.0);
		m_columnsBox->addWidget(label);
		m_columns.push_back(label);
	}
	m_columnsBox->addStretch();
	m_tracks = tracks;
}


QString PerformView::sceneLabel(int scene) const
{
	const QString name = scene >= 0 && scene < static_cast<int>(m_sceneNames.size()) ? m_sceneNames[static_cast<std::size_t>(scene)] : QString();
	return name.isEmpty() ? tr("Scene %1").arg(scene + 1) : name;
}


void PerformView::paintScenes(const QJsonArray& sceneStates)
{
	m_sceneNames.assign(static_cast<std::size_t>(std::max(m_scenes, 0)), QString());
	for (int scene = 0; scene < m_scenes; ++scene)
	{
		m_sceneNames[static_cast<std::size_t>(scene)] = sceneStates.at(scene).toObject().value(QStringLiteral("name")).toString();
	}
	QStringList playing;
	for (int scene = 0; scene < m_scenes; ++scene)
	{
		const sessiongrid::SceneState mark = sessiongrid::sceneState(scene, m_scenes, m_names, m_playingScene, m_phases);
		const QString label = sceneLabel(scene);
		const QString key = scene < 9 ? QStringLiteral("%1  ").arg(scene + 1) : QString();
		QToolButton* button = m_sceneButtons[static_cast<std::size_t>(scene)];
		const QString selected = scene == m_selected ? QStringLiteral("  ◀") : QString();
		const QString text = key + sessiongrid::sceneMark(mark) + label + selected;
		if (button->text() != text) { button->setText(text); }
		button->setAccessibleName(tr("Launch %1, %2%3").arg(label, sessiongrid::stateText(mark),
			scene == m_selected ? tr(", selected") : QString()));
		if (mark == sessiongrid::SceneState::Playing) { playing << label; }
	}
	setText(m_playing, playing.isEmpty() ? tr("Playing: nothing") : tr("Playing: %1").arg(playing.join(QStringLiteral(", "))));
	setText(m_next, m_scenes > 0 ? tr("Next: %1").arg(sceneLabel(m_selected)) : tr("Next: no scenes"));
}


void PerformView::paintColumns()
{
	for (int track = 0; track < m_tracks; ++track)
	{
		const int scene = m_playingScene[static_cast<std::size_t>(track)];
		const QString& phase = m_phases[static_cast<std::size_t>(track)];
		const QString cell = scene >= 0 && scene < m_scenes ? m_names[static_cast<std::size_t>(track * m_scenes + scene)] : QString();
		const QString what = cell.isEmpty() ? QStringLiteral("—") : sessiongrid::cellMark(phase, true) + cell;
		setText(m_columns[static_cast<std::size_t>(track)], QStringLiteral("%1: %2").arg(sessiongrid::columnTitle(track), what));
	}
}


void PerformView::readTransport()
{
	const ControlResult state = run(QStringLiteral("transport.get_state"));
	if (!state.ok)
	{
		m_readError = state.errorMessage;
		return;
	}
	const QJsonObject transport = state.result;
	m_transportRunning = transport.value(QStringLiteral("playing")).toBool();
	m_bpm = transport.value(QStringLiteral("tempo")).toDouble();
	m_ticks = transport.value(QStringLiteral("position_ticks")).toInt();
	setText(m_tempo, tr("%1 BPM").arg(std::lround(m_bpm)));
	const std::int64_t bar = static_cast<std::int64_t>(m_ticks) / m_ticksPerBar + 1;
	setText(m_position, tr("Bar %1 · Beat %2").arg(static_cast<qlonglong>(bar)).arg((m_ticks % m_ticksPerBar) / TicksPerBeat + 1));
	m_play->setChecked(m_transportRunning);
}


void PerformView::refreshSetlist()
{
	const ControlResult state = run(QStringLiteral("session.setlist"));
	if (!state.ok)
	{
		m_readError = state.errorMessage;
		setText(m_status, m_readError);
		return;
	}
	const QJsonObject setlist = state.result;
	const QJsonArray items = setlist.value(QStringLiteral("items")).toArray();
	const int index = setlist.value(QStringLiteral("index")).toInt(-1);
	if (items.isEmpty())
	{
		setText(m_song, tr("No setlist (Launcher grid > Setlist...)"));
		setText(m_nextSongLabel, QString());
	}
	else
	{
		setText(m_song, index < 0 ? tr("Setlist: %n song(s), not started", nullptr, items.size())
			: tr("Song %1 of %2: %3").arg(index + 1).arg(items.size()).arg(songName(items, index)));
		const QString next = songName(items, index + 1);
		setText(m_nextSongLabel, next.isEmpty() ? tr("Last song") : tr("Next song: %1").arg(next));
	}
	m_nextSong->setEnabled(!items.isEmpty() && index + 1 < items.size());
	m_previousSong->setEnabled(index > 0);
}


void PerformView::report(const ControlResult& result)
{
	m_notice = result.ok ? QString() : result.errorMessage;
	if (!result.ok) { a11y::announceStatus(m_status, m_notice, a11y::Politeness::Assertive); }
	refresh();
}


void PerformView::pressScene(int scene)
{
	if (m_learn->isChecked())
	{
		m_learn->setChecked(false);
		const ControlResult learning = run(QStringLiteral("session.bind_learn"),
			{{QStringLiteral("action"), QStringLiteral("scene")}, {QStringLiteral("scene"), scene}});
		m_notice = learning.ok ? tr("Play a note on the controller to bind it to %1").arg(sceneLabel(scene)) : learning.errorMessage;
		refresh();
		return;
	}
	select(scene);
	report(run(QStringLiteral("session.launch_scene"), {{QStringLiteral("scene"), scene}}));
}


void PerformView::pressStopAll()
{
	if (m_learn->isChecked())
	{
		m_learn->setChecked(false);
		const ControlResult learning = run(QStringLiteral("session.bind_learn"), {{QStringLiteral("action"), QStringLiteral("stop_all")}});
		m_notice = learning.ok ? tr("Play a note on the controller to bind it to Stop All") : learning.errorMessage;
		refresh();
		return;
	}
	report(run(QStringLiteral("session.stop_all")));
}


void PerformView::select(int scene)
{
	m_selected = std::clamp(scene, 0, std::max(m_scenes - 1, 0));
	if (QToolButton* button = sceneButton(m_selected)) { m_sceneScroll->ensureWidgetVisible(button); }
}


void PerformView::moveSelection(int step)
{
	select(m_selected + step);
	refresh();
}


void PerformView::launchSelected()
{
	if (m_scenes <= 0) { return; }
	const int scene = m_selected;
	report(run(QStringLiteral("session.launch_scene"), {{QStringLiteral("scene"), scene}}));
	moveSelection(1);
}


void PerformView::toggleTransport()
{
	report(run(m_transportRunning ? QStringLiteral("transport.stop") : QStringLiteral("transport.play")));
}


void PerformView::stepTempo(int step)
{
	const int bpm = std::clamp(static_cast<int>(std::lround(m_bpm)) + step, 10, 999);
	report(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), bpm}}));
}


void PerformView::stepSong(const QString& to)
{
	// Past either end of the set there is no song to open, so the step is nothing: checked before the question below,
	// since mayChangeProject also stops the transport before it asks. The buttons are disabled at the same ends.
	const ControlResult state = run(QStringLiteral("session.setlist"));
	if (!state.ok)
	{
		m_readError = state.errorMessage;
		setText(m_status, m_readError);
		return;
	}
	const QJsonObject setlist = state.result;
	const int target = setlist.value(QStringLiteral("index")).toInt(-1) + (to == QLatin1String("previous") ? -1 : 1);
	if (target < 0 || target >= setlist.value(QStringLiteral("items")).toArray().size()) { return; }
	// It opens another project, so unsaved work is asked about first, as the grid's Next Song asks
	// (MainWindow::mayChangeProject); session.setlist_go itself has nobody to ask.
	MainWindow* window = getGUI() != nullptr ? getGUI()->mainWindow() : nullptr;
	if (window != nullptr && !window->mayChangeProject(true)) { return; }
	report(run(QStringLiteral("session.setlist_go"), {{QStringLiteral("to"), to}}));
	refreshSetlist();
}


bool PerformView::performKey(int key)
{
	if (key >= Qt::Key_1 && key <= Qt::Key_9)
	{
		const int scene = key - Qt::Key_1;
		if (scene < m_scenes) { pressScene(scene); }
		return true;
	}
	using Act = void (*)(PerformView*);
	static const std::pair<int, Act> table[] = {
		{Qt::Key_Up, [](PerformView* v) { v->moveSelection(-1); }},
		{Qt::Key_Down, [](PerformView* v) { v->moveSelection(1); }},
		{Qt::Key_Return, [](PerformView* v) { v->launchSelected(); }},
		{Qt::Key_Enter, [](PerformView* v) { v->launchSelected(); }},
		{Qt::Key_Space, [](PerformView* v) { v->toggleTransport(); }},
		{Qt::Key_Escape, [](PerformView* v) { v->pressStopAll(); }},
		{Qt::Key_PageDown, [](PerformView* v) { v->stepSong(QStringLiteral("next")); }},
		{Qt::Key_PageUp, [](PerformView* v) { v->stepSong(QStringLiteral("previous")); }},
	};
	const auto found = std::find_if(std::begin(table), std::end(table), [key](const auto& entry) { return entry.first == key; });
	if (found == std::end(table)) { return false; }
	found->second(this);
	return true;
}


bool PerformView::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::KeyPress && performKey(static_cast<QKeyEvent*>(event)->key())) { return true; }
	// The release of a key the window took must not reach the button either (Space clicks on release).
	if (event->type() == QEvent::KeyRelease && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Space) { return true; }
	return QWidget::eventFilter(watched, event);
}


void PerformView::keyPressEvent(QKeyEvent* event)
{
	if (!performKey(event->key())) { QWidget::keyPressEvent(event); }
}


QString PerformView::songText() const { return m_song->text(); }
QString PerformView::nextSongText() const { return m_nextSongLabel->text(); }
QString PerformView::playingText() const { return m_playing->text(); }
QString PerformView::nextSceneText() const { return m_next->text(); }
QString PerformView::tempoText() const { return m_tempo->text(); }
QString PerformView::positionText() const { return m_position->text(); }
QString PerformView::statusLine() const { return m_status->text(); }

QString PerformView::columnText(int track) const
{
	return track >= 0 && track < static_cast<int>(m_columns.size()) ? m_columns[static_cast<std::size_t>(track)]->text() : QString();
}

} // namespace lmms::gui
