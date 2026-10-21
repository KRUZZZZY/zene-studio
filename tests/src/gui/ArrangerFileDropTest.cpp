/*
 * ArrangerFileDropTest.cpp - a file dragged out of a plug-in editor becomes a clip where it is dropped
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
/*! 0.5.0 L04 (Q050-031): the arranger takes a `text/uri-list` drag - what a plug-in editor (VST, CLAP, LV2) or the
 *  desktop starts - with none of its own drag keys. A valid MIDI file dropped on an instrument track's lane, on a
 *  clip, on the track's header or on the empty space below the tracks becomes a MIDI clip at the drop's tick (a new
 *  track in the last case); an audio file becomes a sample clip, and still plays when the file was in the temporary
 *  folder and has been deleted since. A missing, damaged, unsupported or wrong-kind file is refused with a message
 *  and leaves the project - tracks, clips, notes and the undo stack - exactly as it was. */

#include <QtTest>

#include <cmath>
#include <memory>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QUrl>

#include "ClipView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DataFile.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "PatternEditor.h"
#include "PatternStore.h"
#include "ProjectJournal.h"
#include "SampleClip.h"
#include "Song.h"
#include "SongEditor.h"
#include "StringPairDrag.h"
#include "Track.h"
#include "TrackContentWidget.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;
constexpr int PixelsPerBar = 40;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

void be16(QByteArray& out, int value)
{
	out.append(static_cast<char>((value >> 8) & 0xFF));
	out.append(static_cast<char>(value & 0xFF));
}

//! One note, C4 for a quarter at division 96.
QByteArray midiFile()
{
	QByteArray body("\x00\x90\x3C\x64\x60\x80\x3C\x00\x00\xFF\x2F\x00", 12);
	QByteArray out("MThd", 4);
	be16(out, 0); be16(out, 6); be16(out, 0); be16(out, 1); be16(out, 96);
	out.append("MTrk", 4);
	be16(out, 0); be16(out, body.size());
	out.append(body);
	return out;
}

QByteArray wavFile(int rate = 44100)
{
	QByteArray data;
	for (int i = 0; i < rate; ++i)
	{
		const auto sample = static_cast<qint16>(12000.0 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / rate));
		data.append(static_cast<char>(sample & 0xFF));
		data.append(static_cast<char>((sample >> 8) & 0xFF));
	}
	QByteArray out("RIFF", 4);
	auto le32 = [&out](quint32 v) { for (int i = 0; i < 4; ++i) { out.append(static_cast<char>((v >> (8 * i)) & 0xFF)); } };
	auto le16 = [&out](quint16 v) { out.append(static_cast<char>(v & 0xFF)); out.append(static_cast<char>(v >> 8)); };
	le32(36 + static_cast<quint32>(data.size()));
	out.append("WAVEfmt ", 8);
	le32(16); le16(1); le16(1); le32(static_cast<quint32>(rate)); le32(static_cast<quint32>(rate * 2)); le16(2); le16(16);
	out.append("data", 4);
	le32(static_cast<quint32>(data.size()));
	out.append(data);
	return out;
}

bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

TrackView* viewOf(const Track* track)
{
	for (TrackView* view : getGUI()->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return view; }
	}
	return nullptr;
}

//! The drag a plug-in editor starts: file URLs and nothing else.
struct PluginDrag
{
	explicit PluginDrag(const QStringList& files)
	{
		QList<QUrl> urls;
		for (const QString& file : files) { urls.append(QUrl::fromLocalFile(file)); }
		mime.setUrls(urls);
	}

