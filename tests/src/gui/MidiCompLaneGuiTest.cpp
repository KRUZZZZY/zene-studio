/*
 * MidiCompLaneGuiTest.cpp - Q050-054.2: MIDI comping through the take lanes panel
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

#include <QtTest>
#include <QJsonArray>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <algorithm>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "CompSwipeStrip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "SongEditor.h"
#include "TakeLanesPanel.h"
#include "TrackContainerView.h"
#include "TrackOperationsWidget.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{
ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}
int undoDepth() { return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(); }
const QList<TrackView*>& rows() { return getGUI()->songEditor()->m_editor->trackViews(); }
TrackView* rowOf(const QString& id)
{
	for (TrackView* view : rows())
	{
		if (control::trackIdOf(view->getTrack()) == id) { return view; }
	}
	return nullptr;
}
QAction* findAction(QMenu* menu, const QString& name)
{
	for (QAction* action : menu->actions())
	{
		if (action->objectName() == name) { return action; }
		if (action->menu() != nullptr)
		{
			if (QAction* inner = findAction(action->menu(), name)) { return inner; }
		}
	}
	return nullptr;
}
QAction* lanesAction(const QString& id)
{
	TrackView* view = rowOf(id);
	if (view == nullptr) { return nullptr; }
	auto* operations = view->findChild<TrackOperationsWidget*>();
	if (operations == nullptr) { return nullptr; }
	QMenu* menu = nullptr;
	for (QPushButton* candidate : operations->findChildren<QPushButton*>())
	{
		if (candidate->menu() != nullptr) { menu = candidate->menu(); }
	}
	if (menu == nullptr) { return nullptr; }
	emit menu->aboutToShow();
	return findAction(menu, QStringLiteral("takeLanes"));
}
bool inkAt(const QImage& image, const QRect& rect, int x)
{
	const QRect visible = rect.intersected(image.rect()).adjusted(0, 1, 0, -1);
	if (visible.isEmpty() || x < visible.left() || x > visible.right()) { return false; }
	QHash<QRgb, int> counts;
	int total = 0;
	for (int y = visible.top(); y <= visible.bottom(); ++y)
	{
		++counts[image.pixel(x, y)];
		++total;
	}
	int mostCommon = 0;
	for (auto it = counts.cbegin(); it != counts.cend(); ++it) { mostCommon = std::max(mostCommon, it.value()); }
	return total - mostCommon > 0;
}
QList<int> noteKeys(const InstrumentTrack* track)
{
	QList<int> keys;
	for (PlayHandle* handle : Engine::audioEngine()->playHandles())
	{
		auto* note = dynamic_cast<NotePlayHandle*>(handle);
		if (note != nullptr && note->instrumentTrack() == track) { keys << note->key(); }
	}
	std::sort(keys.begin(), keys.end());
	return keys;
}
QList<int> playFrom(InstrumentTrack* track, int tick)
{
	Song* song = Engine::getSong();
	song->setPlayPos(tick, Song::PlayMode::Song);
	song->playSong();
	for (int period = 0; period < 3; ++period) { Engine::audioEngine()->renderNextPeriod(); }
	const QList<int> keys = noteKeys(track);
	song->stop();
	for (int period = 0; period < 64 && !noteKeys(track).isEmpty(); ++period) { Engine::audioEngine()->renderNextPeriod(); }
	return keys;
}
QJsonArray segmentsOf(const QString& id)
{
	return run(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), id}})
		.result.value(QStringLiteral("composite")).toObject().value(QStringLiteral("segments")).toArray();
}
void makeMidiTakes(QString& id, InstrumentTrack*& track, MidiClip*& first, MidiClip*& second)
{
	const auto added = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}});
	QVERIFY(added.ok);
	id = added.result.value(QStringLiteral("track")).toString();
	ControlResult error;
	track = dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &error));
	QVERIFY(track != nullptr);
	QVERIFY(track->loadInstrument("tripleoscillator"));
	track->setName("MIDI comp");
	QCOMPARE(track->name(), QStringLiteral("MIDI comp"));
	first = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	second = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	QVERIFY(first != nullptr && second != nullptr);
	for (auto* clip : {first, second}) { clip->setAutoResize(false); clip->changeLength(TimePos(384)); }
	first->addNote(Note(TimePos(48), TimePos(0), 60), false);
	first->addNote(Note(TimePos(96), TimePos(168), 62), false);
	first->addNote(Note(TimePos(24), TimePos(300), 67), false);
	second->addNote(Note(TimePos(48), TimePos(0), 64), false);
	second->addNote(Note(TimePos(48), TimePos(240), 65), false);
}

void openMidiPanel(const QString& id, MidiClip* first, MidiClip* second, CompSwipeStrip*& strip)
{
	QCoreApplication::processEvents();
	const auto beforePanels = getGUI()->mainWindow()->findChildren<TakeLanesPanel*>().size();
	QAction* action = lanesAction(id);
	QVERIFY(action != nullptr && action->isEnabled());
	QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("comp.get_state"));
	action->trigger();
	QCoreApplication::processEvents();
	auto panels = getGUI()->mainWindow()->findChildren<TakeLanesPanel*>();
	QCOMPARE(panels.size(), beforePanels + 1);
	auto* panel = panels.last();
	QTest::mouseClick(panel->addLaneButton(), Qt::LeftButton);
	QTest::mouseClick(panel->addLaneButton(), Qt::LeftButton);
	QVERIFY(run(QStringLiteral("comp.assign"), {{QStringLiteral("clip"), control::clipIdOf(first)}, {QStringLiteral("lane"), 0}}).ok);
	QVERIFY(run(QStringLiteral("comp.assign"), {{QStringLiteral("clip"), control::clipIdOf(second)}, {QStringLiteral("lane"), 1}}).ok);
	panel->refresh(); panel->resize(640, 420); panel->show(); QCoreApplication::processEvents();
	strip = panel->strip();
	QCOMPARE(strip->takeBands().size(), std::size_t{2});
}

void swipeMidiSegments(CompSwipeStrip* strip, int& boundary, int& end, int& depth)
{
	const int boundaryX = strip->tickToX(192);
	boundary = strip->tickAt(boundaryX);
	end = strip->tickAt(strip->width() - 1);
	QVERIFY(boundary >= 184 && boundary <= 200);
	depth = undoDepth();
	QTest::mousePress(strip, Qt::LeftButton, Qt::NoModifier, QPoint(0, 10));
	QTest::mouseMove(strip, QPoint(boundaryX, 10));
	QTest::mouseRelease(strip, Qt::LeftButton, Qt::NoModifier, QPoint(boundaryX, 10));
	QCOMPARE(undoDepth(), depth + 1);
	QTest::mousePress(strip, Qt::LeftButton, Qt::NoModifier, QPoint(boundaryX, 32));
	QTest::mouseMove(strip, QPoint(strip->width() - 1, 32));
	QTest::mouseRelease(strip, Qt::LeftButton, Qt::NoModifier, QPoint(strip->width() - 1, 32));
	QCOMPARE(undoDepth(), depth + 2);
}

void checkMidiSegments(const QString& id, int boundary, int end, QJsonArray& segments)
{
	segments = segmentsOf(id);
	QCOMPARE(segments.size(), 2);
	for (int i = 0; i < 2; ++i)
	{
		const auto segment = segments.at(i).toObject();
		QCOMPARE(segment.value(QStringLiteral("lane")).toInt(), i);
		QCOMPARE(segment.value(QStringLiteral("begin")).toInt(), i == 0 ? 0 : boundary);
		QCOMPARE(segment.value(QStringLiteral("end")).toInt(), i == 0 ? boundary : end);
	}
}

void checkPlayback(InstrumentTrack* current)
{
	QCOMPARE(playFrom(current, 0), QList<int>({60}));
	QCOMPARE(playFrom(current, 168), QList<int>({62}));
	const auto at240 = playFrom(current, 240);
	QVERIFY(at240.contains(65) && !at240.contains(64));
	QVERIFY(!playFrom(current, 300).contains(67));
}

void checkUndoAndInvalidSelections(const QString& id, int depth, const QJsonArray& segments)
{
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(undoDepth(), depth + 1);
	QCOMPARE(segmentsOf(id).size(), 1);
	QCOMPARE(segmentsOf(id).first().toObject().value(QStringLiteral("lane")).toInt(), 0);
	QVERIFY(run(QStringLiteral("control.redo")).ok);
	QCOMPARE(segmentsOf(id), segments);
	const int currentDepth = undoDepth();
	QVERIFY(!run(QStringLiteral("comp.select"), {{QStringLiteral("track"), id}, {QStringLiteral("begin"), 0}, {QStringLiteral("end"), 100}, {QStringLiteral("lane"), 5}}).ok);
	QVERIFY(!run(QStringLiteral("comp.select"), {{QStringLiteral("track"), id}, {QStringLiteral("begin"), 200}, {QStringLiteral("end"), 100}, {QStringLiteral("lane"), 0}}).ok);
	QCOMPARE(segmentsOf(id), segments);
	QCOMPARE(undoDepth(), currentDepth);
}

void reloadMidiProject(const QTemporaryDir& home, QString& id, InstrumentTrack*& track, const QJsonArray& segments)
{
	const QString path = home.filePath(QStringLiteral("midicomp.mmp"));
	QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
	QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}}).ok);
	Engine::audioEngine()->audioDev()->stopProcessing();
	QCoreApplication::processEvents();
	track = nullptr;
	QStringList names;
	for (Track* candidate : Engine::getSong()->tracks())
	{
		names << candidate->name();
		if (candidate->name() == QStringLiteral("MIDI comp")) { track = dynamic_cast<InstrumentTrack*>(candidate); }
	}
	QVERIFY2(track != nullptr, qPrintable(names.join(", ")));
	id = control::trackIdOf(track);
	QCOMPARE(segmentsOf(id), segments);
	QList<int> lanes;
	for (Clip* clip : track->getClips())
	{
		if (auto* midi = dynamic_cast<MidiClip*>(clip)) { lanes << midi->laneIndex(); }
	}
	std::sort(lanes.begin(), lanes.end());
	QCOMPARE(lanes, QList<int>({0, 1}));
}

void reopenMidiPanel(const QString& id)
{
	const auto oldPanelCount = getGUI()->mainWindow()->findChildren<TakeLanesPanel*>().size();
	QAction* action = lanesAction(id);
	QVERIFY(action != nullptr && action->isEnabled());
	action->trigger(); QCoreApplication::processEvents();
	auto panels = getGUI()->mainWindow()->findChildren<TakeLanesPanel*>();
	QCOMPARE(panels.size(), oldPanelCount + 1);
	panels.last()->refresh();
	QCOMPARE(panels.last()->strip()->takeBands().size(), std::size_t{2});
}

}

class MidiCompLaneGuiTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		NotePlayHandleManager::init();
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}
	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}
	void aMidiLaneRegionSelectedInTheStripGatesPlaybackAndSurvivesSaveLoad()
	{
		QString id;
		InstrumentTrack* track = nullptr;
		MidiClip* first = nullptr;
		MidiClip* second = nullptr;
		CompSwipeStrip* strip = nullptr;
		int boundary = 0, end = 0, depth = 0;
		QJsonArray segments;
		makeMidiTakes(id, track, first, second);
		if (QTest::currentTestFailed()) { return; }
		openMidiPanel(id, first, second, strip);
		if (QTest::currentTestFailed()) { return; }
		swipeMidiSegments(strip, boundary, end, depth);
		if (QTest::currentTestFailed()) { return; }
		checkMidiSegments(id, boundary, end, segments);
		if (QTest::currentTestFailed()) { return; }
		checkPlayback(track);
		if (QTest::currentTestFailed()) { return; }
		checkUndoAndInvalidSelections(id, depth, segments);
		if (QTest::currentTestFailed()) { return; }
		reloadMidiProject(m_home, id, track, segments);
		if (QTest::currentTestFailed()) { return; }
		checkPlayback(track);
		if (QTest::currentTestFailed()) { return; }
		reopenMidiPanel(id);
		if (QTest::currentTestFailed()) { return; }
	}

	void theStripDrawsAMidiTakesNotes()
	{
		const auto added = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}});
		QVERIFY(added.ok);
		const QString id = added.result.value(QStringLiteral("track")).toString();
		ControlResult error;
		auto* track = dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &error));
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument("tripleoscillator"));
		auto* first = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		auto* second = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(first != nullptr && second != nullptr);
		for (auto* clip : {first, second}) { clip->setAutoResize(false); clip->changeLength(TimePos(384)); }
		first->addNote(Note(TimePos(96), TimePos(0), 60), false);
		second->addNote(Note(TimePos(96), TimePos(192), 72), false);
		QCoreApplication::processEvents();
		const auto beforePanels = getGUI()->mainWindow()->findChildren<TakeLanesPanel*>().size();
		QAction* action = lanesAction(id);
		QVERIFY(action != nullptr && action->isEnabled());
		action->trigger();
		QCoreApplication::processEvents();
		const auto panels = getGUI()->mainWindow()->findChildren<TakeLanesPanel*>();
		QCOMPARE(panels.size(), beforePanels + 1);
		auto* panel = panels.last();
		QVERIFY(panel->consolidateButton()->isHidden());
		QTest::mouseClick(panel->addLaneButton(), Qt::LeftButton);
		QTest::mouseClick(panel->addLaneButton(), Qt::LeftButton);
		QVERIFY(run(QStringLiteral("comp.assign"), {{QStringLiteral("clip"), control::clipIdOf(first)}, {QStringLiteral("lane"), 0}}).ok);
		QVERIFY(run(QStringLiteral("comp.assign"), {{QStringLiteral("clip"), control::clipIdOf(second)}, {QStringLiteral("lane"), 1}}).ok);
		panel->resize(640, 420); panel->show(); QCoreApplication::processEvents(); panel->refresh();
		auto* strip = panel->strip();
		QCOMPARE(strip->takeBands().size(), std::size_t{2});
		QRect firstRect, secondRect;
		for (const auto& band : strip->takeBands())
		{
			if (band.lane == 0) { firstRect = band.rect; }
			if (band.lane == 1) { secondRect = band.rect; }
		}
		QVERIFY(!firstRect.isEmpty() && !secondRect.isEmpty());
		const QImage drawn = strip->grab().toImage();
		QVERIFY(inkAt(drawn, firstRect, strip->tickToX(48)));
		QVERIFY(inkAt(drawn, secondRect, strip->tickToX(240)));
		QVERIFY(!inkAt(drawn, firstRect, strip->tickToX(288)));
	}

private:
	QTemporaryDir m_home;
};
QTEST_MAIN(MidiCompLaneGuiTest)
#include "MidiCompLaneGuiTest.moc"
