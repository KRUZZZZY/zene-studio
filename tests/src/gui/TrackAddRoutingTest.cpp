/*
 * TrackAddRoutingTest.cpp - a new track's destination and its own named mixer channel (Q050-212)
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

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include "AddTrackMenu.h"
#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlTrackCreation.h"
#include "ControlTrackRouting.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "PatternStore.h"
#include "SetupDialog.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString runOk(const QString& id, const QJsonObject& args, const char* field)
{
	const ControlResult result = run(id, args);
	if (!result.ok) { qWarning() << id << result.errorMessage; }
	return result.result.value(QString::fromLatin1(field)).toString();
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

//! The channel a track plays into.
const MixerChannel* channelOf(Track* track)
{
	return Engine::mixer()->mixerChannel(track->mixerChannelModel()->value());
}

QString addChannel(const QString& name)
{
	const QString id = runOk(QStringLiteral("mixer.add_channel"), {}, "channel");
	run(QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), id}, {QStringLiteral("name"), name}});
	return id;
}

/*! Empties the song as a person's session does: with the event loop having run since the last track was added. A new
 *  track's view queues its first layout, which a clear in the same event-loop turn would leave reading a freed track. */
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
	QCOMPARE(tracks(), 0);
	QCOMPARE(channels(), 1);
	const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}});
	QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
}

QAction* actionIn(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
		if (action->menu() != nullptr)
		{
			if (QAction* inner = actionIn(action->menu(), text)) { return inner; }
		}
	}
	return nullptr;
}

//! The toolbar's own entry whose text is @a text.
QAction* toolbarAction(const QString& text)
{
	for (QToolBar* bar : getGUI()->songEditor()->findChildren<QToolBar*>())
	{
		for (QAction* action : bar->actions())
		{
			if (action->text() == text) { return action; }
		}
	}
	return nullptr;
}

//! The add-track menu as the toolbar button opens it.
QMenu* openedAddTrackMenu()
{
	auto* button = getGUI()->songEditor()->findChild<QToolButton*>(QStringLiteral("addTrackButton"));
	if (button == nullptr || button->menu() == nullptr) { return nullptr; }
	emit button->menu()->aboutToShow();
	return button->menu();
}

} // namespace