	//! Enter, then drop, on \a target at \a point; whether the drop was accepted.
	bool dropOn(QWidget* target, const QPoint& point, bool* entered = nullptr)
	{
		QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(target, &enter);
		if (entered != nullptr) { *entered = enter.isAccepted(); }
		QDropEvent drop(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		drop.setAccepted(false);
		QCoreApplication::sendEvent(target, &drop);
		return drop.isAccepted();
	}

	QMimeData mime;
};

//! The drag a Ctrl-drag of \a track starts (TrackGrip): the track's own saved document, keyed by its type.
std::unique_ptr<QMimeData> trackCopyDrag(Track* track)
{
	DataFile copy(DataFile::Type::DragNDropData);
	track->saveState(copy, copy.content());
	const QString key = QStringLiteral("track_%1").arg(static_cast<int>(track->type()));
	return std::unique_ptr<QMimeData>(StringPairDrag::createMimeData(key, copy.toString()));
}

//! Everything a refused drop must leave alone.
QString stateOf()
{
	QString state;
	for (Track* track : Engine::getSong()->tracks())
	{
		state += QStringLiteral("%1:%2;").arg(track->id()).arg(track->numOfClips());
	}
	return state + QStringLiteral("undo%1").arg(Engine::projectJournal()->undoDepth());
}

} // namespace

class ArrangerFileDropTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		QVERIFY(m_files.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		m_editor = getGUI()->songEditor()->m_editor;
		QVERIFY(write(m_files.filePath(QStringLiteral("generated.mid")), midiFile()));
		QVERIFY(write(m_files.filePath(QStringLiteral("notes.txt")), "this is not music"));
		QVERIFY(write(m_files.filePath(QStringLiteral("cut.mid")), midiFile().left(20)));
		QVERIFY(write(m_files.filePath(QStringLiteral("broken.wav")), "RIFF....WAVEjunk junk junk junk"));
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		m_sample = Track::create(Track::Type::Sample, Engine::getSong());
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), PixelsPerBar}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();
		QVERIFY(viewOf(m_instrument) != nullptr && viewOf(m_sample) != nullptr);
	}

	QString file(const QString& name) const { return m_files.filePath(name); }

	void aMidiFileDroppedOnAnInstrumentLaneBecomesAClipAtTheDropTick()
	{
		TrackContentWidget* lane = viewOf(m_instrument)->getTrackContentWidget();
		PluginDrag drag({file(QStringLiteral("generated.mid"))});
		bool entered = false;
		const int depth = Engine::projectJournal()->undoDepth();
		QVERIFY(drag.dropOn(lane, QPoint(2 * PixelsPerBar, 10), &entered));
		QVERIFY2(entered, "the lane refused the drag of a plug-in editor");
		QCOMPARE(m_instrument->numOfClips(), 1);
		auto* clip = dynamic_cast<MidiClip*>(m_instrument->getClip(0));
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 2 * Bar);
		QCOMPARE(static_cast<int>(clip->notes().size()), 1);
		QCOMPARE(clip->notes()[0]->key(), 60);
		QVERIFY(getGUI()->mainWindow()->statusBar()->currentMessage().contains(QStringLiteral("generated.mid")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one drop, one undo step
		QCOMPARE(m_instrument->numOfClips(), 0);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth);
	}

	void anAudioFileDroppedOnASampleLaneBecomesASampleClipAndSurvivesItsDeletion()
	{
		const QString temporary = file(QStringLiteral("plugin-drag.wav"));
		QVERIFY(write(temporary, wavFile()));
		TrackContentWidget* lane = viewOf(m_sample)->getTrackContentWidget();
		PluginDrag drag({temporary});
		QVERIFY(drag.dropOn(lane, QPoint(4 * PixelsPerBar, 10)));
		QVERIFY(QFile::remove(temporary));  // the plug-in deletes its file right after the drag
		QCOMPARE(m_sample->numOfClips(), 1);
		auto* clip = dynamic_cast<SampleClip*>(m_sample->getClip(0));
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 4 * Bar);
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), 44100);
		QVERIFY(clip->sampleFile().isEmpty());
		QVERIFY(clip->length().getTicks() > 0);
	}

	void aFileDroppedOnAClipLandsOnItsLane()
	{
		QVERIFY(run(QStringLiteral("clip.add"), {{QStringLiteral("track"), control::trackIdOf(m_instrument)}, {QStringLiteral("position"), 3 * Bar}}).ok);
		QCoreApplication::processEvents();
		ClipView* view = nullptr;
		for (ClipView* candidate : m_editor->findChildren<ClipView*>())
		{
			if (candidate->getClip() == m_instrument->getClip(0)) { view = candidate; }
		}
		QVERIFY(view != nullptr);
		PluginDrag drag({file(QStringLiteral("generated.mid"))});
		QVERIFY(drag.dropOn(view, QPoint(0, 5)));
		QCOMPARE(m_instrument->numOfClips(), 2);
		bool placed = false;
		for (int i = 0; i < m_instrument->numOfClips(); ++i)
		{
			auto* midi = dynamic_cast<MidiClip*>(m_instrument->getClip(i));
			placed = placed || (midi != nullptr && midi->notes().size() == 1 && midi->startPosition().getTicks() == 3 * Bar);
		}
		QVERIFY2(placed, "the dropped file's clip is not where the drop was");
	}

	void aFileDroppedOnATrackHeaderLandsOnThatTrack()
	{
		TrackView* view = viewOf(m_instrument);
		const int head = view->getTrackContentWidget()->x();
		PluginDrag drag({file(QStringLiteral("generated.mid"))});
		QVERIFY(drag.dropOn(view, QPoint(head / 2, 10)));
		QCOMPARE(m_instrument->numOfClips(), 1);
		QCOMPARE(m_instrument->getClip(0)->startPosition().getTicks(), 0);  // over the head, the left edge of the time shown
	}

	void aFileDroppedBelowTheTracksGetsANewTrackAndOneUndoRemovesBoth()
	{
		const int tracks = static_cast<int>(Engine::getSong()->tracks().size());
		const int head = viewOf(m_instrument)->getTrackContentWidget()->mapTo(m_editor, QPoint(0, 0)).x();
		PluginDrag drag({file(QStringLiteral("generated.mid"))});
		bool entered = false;
		QVERIFY(drag.dropOn(m_editor, QPoint(head + 3 * PixelsPerBar, m_editor->height() - 4), &entered));
		QVERIFY(entered);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks + 1);
		Track* created = Engine::getSong()->tracks().back();
		QVERIFY(dynamic_cast<InstrumentTrack*>(created) != nullptr);
		QCOMPARE(created->numOfClips(), 1);
		QCOMPARE(created->getClip(0)->startPosition().getTicks(), 3 * Bar);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks);
	}

	void aFileThatCannotBeImportedIsRefusedWithAMessageAndChangesNothing()
	{
		TrackContentWidget* instrument = viewOf(m_instrument)->getTrackContentWidget();
		TrackContentWidget* sample = viewOf(m_sample)->getTrackContentWidget();
		struct Case { QString name; TrackContentWidget* lane; QString path; };
		const QVector<Case> cases = {
			{QStringLiteral("text"), instrument, file(QStringLiteral("notes.txt"))},
			{QStringLiteral("missing"), instrument, file(QStringLiteral("gone.mid"))},
			{QStringLiteral("truncated midi"), instrument, file(QStringLiteral("cut.mid"))},
			{QStringLiteral("damaged audio"), sample, file(QStringLiteral("broken.wav"))},
			{QStringLiteral("midi on a sample lane"), sample, file(QStringLiteral("generated.mid"))},
		};
		for (const Case& c : cases)
		{
			getGUI()->mainWindow()->statusBar()->clearMessage();
			const QString before = stateOf();
			PluginDrag drag({c.path});
			QVERIFY2(!drag.dropOn(c.lane, QPoint(2 * PixelsPerBar, 10)), qPrintable(c.name + " was accepted"));
			QCOMPARE(stateOf(), before);
			QVERIFY2(!getGUI()->mainWindow()->statusBar()->currentMessage().isEmpty(), qPrintable(c.name + ": no message"));
		}
		// The same refusals through the empty space below the tracks create no track.
		const QString before = stateOf();
		const int head = instrument->mapTo(m_editor, QPoint(0, 0)).x();
		PluginDrag drag({file(QStringLiteral("cut.mid"))});
		QVERIFY(!drag.dropOn(m_editor, QPoint(head + PixelsPerBar, m_editor->height() - 4)));
		QCOMPARE(stateOf(), before);
	}

	//! A track Ctrl-dragged onto the empty space below the tracks is a copy, and that copy is one undo step.
	void aTrackCopiedByADropBelowTheTracksIsOneUndoStep()
	{
		std::unique_ptr<QMimeData> mime = trackCopyDrag(m_instrument);
		const int tracks = static_cast<int>(Engine::getSong()->tracks().size());
		const int depth = Engine::projectJournal()->undoDepth();
		const int head = viewOf(m_instrument)->getTrackContentWidget()->mapTo(m_editor, QPoint(0, 0)).x();
		const QPoint point(head + 3 * PixelsPerBar, m_editor->height() - 4);

		QDragEnterEvent enter(point, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::ControlModifier);
		QCoreApplication::sendEvent(m_editor, &enter);
		QVERIFY2(enter.isAccepted(), "the empty space refused a track drag");
		QDropEvent drop(QPointF(point), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::ControlModifier);
		QCoreApplication::sendEvent(m_editor, &drop);
		QVERIFY(drop.isAccepted());
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks + 1);
		QVERIFY(dynamic_cast<InstrumentTrack*>(Engine::getSong()->tracks().back()) != nullptr);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);  // red: the copy records no undo step

		Engine::projectJournal()->undo();  // what Edit > Undo does; the drop is not a registry command
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks);
	}

	//! The same Ctrl-drag into the Pattern Editor copies the track into the PatternStore, and that copy is one undo step.
	void aTrackCopiedIntoThePatternEditorIsOneUndoStep()
	{
		// One pattern track gives the store a pattern, so the copy gets the pattern clip and no song track is added.
		Track* pattern = Track::create(Track::Type::Pattern, Engine::getSong());
		auto* sourceClip = dynamic_cast<MidiClip*>(m_instrument->createClip(TimePos(0, 0)));
		QVERIFY(sourceClip != nullptr);
		sourceClip->changeLength(TimePos(2, 0));
		PatternEditor* patternView = getGUI()->patternEditor()->m_editor;
		patternView->updateMaxSteps();
		const int originalPixelsPerBar = patternView->pixelsPerBar();
		PatternStore* store = Engine::patternStore();
		const int storeTracks = static_cast<int>(store->tracks().size());
		const int songTracks = static_cast<int>(Engine::getSong()->tracks().size());
		const int depth = Engine::projectJournal()->undoDepth();
		std::unique_ptr<QMimeData> mime = trackCopyDrag(m_instrument);
		const QPoint point(10, 10);

		QDragEnterEvent enter(point, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::ControlModifier);
		QCoreApplication::sendEvent(patternView, &enter);
		QVERIFY2(enter.isAccepted(), "the Pattern Editor refused a track drag");
		QDropEvent drop(QPointF(point), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::ControlModifier);
		QCoreApplication::sendEvent(patternView, &drop);
		QVERIFY(drop.isAccepted());
		QCOMPARE(static_cast<int>(store->tracks().size()), storeTracks + 1);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), songTracks);
		Track* copy = store->tracks().back();
		QVERIFY(dynamic_cast<InstrumentTrack*>(copy) != nullptr);
		QCOMPARE(copy->numOfClips(), 1);
		const int copiedPixelsPerBar = patternView->pixelsPerBar();
		QVERIFY(copiedPixelsPerBar < originalPixelsPerBar);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);  // red: the copy records no undo step

		Engine::projectJournal()->undo();
		QCOMPARE(static_cast<int>(store->tracks().size()), storeTracks);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), songTracks);
		QCOMPARE(patternView->pixelsPerBar(), originalPixelsPerBar);

		Engine::projectJournal()->redo();  // rebuilt in the PatternStore, not in the song
		QCOMPARE(static_cast<int>(store->tracks().size()), storeTracks + 1);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), songTracks);
		QCOMPARE(patternView->pixelsPerBar(), copiedPixelsPerBar);
		QCoreApplication::processEvents();  // the redone track's view is built on the next turn

		delete pattern;
	}

	//! The same Ctrl-drag into a Song with no pattern track: the copy's creation also makes the Song's pattern track, and
	//! that track belongs to the drop's one undo step. Undo removes both; redo restores both.
	void aTrackCopiedIntoAnEmptyPatternEditorIsOneUndoStep()
	{
		const auto songPatternTracks = []() {
			int count = 0;
			for (Track* track : Engine::getSong()->tracks()) { count += track->type() == Track::Type::Pattern ? 1 : 0; }
			return count;
		};
		PatternStore* store = Engine::patternStore();
		QCOMPARE(songPatternTracks(), 0);  // the precondition: the drop must create the Song's first pattern track
		QCOMPARE(store->numOfPatterns(), 0);
		const int storeTracks = static_cast<int>(store->tracks().size());
		const int songTracks = static_cast<int>(Engine::getSong()->tracks().size());
		const int depth = Engine::projectJournal()->undoDepth();
		std::unique_ptr<QMimeData> mime = trackCopyDrag(m_instrument);
		PatternEditor* patternView = getGUI()->patternEditor()->m_editor;
		const QPoint point(10, 10);

		QDragEnterEvent enter(point, Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::ControlModifier);
		QCoreApplication::sendEvent(patternView, &enter);
		QVERIFY2(enter.isAccepted(), "the Pattern Editor refused a track drag");
		QDropEvent drop(QPointF(point), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::ControlModifier);
		QCoreApplication::sendEvent(patternView, &drop);
		QVERIFY(drop.isAccepted());
		QCOMPARE(songPatternTracks(), 1);  // the copy's creation made the Song's pattern track
		QCOMPARE(static_cast<int>(store->tracks().size()), storeTracks + 1);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), songTracks + 1);
		Track* copy = store->tracks().back();
		QVERIFY(dynamic_cast<InstrumentTrack*>(copy) != nullptr);
		QCOMPARE(copy->numOfClips(), 1);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);  // red: the Song's pattern track is not in the step

		Engine::projectJournal()->undo();  // one step: the copy and the Song's pattern track both go
		QCOMPARE(songPatternTracks(), 0);
		QCOMPARE(store->numOfPatterns(), 0);
		QCOMPARE(static_cast<int>(store->tracks().size()), storeTracks);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), songTracks);

		Engine::projectJournal()->redo();  // both come back, the Song's pattern track first
		QCOMPARE(songPatternTracks(), 1);
		QCOMPARE(store->numOfPatterns(), 1);
		QCOMPARE(static_cast<int>(store->tracks().size()), storeTracks + 1);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), songTracks + 1);
		QCoreApplication::processEvents();  // the redone track's view is built on the next turn
		QCOMPARE(store->tracks().back()->numOfClips(), 1);  // one pattern clip, not one from the redo and one from the restore
	}

	void aDragWithNoFileIsNotTaken()
	{
		TrackContentWidget* lane = viewOf(m_instrument)->getTrackContentWidget();
		QMimeData text;
		text.setText(QStringLiteral("hello"));
		QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
		enter.setAccepted(false);
		QCoreApplication::sendEvent(lane, &enter);
		QVERIFY(!enter.isAccepted());
	}

private:
	QTemporaryDir m_home;
	QTemporaryDir m_files;
	SongEditor* m_editor = nullptr;
	Track* m_instrument = nullptr;
	Track* m_sample = nullptr;
};

QTEST_MAIN(ArrangerFileDropTest)
#include "ArrangerFileDropTest.moc"
