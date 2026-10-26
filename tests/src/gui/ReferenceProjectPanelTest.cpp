/*
 * ReferenceProjectPanelTest.cpp - the reference project's window and its three copies into this project
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

/*! The reference project (another project opened beside this one) shows in a window: its tracks with their clips and
 *  devices, and its mixer channels with theirs. Three buttons copy a selection into this project - tracks, clips onto
 *  a chosen track (where they were, or at the play position), devices onto a chosen chain - each by running a
 *  reference.* command, so a copy made here is one undo step like the socket's. The window follows the reference as it
 *  is opened, changed and closed, and says what is wrong with a selection instead of copying something else. */

#include <QtTest>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Mixer.h"
#include "ReferenceProject.h"
#include "ReferenceProjectPanel.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "../core/TakeLaneTestSupport.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QPushButton* buttonTexted(QWidget* window, const QString& text)
{
	for (QPushButton* button : window->findChildren<QPushButton*>())
	{
		if (button->text() == text) { return button; }
	}
	return nullptr;
}

//! The tree item named @a name anywhere under the tree's top level, or nullptr.
QTreeWidgetItem* itemNamed(QTreeWidget* tree, const QString& name)
{
	const QList<QTreeWidgetItem*> found = tree->findItems(name, Qt::MatchExactly | Qt::MatchRecursive, 0);
	return found.isEmpty() ? nullptr : found.first();
}

QString pathLabelText(QWidget* panel)
{
	// The path label is the first label above the tree.
	for (QLabel* label : panel->findChildren<QLabel*>())
	{
		if (label->text().startsWith(QStringLiteral("No reference")) || label->text().contains(QStringLiteral(".mmp"))
			|| label->text().startsWith(QStringLiteral("Select"))
			|| label->text().startsWith(QStringLiteral("The reference project could not be read"))) { return label->text(); }
	}
	return {};
}

int targetIndexForId(QComboBox* targets, const QString& id)
{
	return targets->findData(id);
}

} // namespace

class ReferenceProjectPanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
		m_tone = taketest::writeToneWav(QDir(m_home.path()).filePath(QStringLiteral("tone.wav")), 440.0);
		QVERIFY(!m_tone.isEmpty());
		m_source = writeSourceProject();
		QVERIFY(!m_source.isEmpty());
	}

	void cleanupTestCase()
	{
		ReferenceProject::instance().close();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		ReferenceProject::instance().close();
		clearSong();
		QCoreApplication::processEvents();
	}

	void withNoReferenceTheWindowSaysSoAndListsTheTargets()
	{
		Track* mine = newTrack(Track::Type::Sample, QStringLiteral("Mine"));
		ReferenceProjectPanel* panel = ReferenceProjectPanel::showPanel(nullptr);
		QVERIFY(panel != nullptr);
		QVERIFY(panel->isVisible());
		QCOMPARE(ReferenceProjectPanel::showPanel(nullptr), panel);  // the one panel, made once
		QCOMPARE(panel->windowTitle(), QStringLiteral("Reference Project"));
		QCOMPARE(pathLabelText(panel), QStringLiteral("No reference project is open."));
		QCOMPARE(panel->tree()->topLevelItemCount(), 0);

		QComboBox* targets = panel->targets();
		QVERIFY(targets->findText(QStringLiteral("Track: Mine")) >= 0);
		QVERIFY(targets->findText(QStringLiteral("Master channel")) >= 0);
		QCOMPARE(targets->count(), int(Engine::getSong()->tracks().size()) + int(Engine::mixer()->numChannels()));
		QCOMPARE(targets->itemData(targets->findText(QStringLiteral("Track: Mine"))).toString(), control::trackIdOf(mine));

		// A target the user chose is kept across a refresh while it still exists.
		targets->setCurrentIndex(targets->findText(QStringLiteral("Master channel")));
		panel->refresh();
		QCOMPARE(targets->currentText(), QStringLiteral("Master channel"));
		ReferenceProjectPanel::refreshIfOpen();
		QCOMPARE(targets->currentText(), QStringLiteral("Master channel"));
	}

	void theTreeShowsTheReferencesTracksClipsAndDevices()
	{
		ReferenceProjectPanel* panel = openReference();
		QTreeWidget* tree = panel->tree();
		QVERIFY(pathLabelText(panel).contains(QStringLiteral("source.mmp")));
		QTreeWidgetItem* vox = itemNamed(tree, QStringLiteral("Vox"));
		QTreeWidgetItem* lead = itemNamed(tree, QStringLiteral("Lead"));
		QVERIFY(vox != nullptr && lead != nullptr);
		QCOMPARE(vox->text(1), QStringLiteral("sample"));
		QCOMPARE(vox->childCount(), 1);
		QVERIFY2(vox->child(0)->text(1).endsWith(QStringLiteral("at bar 2")), qPrintable(vox->child(0)->text(1)));
		QTreeWidgetItem* mixer = itemNamed(tree, QStringLiteral("Mixer"));
		QVERIFY(mixer != nullptr);
		QVERIFY(mixer->childCount() >= 1);
		QCOMPARE(mixer->child(0)->text(1), QStringLiteral("channel 0"));
		QCOMPARE(mixer->child(0)->childCount(), hasAmplifier() ? 1 : 0);  // the master carries the source's one device, where it can load

		// The change hook keeps the open window current: a closed reference empties it.
		QVERIFY(run(QStringLiteral("reference.close")).ok);
		QCOMPARE(pathLabelText(panel), QStringLiteral("No reference project is open."));
		QCOMPARE(tree->topLevelItemCount(), 0);
	}

	void copyTracksCopiesTheSelectedTracksInOneStep()
	{
		ReferenceProjectPanel* panel = openReference();
		const std::size_t before = Engine::getSong()->tracks().size();
		buttonTexted(panel, QStringLiteral("Copy Tracks"))->click();  // nothing selected
		QCOMPARE(pathLabelText(panel), QStringLiteral("Select the tracks to copy first."));
		QCOMPARE(Engine::getSong()->tracks().size(), before);

		panel->tree()->clearSelection();
		itemNamed(panel->tree(), QStringLiteral("Lead"))->setSelected(true);
		itemNamed(panel->tree(), QStringLiteral("Vox"))->setSelected(true);
		buttonTexted(panel, QStringLiteral("Copy Tracks"))->click();
		QCOMPARE(Engine::getSong()->tracks().size(), before + 2);
		QVERIFY(undoOne());  // one step takes both back
		QCOMPARE(Engine::getSong()->tracks().size(), before);
	}

	void copyClipsLandsThemOnTheChosenTrackWhereTheyWereOrAtThePlayPosition()
	{
		ReferenceProjectPanel* panel = openReference();
		Track* target = newTrack(Track::Type::Sample, QStringLiteral("Target"));
		panel->refresh();
		panel->targets()->setCurrentIndex(targetIndexForId(panel->targets(), control::trackIdOf(target)));
		QTreeWidgetItem* clipItem = itemNamed(panel->tree(), QStringLiteral("Vox"))->child(0);

		buttonTexted(panel, QStringLiteral("Copy Clips"))->click();  // none selected
		QCOMPARE(pathLabelText(panel), QStringLiteral("Select the clips to copy first."));
		QCOMPARE(target->getClips().size(), std::size_t{0});

		panel->tree()->clearSelection();
		clipItem->setSelected(true);
		buttonTexted(panel, QStringLiteral("Copy Clips"))->click();
		QCOMPARE(target->getClips().size(), std::size_t{1});
		QCOMPARE(target->getClips().front()->startPosition().getTicks(), 192);  // where it was
		QVERIFY(undoOne());
		QCOMPARE(target->getClips().size(), std::size_t{0});

		// At the play position instead.
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 960}}).ok);  // the play position, as the transport bar has it
		panel->findChild<QCheckBox*>()->setChecked(true);
		panel->tree()->clearSelection();
		itemNamed(panel->tree(), QStringLiteral("Vox"))->child(0)->setSelected(true);
		buttonTexted(panel, QStringLiteral("Copy Clips"))->click();
		QCOMPARE(target->getClips().size(), std::size_t{1});
		QCOMPARE(target->getClips().front()->startPosition().getTicks(), 960);
		panel->findChild<QCheckBox*>()->setChecked(false);
	}

	void clipsOfTwoTracksAreNotCopiedTogether()
	{
		for (const char* name : {"First", "Second"})
		{
			Track* track = newTrack(Track::Type::Sample, QLatin1String(name));
			dynamic_cast<SampleClip*>(track->createClip(TimePos(0)))->setSampleFile(m_tone);
		}
		const QString both = QDir(m_home.path()).filePath(QStringLiteral("both.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(both));
		clearSong();
		QVERIFY(run(QStringLiteral("reference.open"), {{QStringLiteral("path"), both}}).ok);
		ReferenceProjectPanel* panel = ReferenceProjectPanel::showPanel(nullptr);
		Track* target = newTrack(Track::Type::Sample, QStringLiteral("Target"));
		panel->refresh();
		panel->targets()->setCurrentIndex(targetIndexForId(panel->targets(), control::trackIdOf(target)));
		panel->tree()->clearSelection();
		itemNamed(panel->tree(), QStringLiteral("First"))->child(0)->setSelected(true);
		itemNamed(panel->tree(), QStringLiteral("Second"))->child(0)->setSelected(true);
		buttonTexted(panel, QStringLiteral("Copy Clips"))->click();
		QCOMPARE(pathLabelText(panel), QStringLiteral("Select clips of one owner at a time."));
		QCOMPARE(target->getClips().size(), std::size_t{0});

		// Tracks, though, are copied together whatever their owner (they have none).
		panel->tree()->clearSelection();
		itemNamed(panel->tree(), QStringLiteral("First"))->setSelected(true);
		itemNamed(panel->tree(), QStringLiteral("Second"))->setSelected(true);
		const std::size_t before = Engine::getSong()->tracks().size();
		buttonTexted(panel, QStringLiteral("Copy Tracks"))->click();
		QCOMPARE(Engine::getSong()->tracks().size(), before + 2);
	}

	void copyDevicesPutsTheSelectedDevicesOnTheChosenChain()
	{
		if (!hasAmplifier()) { QSKIP("this host loads no effect module"); }
		ReferenceProjectPanel* panel = openReference();
		QTreeWidgetItem* device = itemNamed(panel->tree(), QStringLiteral("Mixer"))->child(0)->child(0);
		QCOMPARE(device->text(1), QStringLiteral("device"));
		EffectChain& master = Engine::mixer()->mixerChannel(0)->m_fxChain;
		master.clear();
		const std::size_t before = master.effects().size();
		panel->targets()->setCurrentIndex(panel->targets()->findText(QStringLiteral("Master channel")));
		buttonTexted(panel, QStringLiteral("Copy Devices"))->click();  // nothing selected
		QCOMPARE(pathLabelText(panel), QStringLiteral("Select the devices to copy first."));
		panel->tree()->clearSelection();
		device->setSelected(true);
		buttonTexted(panel, QStringLiteral("Copy Devices"))->click();
		QCOMPARE(master.effects().size(), before + 1);
		QVERIFY(undoOne());
		QCOMPARE(master.effects().size(), before);
	}

	void theButtonsOpenAndCloseTheReference()
	{
		ReferenceProjectPanel* panel = openReference();
		buttonTexted(panel, QStringLiteral("Close Reference"))->click();
		QCOMPARE(pathLabelText(panel), QStringLiteral("No reference project is open."));
		QCOMPARE(panel->tree()->topLevelItemCount(), 0);

		// Open... asks for a file; the file dialog answers with the source project.
		answerFileDialog(m_source);
		buttonTexted(panel, QStringLiteral("Open..."))->click();
		QTRY_VERIFY(pathLabelText(panel).contains(QStringLiteral("source.mmp")));
		QVERIFY(itemNamed(panel->tree(), QStringLiteral("Lead")) != nullptr);

		// A cancelled dialog opens nothing and reports it.
		QVERIFY(run(QStringLiteral("reference.close")).ok);
		answerFileDialog(QString());
		QVERIFY(!ReferenceProjectPanel::openWithDialog(panel));
		QCOMPARE(pathLabelText(panel), QStringLiteral("No reference project is open."));
		// A file the command refuses is not shown as open.
		const QString notAProject = QDir(m_home.path()).filePath(QStringLiteral("nope.mmp"));
		QFile bad(notAProject);
		QVERIFY(bad.open(QIODevice::WriteOnly));
		bad.write("not xml");
		bad.close();
		answerFileDialog(notAProject);
		QVERIFY(!ReferenceProjectPanel::openWithDialog(panel));
	}

	void openWithoutAnErrorSinkRefusesInsteadOfCrashing()
	{
		// The why out-parameter is optional: a caller that does not want the reason passes nullptr (BUG 9, cluster
		// new-p9-383). Both refusals - a missing file and a file with no song tracks - must return false, not write through it.
		ReferenceProject& reference = ReferenceProject::instance();
		const QString missing = QDir(m_home.path()).filePath(QStringLiteral("missing.mmp"));
		QVERIFY(!reference.open(missing, nullptr));
		QVERIFY(!reference.isOpen());

		const QString notAProject = QDir(m_home.path()).filePath(QStringLiteral("trackless.mmp"));
		QFile bad(notAProject);
		QVERIFY(bad.open(QIODevice::WriteOnly));
		bad.write("not xml");
		bad.close();
		QVERIFY(!reference.open(notAProject, nullptr));
		QVERIFY(!reference.isOpen());

		// With a sink the reason is still written.
		QString why;
		QVERIFY(!reference.open(missing, &why));
		QVERIFY2(why.contains(QStringLiteral("no project file")), qPrintable(why));
	}

	void aReferenceChangedOnDiskIsMarkedStale()
	{
		ReferenceProjectPanel* panel = openReference();
		QFile source(m_source);
		QVERIFY(source.open(QIODevice::ReadWrite | QIODevice::Append));
		source.write("\n");
		QVERIFY(source.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
		source.close();
		panel->refresh();
		QVERIFY2(pathLabelText(panel).contains(QStringLiteral("changed on disk since it was opened")), qPrintable(pathLabelText(panel)));
	}

	void aFailedStateQuerySaysSoAndKeepsTheLastTree()
	{
		ReferenceProjectPanel* panel = openReference();
		QVERIFY(itemNamed(panel->tree(), QStringLiteral("Lead")) != nullptr);

		// The registry refuses every command that needs the engine until the model is up (ControlRegistry.cpp:283-291),
		// the same typed failure a refused reference.get_state hands the panel: no time or race in the test.
		ControlRegistry::setReady(false);
		panel->refresh();
		ControlRegistry::setReady(true);

		const QString shown = pathLabelText(panel);
		QVERIFY2(shown.startsWith(QStringLiteral("The reference project could not be read:")), qPrintable(shown));
		QVERIFY2(!shown.contains(QStringLiteral("No reference project is open")), qPrintable(shown));
		// The last read stays: a failed query is not a closed reference.
		QVERIFY(itemNamed(panel->tree(), QStringLiteral("Lead")) != nullptr);

		// The next good query recovers the usual view.
		panel->refresh();
		QVERIFY2(pathLabelText(panel).contains(QStringLiteral("source.mmp")), qPrintable(pathLabelText(panel)));
	}

private:
	//! An empty song. A track cleared before the arranger has built its view (a queued trackAdded, with a raw Track*:
	//! src/gui/editors/TrackContainerView.cpp:124-126) leaves a view over a freed model, and the next periodic update
	//! crashes in InstrumentTrackView::corruptStateUpdate (src/gui/tracks/InstrumentTrackView.cpp:437) - so every track
	//! here is made by newTrack(), which lets the view be built first.
	//! One undo, after the arranger has built the views of whatever the step being undone made (see clearSong()).
	static bool undoOne()
	{
		QCoreApplication::processEvents();
		const bool ok = run(QStringLiteral("control.undo")).ok;
		QCoreApplication::processEvents();
		return ok;
	}

	static Track* newTrack(Track::Type type, const QString& name)
	{
		Track* track = Track::create(type, Engine::getSong());
		track->setName(name);
		QCoreApplication::processEvents();  // the arranger builds the view from a queued signal; see clearSong()
		return track;
	}

	static void clearSong()
	{
		QCoreApplication::processEvents();  // tracks made by a command get their views before they are cleared
		Engine::getSong()->clearProject();
		QCoreApplication::processEvents();
	}

	bool hasAmplifier()
	{
		EffectChain& chain = Engine::mixer()->mixerChannel(0)->m_fxChain;
		Effect* probe = Effect::instantiate(QStringLiteral("amplifier"), &chain, nullptr);
		const bool ok = probe != nullptr && probe->isOkay();
		delete probe;
		return ok;
	}

	QString writeSourceProject()
	{
		clearSong();
		newTrack(Track::Type::Instrument, QStringLiteral("Lead"));
		Track* vox = newTrack(Track::Type::Sample, QStringLiteral("Vox"));
		auto* clip = dynamic_cast<SampleClip*>(vox->createClip(TimePos(192)));
		clip->setSampleFile(m_tone);
		if (hasAmplifier())
		{
			EffectChain& master = Engine::mixer()->mixerChannel(0)->m_fxChain;
			master.clear();
			master.appendEffect(Effect::instantiate(QStringLiteral("amplifier"), &master, nullptr));
		}
		const QString path = QDir(m_home.path()).filePath(QStringLiteral("source.mmp"));
		Engine::getSong()->saveProjectFile(path);
		return path;
	}

	ReferenceProjectPanel* openReference()
	{
		const ControlResult opened = run(QStringLiteral("reference.open"), {{QStringLiteral("path"), m_source}});
		if (!opened.ok) { QTest::qFail(qPrintable(QStringLiteral("reference.open failed: ") + opened.errorMessage), __FILE__, __LINE__); }
		ReferenceProjectPanel* panel = ReferenceProjectPanel::showPanel(nullptr);
		panel->refresh();
		return panel;
	}

	//! Answers the next file dialog with @a path (cancels it when empty).
	static void answerFileDialog(const QString& path, int triesLeft = 100)
	{
		QTimer::singleShot(10, [path, triesLeft] {
			auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
			if (dialog == nullptr) { if (triesLeft > 0) { answerFileDialog(path, triesLeft - 1); } return; }
			if (path.isEmpty()) { dialog->reject(); return; }
			dialog->selectFile(path);
			static_cast<QDialog*>(dialog)->accept();
		});
	}

	QTemporaryDir m_home;
	QString m_tone;
	QString m_source;
};

QTEST_MAIN(ReferenceProjectPanelTest)
#include "ReferenceProjectPanelTest.moc"