class TrackAddRoutingTest : public QObject
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

	void anExplicitOutputRoutesTheNewTrackAndUndoRedoSaveLoadKeepIt()
	{
		const QString bus = addChannel(QStringLiteral("Drums"));
		const int steps = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		for (const QString& type : {QStringLiteral("instrument"), QStringLiteral("sample")})
		{
			const ControlResult added = run(QStringLiteral("track.add"), {{QStringLiteral("type"), type},
				{QStringLiteral("name"), type}, {QStringLiteral("output"), bus}});
			QVERIFY2(added.ok, qPrintable(added.errorMessage));
			QCOMPARE(added.result.value(QStringLiteral("channel")).toString(), bus);
			QCOMPARE(added.result.value(QStringLiteral("channel_linked")).toBool(), false);
			QCOMPARE(control::channelIdOf(channelOf(trackNamed(type))), bus);
		}
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), steps + 2);

		undo();
		QVERIFY(trackNamed(QStringLiteral("sample")) == nullptr);
		QCOMPARE(channels(), 2);  // a channel it did not make stays
		redo();
		QVERIFY(trackNamed(QStringLiteral("sample")) != nullptr);
		QCOMPARE(control::channelIdOf(channelOf(trackNamed(QStringLiteral("sample")))), bus);

		QTemporaryDir dir;
		reopen(dir.filePath(QStringLiteral("explicit.mmp")));
		QCOMPARE(control::channelIdOf(channelOf(trackNamed(QStringLiteral("instrument")))), bus);
		QCOMPARE(control::channelIdOf(channelOf(trackNamed(QStringLiteral("sample")))), bus);
		QCOMPARE(channelOf(trackNamed(QStringLiteral("sample")))->m_name, QStringLiteral("Drums"));
	}

	void anExplicitOutputWinsOverThePreference()
	{
		const QString bus = addChannel(QStringLiteral("Drums"));
		setAuto(true);
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("t")}, {QStringLiteral("output"), bus}}).ok);
		QCOMPARE(channels(), 2);
		QCOMPARE(trackNamed(QStringLiteral("t"))->linkedChannelId(), -1);
	}

	void aDestinationThatCouldNotPlayIsRefusedAndChangesNothing()
	{
		const QString parallel = runOk(QStringLiteral("bus.create"), {}, "channel");
		QVERIFY(!parallel.isEmpty());
		const int trackCount = tracks();
		const int channelCount = channels();
		QVERIFY(!run(QStringLiteral("track.add"), {{QStringLiteral("output"), parallel}}).ok);
		QVERIFY(!run(QStringLiteral("track.add"), {{QStringLiteral("output"), QStringLiteral("ch-99999")}}).ok);
		QVERIFY(!run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("pattern")}, {QStringLiteral("output"), parallel}}).ok);
		QVERIFY(!run(QStringLiteral("track.add"), {{QStringLiteral("instrument"), QStringLiteral("no-such-plugin")}}).ok);
		QVERIFY(!run(QStringLiteral("track.add_to_new_bus"), {{QStringLiteral("count"), 65}}).ok);
		QVERIFY(!run(QStringLiteral("track.add_to_new_bus"), {{QStringLiteral("count"), 2},
			{QStringLiteral("instrument"), QStringLiteral("no-such-plugin")}}).ok);
		QCOMPARE(tracks(), trackCount);
		QCOMPARE(channels(), channelCount);
	}

	void withThePreferenceOffANewTrackKeepsTheDefaultRoute()
	{
		const int before = channels();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("off")}}).ok);
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}, {QStringLiteral("name"), QStringLiteral("off2")}}).ok);
		QCOMPARE(channels(), before);
		QCOMPARE(trackNamed(QStringLiteral("off"))->mixerChannelModel()->value(), 0);
		QCOMPARE(trackNamed(QStringLiteral("off2"))->mixerChannelModel()->value(), 0);
		QCOMPARE(trackNamed(QStringLiteral("off"))->linkedChannelId(), -1);
	}

	void autoAssignmentNamesEachChannelAndRenameKeepsThemInStep()
	{
		setAuto(true);
		const ControlResult instrument = run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("Lead")}});
		QVERIFY2(instrument.ok, qPrintable(instrument.errorMessage));
		QCOMPARE(instrument.result.value(QStringLiteral("channel_name")).toString(), QStringLiteral("Lead"));
		QCOMPARE(instrument.result.value(QStringLiteral("channel_linked")).toBool(), true);
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}, {QStringLiteral("name"), QStringLiteral("Vox")}}).ok);
		Track* lead = trackNamed(QStringLiteral("Lead"));
		Track* vox = trackNamed(QStringLiteral("Vox"));
		QVERIFY(channelOf(lead) != channelOf(vox));
		QVERIFY(lead->mixerChannelModel()->value() > 0 && vox->mixerChannelModel()->value() > 0);
		QCOMPARE(channelOf(lead)->m_name, QStringLiteral("Lead"));
		QCOMPARE(channelOf(vox)->m_name, QStringLiteral("Vox"));

		// A pattern, automation or folder track has no channel to give.
		const int count = channels();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("automation")}}).ok);
		QCOMPARE(channels(), count);

		// Track rename: the channel follows, and its one undo takes both back.
		const QString leadId = control::trackIdOf(lead);
		const QString channelIdText = control::channelIdOf(channelOf(lead));
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), leadId}, {QStringLiteral("name"), QStringLiteral("Solo")}}).ok);
		QCOMPARE(channelOf(lead)->m_name, QStringLiteral("Solo"));
		undo();
		QCOMPARE(lead->name(), QStringLiteral("Lead"));
		QCOMPARE(channelOf(lead)->m_name, QStringLiteral("Lead"));
		redo();
		QCOMPARE(channelOf(lead)->m_name, QStringLiteral("Solo"));

		// Channel rename: the track follows, likewise.
		QVERIFY(run(QStringLiteral("mixer.rename_channel"), {{QStringLiteral("channel"), channelIdText}, {QStringLiteral("name"), QStringLiteral("Hook")}}).ok);
		QCOMPARE(lead->name(), QStringLiteral("Hook"));
		undo();
		QCOMPARE(lead->name(), QStringLiteral("Solo"));
		QCOMPARE(channelOf(lead)->m_name, QStringLiteral("Solo"));

		// Turning the preference off changes future additions only.
		setAuto(false);
		lead->setName(QStringLiteral("Still linked"));
		QCOMPARE(channelOf(lead)->m_name, QStringLiteral("Still linked"));
		const int channelsNow = channels();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("later")}}).ok);
		QCOMPARE(channels(), channelsNow);
	}

	void aReroutedTrackOrACopyRenamesNoChannel()
	{
		setAuto(true);
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("Owner")}}).ok);
		Track* owner = trackNamed(QStringLiteral("Owner"));
		const MixerChannel* own = channelOf(owner);

		Track* copy = owner->clone();
		QCOMPARE(copy->linkedChannelId(), -1);
		copy->setName(QStringLiteral("Copy"));
		QCOMPARE(own->m_name, QStringLiteral("Owner"));

		const QString other = addChannel(QStringLiteral("Other"));
		QVERIFY(run(QStringLiteral("track.set_mixer_channel"), {{QStringLiteral("track"), control::trackIdOf(owner)},
			{QStringLiteral("channel"), other}}).ok);
		owner->setName(QStringLiteral("Renamed"));
		QCOMPARE(own->m_name, QStringLiteral("Owner"));
		QCOMPARE(channelOf(owner)->m_name, QStringLiteral("Other"));
	}

	void autoAssignmentIsOneUndoStepAndSurvivesRedoAndReopen()
	{
		setAuto(true);
		const int before = channels();
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const ControlResult added = run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("Bass")},
			{QStringLiteral("instrument"), QStringLiteral("tripleoscillator")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth + 1);
		const QString channel = added.result.value(QStringLiteral("channel")).toString();

		undo();
		QVERIFY(trackNamed(QStringLiteral("Bass")) == nullptr);
		QCOMPARE(channels(), before);

		redo();
		Track* bass = trackNamed(QStringLiteral("Bass"));
		QVERIFY(bass != nullptr);
		QCOMPARE(channels(), before + 1);
		QCOMPARE(control::channelIdOf(channelOf(bass)), channel);
		QCOMPARE(channelOf(bass)->m_name, QStringLiteral("Bass"));

		QTemporaryDir dir;
		// Off while reopening: the link comes from the file, not the preference.
		setAuto(false);
		reopen(dir.filePath(QStringLiteral("auto.mmp")));
		bass = trackNamed(QStringLiteral("Bass"));
		QVERIFY(bass != nullptr);
		QCOMPARE(channels(), before + 1);
		QCOMPARE(control::channelIdOf(channelOf(bass)), channel);
		QCOMPARE(channelOf(bass)->m_name, QStringLiteral("Bass"));
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), control::trackIdOf(bass)}, {QStringLiteral("name"), QStringLiteral("Sub")}}).ok);
		QCOMPARE(channelOf(bass)->m_name, QStringLiteral("Sub"));

		// Removing the track and undoing that keeps the link: it is in the captured document.
		const QString id = control::trackIdOf(bass);
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), id}}).ok);
		undo();
		bass = trackNamed(QStringLiteral("Sub"));
		QVERIFY(bass != nullptr);
		bass->setName(QStringLiteral("Back"));
		QCOMPARE(channelOf(bass)->m_name, QStringLiteral("Back"));
	}

	void openingAProjectAssignsNothing()
	{
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("name"), QStringLiteral("old")}}).ok);
		QTemporaryDir dir;
		const QString path = dir.filePath(QStringLiteral("old.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QVERIFY2(!file.readAll().contains("mixlink"), "a track with no channel of its own wrote the link");
		setAuto(true);
		clearSong();
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}}).ok);
		QCOMPARE(channels(), 1);
		QCOMPARE(trackNamed(QStringLiteral("old"))->mixerChannelModel()->value(), 0);
	}

	void aCommandsOwnNewTrackGetsItsChannelInOneStep()
	{
		setAuto(true);
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		track->setName(QStringLiteral("Loaded"));
		control::recordCreatedRoutedTrack(track);
		const QString channel = control::channelIdOf(channelOf(track));
		QCOMPARE(channelOf(track)->m_name, QStringLiteral("Loaded"));
		undo();
		QVERIFY(trackNamed(QStringLiteral("Loaded")) == nullptr);
		QCOMPARE(channels(), 1);
		redo();
		Track* again = trackNamed(QStringLiteral("Loaded"));
		QVERIFY(again != nullptr);
		QCOMPARE(control::channelIdOf(channelOf(again)), channel);
		again->setName(QStringLiteral("Linked"));
		QCOMPARE(channelOf(again)->m_name, QStringLiteral("Linked"));
		// A track outside the song is the Pattern Editor's: not assigned.
		const int count = channels();
		control::assignNewTrackChannel(Track::create(Track::Type::Sample, Engine::patternStore()));
		QCOMPARE(channels(), count);
	}

	void addToNewBusSharesOneChannelInOneUndoStep()
	{
		setAuto(true);  // an explicit destination: no channel per track
		const int before = channels();
		const int tracksBefore = tracks();
		const ControlResult made = run(QStringLiteral("track.add_to_new_bus"), {{QStringLiteral("count"), 3},
			{QStringLiteral("type"), QStringLiteral("sample")}, {QStringLiteral("bus_name"), QStringLiteral("Vocals")},
			{QStringLiteral("name"), QStringLiteral("Take")}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		const QString channel = made.result.value(QStringLiteral("channel")).toString();
		QCOMPARE(made.result.value(QStringLiteral("tracks")).toArray().size(), 3);
		const auto verifyMade = [&] {
			QCOMPARE(channels(), before + 1);
			QCOMPARE(tracks(), tracksBefore + 3);
			for (const QString& name : {QStringLiteral("Take 1"), QStringLiteral("Take 2"), QStringLiteral("Take 3")})
			{
				Track* track = trackNamed(name);
				QVERIFY2(track != nullptr, qPrintable(name));
				QCOMPARE(control::channelIdOf(channelOf(track)), channel);
				QCOMPARE(channelOf(track)->m_name, QStringLiteral("Vocals"));
				QCOMPARE(track->linkedChannelId(), -1);
			}
		};
		verifyMade();
		undo();
		QCOMPARE(channels(), before);
		QCOMPARE(tracks(), tracksBefore);
		redo();
		verifyMade();
		QTemporaryDir dir;
		reopen(dir.filePath(QStringLiteral("bus.mmp")));
		verifyMade();
	}

	void theToolbarOffersTheDestinationAndAddsThroughTheCommand()
	{
		const QString bus = addChannel(QStringLiteral("Keys"));
		QVERIFY(run(QStringLiteral("bus.create")).ok);  // a parallel bus is not offered
		const QString parallelName = Engine::mixer()->mixerChannel(channels() - 1)->m_name;
		QMenu* menu = openedAddTrackMenu();
		QVERIFY2(menu != nullptr, "no add-track button on the song editor's toolbar");
		QVERIFY(actionIn(menu, parallelName) == nullptr);

		// Instrument track to > Keys.
		QAction* keys = nullptr;
		for (QAction* entry : menu->actions())
		{
			if (entry->menu() != nullptr && entry->text() == QObject::tr("Instrument track to")) { keys = actionIn(entry->menu(), QStringLiteral("Keys")); }
		}
		QVERIFY(keys != nullptr);
		QCOMPARE(keys->data().toString(), QStringLiteral("track.add"));
		const int before = tracks();
		keys->trigger();
		QCOMPARE(tracks(), before + 1);
		Track* added = Engine::getSong()->tracks().back();
		QCOMPARE(added->type(), Track::Type::Instrument);
		QCOMPARE(control::channelIdOf(channelOf(added)), bus);
		// One Ctrl+Z: an interface entry is the command, its one step included.
		undo();
		QCOMPARE(tracks(), before);
	}

	void theToolbarSampleActionFollowsThePreference()
	{
		QAction* addSample = toolbarAction(SongEditorWindow::tr("Add sample-track"));
		QVERIFY(addSample != nullptr);
		addSample->trigger();
		QCOMPARE(channels(), 1);
		QCOMPARE(Engine::getSong()->tracks().back()->type(), Track::Type::Sample);
		setAuto(true);
		addSample->trigger();
		QCOMPARE(channels(), 2);
		Track* added = Engine::getSong()->tracks().back();
		QCOMPARE(channelOf(added)->m_name, added->name());
		undo();
		QCOMPARE(channels(), 1);
	}

	void theNewBusDialogAddsNTracks()
	{
		QMenu* menu = openedAddTrackMenu();
		QVERIFY(menu != nullptr);
		QAction* entry = actionIn(menu, QObject::tr("Add tracks to new bus..."));
		QVERIFY(entry != nullptr);
		QCOMPARE(entry->data().toString(), QStringLiteral("track.add_to_new_bus"));
		bool driven = false;
		QTimer::singleShot(0, this, [&driven] {
			auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
			if (dialog == nullptr || dialog->objectName() != QStringLiteral("addTracksToNewBus")) { return; }
			dialog->findChild<QSpinBox*>(QStringLiteral("count"))->setValue(4);
			auto* type = dialog->findChild<QComboBox*>(QStringLiteral("type"));
			type->setCurrentIndex(type->findData(QStringLiteral("sample")));
			dialog->findChild<QLineEdit*>(QStringLiteral("busName"))->setText(QStringLiteral("Stems"));
			driven = true;
			dialog->accept();
		});
		const int before = tracks();
		entry->trigger();
		QVERIFY2(driven, "the dialog did not open");
		QCOMPARE(tracks(), before + 4);
		QCOMPARE(channels(), 2);
		for (int i = before; i < tracks(); ++i)
		{
			QCOMPARE(channelOf(Engine::getSong()->tracks()[i])->m_name, QStringLiteral("Stems"));
		}
		undo();
		QCOMPARE(tracks(), before);
		QCOMPARE(channels(), 1);
	}

	void theSettingsDialogWritesThePreference()
	{
		for (const bool wanted : {true, false})
		{
			SetupDialog dialog;
			QCheckBox* box = nullptr;
			for (QCheckBox* candidate : dialog.findChildren<QCheckBox*>())
			{
				if (candidate->text() == SetupDialog::tr("Give each new instrument or sample track its own named mixer channel")) { box = candidate; }
			}
			QVERIFY2(box != nullptr, "no preference checkbox in the settings dialog");
			QCOMPARE(box->isChecked(), !wanted);
			box->click();
			static_cast<QDialog&>(dialog).accept();
			QCOMPARE(control::autoTrackChannelEnabled(), wanted);
		}
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(TrackAddRoutingTest)
#include "TrackAddRoutingTest.moc"
