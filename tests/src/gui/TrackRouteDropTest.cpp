/*
 * TrackRouteDropTest.cpp - drops and SlicerT give a track its channel; routing back reconciles the name (Q050-212)
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

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DataFile.h"
#include "Engine.h"
#include "FileBrowser.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "PatternStore.h"
#include "Song.h"
#include "SongEditor.h"
#include "StringPairDrag.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

void undo() { QVERIFY(run(QStringLiteral("control.undo")).ok); }
void redo() { QVERIFY(run(QStringLiteral("control.redo")).ok); }

int channels() { return Engine::mixer()->numChannels(); }
int tracks() { return static_cast<int>(Engine::getSong()->tracks().size()); }

void setAuto(bool on)
{
	QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), QStringLiteral("app/autotrackchannel")},
		{QStringLiteral("value"), on ? QStringLiteral("1") : QStringLiteral("0")}}).ok);
}

Track* trackNamed(const QString& name)
{
	for (Track* track : Engine::getSong()->tracks())
	{
		if (track->name() == name) { return track; }
	}
	return nullptr;
}

const MixerChannel* channelOf(Track* track)
{
	return Engine::mixer()->mixerChannel(track->mixerChannelModel()->value());
}

QString addChannel(const QString& name)
{
	const QString id = run(QStringLiteral("mixer.add_channel")).result.value(QStringLiteral("channel")).toString();
	run(QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), id}, {QStringLiteral("name"), name}});
	return id;
}

//! Empties the song with the event loop having run: a new track's view queues its first layout.
void clearSong()
{
	QCoreApplication::processEvents();
	Engine::getSong()->clearProject();
	QCoreApplication::processEvents();
}

void reopen(const QString& path)
{
	QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
	clearSong();
	const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}});
	QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
}

/*! Drops the StringPair payload of a drag of @a key on the arranger's empty space. A browser drag also carries the file
 *  as a URL, which takes clip.import_file's road (@a withUrl); without it the drop reaches the container's own branches. */
void dropOnArranger(const QString& key, const QString& value, bool withUrl = false)
{
	std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(key, value));
	if (!withUrl) { mime->removeFormat(QStringLiteral("text/uri-list")); }
	auto* arranger = getGUI()->songEditor()->m_editor;
	QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
	enter.setAccepted(false);
	QCoreApplication::sendEvent(arranger, &enter);
	QVERIFY(enter.isAccepted());
	QDropEvent drop(QPointF(4, 4), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
	drop.setAccepted(false);
	QCoreApplication::sendEvent(arranger, &drop);
	QVERIFY(drop.isAccepted());
	QCoreApplication::processEvents();
}

//! One second of silence; enough for the sample plug-ins to open.
QString writeWav(const QString& path)
{
	const int frames = 44100;
	const auto le = [](quint32 value, int bytes) {
		QByteArray out;
		for (int i = 0; i < bytes; ++i) { out.append(static_cast<char>((value >> (8 * i)) & 0xFF)); }
		return out;
	};
	QByteArray wav("RIFF");
	wav += le(36 + frames * 2, 4) + QByteArray("WAVEfmt ") + le(16, 4) + le(1, 2) + le(1, 2) + le(44100, 4) + le(88200, 4) + le(2, 2) + le(16, 2);
	wav += QByteArray("data") + le(frames * 2, 4) + QByteArray(frames * 2, '\0');
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return {}; }
	file.write(wav);
	return path;
}

/*! Opens the browser's context menu on @a item and triggers the @a nth (0: Song Editor, 1: Pattern Editor) entry called
 *  @a text; whether it was there. */
bool triggerBrowserEntry(FileBrowserTreeWidget* tree, QTreeWidgetItem* item, const QString& text, int nth)
{
	bool found = false;
	QTimer::singleShot(0, tree, [&found, text, nth] {
		auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
		if (menu == nullptr) { return; }
		int seen = 0;
		for (QAction* action : menu->actions())
		{
			if (action->text() == text && seen++ == nth) { found = true; action->trigger(); break; }
		}
		menu->close();
	});
	const QPoint at = tree->visualItemRect(item).center();
	QContextMenuEvent event(QContextMenuEvent::Mouse, at, tree->viewport()->mapToGlobal(at));
	QCoreApplication::sendEvent(tree->viewport(), &event);
	return found;
}

} // namespace

class TrackRouteDropTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		Engine::getSong()->setLoadOnLaunch(false);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		setAuto(false);
		clearSong();
	}

	void routedBackAfterARenameTheChannelTakesTheNameInTheRoutingStep()
	{
		setAuto(true);
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("Owner")}}).ok);
		Track* owner = trackNamed(QStringLiteral("Owner"));
		const QString id = control::trackIdOf(owner);
		const MixerChannel* own = channelOf(owner);
		const QString ownId = control::channelIdOf(own);
		const QString other = addChannel(QStringLiteral("Other"));
		const auto route = [&id](const QString& channel) {
			return run(QStringLiteral("track.set_mixer_channel"), {{QStringLiteral("track"), id}, {QStringLiteral("channel"), channel}}).ok;
		};
		const auto depth = [] { return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(); };

		QVERIFY(route(other));
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), id}, {QStringLiteral("name"), QStringLiteral("Renamed")}}).ok);
		QCOMPARE(own->m_name, QStringLiteral("Owner"));

		// Back through the command: the names meet, in the routing's one step.
		int steps = depth();
		QVERIFY(route(ownId));
		QCOMPARE(depth(), steps + 1);
		QCOMPARE(channelOf(owner), own);
		QCOMPARE(own->m_name, QStringLiteral("Renamed"));
		undo();
		QCOMPARE(control::channelIdOf(channelOf(owner)), other);
		QCOMPARE(own->m_name, QStringLiteral("Owner"));
		QCOMPARE(owner->name(), QStringLiteral("Renamed"));
		QCOMPARE(depth(), steps);
		redo();
		QCOMPARE(channelOf(owner), own);
		QCOMPARE(own->m_name, QStringLiteral("Renamed"));

		// Back through the model, as the track header's channel number writes it: the same one step.
		QVERIFY(route(other));
		owner->setName(QStringLiteral("Again"));
		QCOMPARE(own->m_name, QStringLiteral("Renamed"));
		steps = depth();
		owner->mixerChannelModel()->setValue(static_cast<float>(own->index()));
		QCOMPARE(depth(), steps + 1);
		QCOMPARE(own->m_name, QStringLiteral("Again"));
		undo();
		QCOMPARE(control::channelIdOf(channelOf(owner)), other);
		QCOMPARE(own->m_name, QStringLiteral("Renamed"));
		redo();
		QCOMPARE(own->m_name, QStringLiteral("Again"));

		// The link still works, and the meeting names survive a reopen.
		owner->setName(QStringLiteral("Kept"));
		QCOMPARE(own->m_name, QStringLiteral("Kept"));
		QTemporaryDir dir;
		reopen(dir.filePath(QStringLiteral("back.mmp")));
		owner = trackNamed(QStringLiteral("Kept"));
		QVERIFY(owner != nullptr);
		QCOMPARE(control::channelIdOf(channelOf(owner)), ownId);
		QCOMPARE(channelOf(owner)->m_name, QStringLiteral("Kept"));

		// A track routed to a channel that is not its own renames nothing by getting there.
		setAuto(false);
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("Plain")}}).ok);
		QVERIFY(run(QStringLiteral("track.set_mixer_channel"), {{QStringLiteral("track"), control::trackIdOf(trackNamed(QStringLiteral("Plain")))},
			{QStringLiteral("channel"), other}}).ok);
		QCOMPARE(channelOf(trackNamed(QStringLiteral("Plain")))->m_name, QStringLiteral("Other"));
	}

	void droppingAFileOrPresetOnTheArrangerFollowsThePreference()
	{
		QTemporaryDir dir;
		const QString wav = writeWav(dir.filePath(QStringLiteral("dropped.wav")));
		QVERIFY(!wav.isEmpty());
		const QString preset = dir.filePath(QStringLiteral("dropped.xpf"));
		{
			QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("Preset source")},
				{QStringLiteral("instrument"), QStringLiteral("tripleoscillator")}}).ok);
			Track* source = trackNamed(QStringLiteral("Preset source"));
			DataFile file(DataFile::Type::InstrumentTrackSettings);
			source->savePreset(file, file.content());
			QVERIFY(file.writeFile(preset));
			clearSong();
		}
		const auto verifyDrop = [](const QString& key, const QString& value) {
			// Off: the default route and no channel.
			setAuto(false);
			clearSong();
			dropOnArranger(key, value);
			QCOMPARE(tracks(), 1);
			QCOMPARE(channels(), 1);
			QCOMPARE(Engine::getSong()->tracks().back()->mixerChannelModel()->value(), 0);
			// On: a channel of its own, named after the track, and linked.
			setAuto(true);
			dropOnArranger(key, value);
			QCOMPARE(tracks(), 2);
			QCOMPARE(channels(), 2);
			Track* dropped = Engine::getSong()->tracks().back();
			QCOMPARE(dropped->type(), Track::Type::Instrument);
			QVERIFY(dropped->mixerChannelModel()->value() > 0);
			QCOMPARE(channelOf(dropped)->m_name, dropped->name());
			QVERIFY(dropped->linkedChannel() != nullptr);
			dropped->setName(QStringLiteral("Renamed drop"));
			QCOMPARE(channelOf(dropped)->m_name, QStringLiteral("Renamed drop"));
		};
		verifyDrop(QStringLiteral("samplefile"), wav);
		verifyDrop(QStringLiteral("presetfile"), preset);

		// The browser's whole drag, its URL included: a sample track through clip.import_file, with its channel.
		setAuto(true);
		clearSong();
		dropOnArranger(QStringLiteral("samplefile"), wav, true);
		QCOMPARE(tracks(), 1);
		QCOMPARE(channels(), 2);
		QCOMPARE(channelOf(Engine::getSong()->tracks().back())->m_name, Engine::getSong()->tracks().back()->name());
		// A preset's whole drag: what it does today is asserted nowhere here; see the feature doc's known limits.
	}

	void sendingASampleToSlicerTFollowsThePreferenceInTheSongOnly()
	{
		QTemporaryDir dir;
		QVERIFY(!writeWav(dir.filePath(QStringLiteral("sliced.wav"))).isEmpty());
		FileBrowserTreeWidget tree(nullptr);
		auto* item = new FileItem(QStringLiteral("sliced.wav"), dir.path());
		tree.addTopLevelItem(item);
		tree.resize(400, 200);
		tree.show();
		const QString entry = FileBrowserTreeWidget::tr("Send to new SlicerT instance");

		QVERIFY2(triggerBrowserEntry(&tree, item, entry, 0), "no SlicerT entry for the Song Editor");
		QCOMPARE(tracks(), 1);
		QCOMPARE(channels(), 1);
		QCOMPARE(Engine::getSong()->tracks().back()->mixerChannelModel()->value(), 0);

		setAuto(true);
		QVERIFY(triggerBrowserEntry(&tree, item, entry, 0));
		QCOMPARE(tracks(), 2);
		QCOMPARE(channels(), 2);
		Track* sliced = Engine::getSong()->tracks().back();
		QVERIFY(sliced->linkedChannel() != nullptr);
		QCOMPARE(channelOf(sliced)->m_name, sliced->name());

		// The Pattern Editor's entry makes a track outside the song: no channel.
		const auto patternTracks = Engine::patternStore()->tracks().size();
		QVERIFY2(triggerBrowserEntry(&tree, item, entry, 1), "no SlicerT entry for the Pattern Editor");
		QCOMPARE(Engine::patternStore()->tracks().size(), patternTracks + 1);
		QCOMPARE(channels(), 2);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(TrackRouteDropTest)
#include "TrackRouteDropTest.moc"
