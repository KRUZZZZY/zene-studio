/*
 * LoopDropWarpTest.cpp - a dropped loop follows the project tempo and key, or keeps its own (Q050-093)
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
/*! 0.5.0 L05 (Q050-093), through the interface: Edit > Auto-Warp Dropped Loops is the remembered loop mode (it runs
 *  settings.set on import/loopwarp); a four-bar A major loop at 100 BPM dropped on a sample lane of a 140 BPM C major
 *  project then follows the project - four project bars, transposed +3 - and the status bar names the detected tempo
 *  and key. Unchecked, the next drop keeps its own rate; Alt held on a drop takes the other mode for that drop only.
 *  The browser's own sample drag takes the same path. Each drop is one undo step. */

#include <QtTest>

#include <cmath>
#include <memory>

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMimeData>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QUrl>

#include "../core/LoopFixture.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "ProjectJournal.h"
#include "ProjectKey.h"
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

constexpr int PixelsPerBar = 40;
constexpr tick_t FourBars = 4 * 192;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

TrackView* viewOf(const Track* track)
{
	for (TrackView* view : getGUI()->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return view; }
	}
	return nullptr;
}

//! Enter, then drop, \a mime on \a target at \a point with \a modifiers held.
bool dropOn(QWidget* target, QMimeData* mime, const QPoint& point, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	QDragEnterEvent enter(point, Qt::CopyAction, mime, Qt::LeftButton, modifiers);
	QCoreApplication::sendEvent(target, &enter);
	QDropEvent drop(QPointF(point), Qt::CopyAction, mime, Qt::LeftButton, modifiers);
	drop.setAccepted(false);
	QCoreApplication::sendEvent(target, &drop);
	return drop.isAccepted();
}

QString preference()
{
	return run(QStringLiteral("settings.get"), {{QStringLiteral("key"), QStringLiteral("import/loopwarp")}})
		.result.value(QStringLiteral("value")).toString();
}

} // namespace

class LoopDropWarpTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		QVERIFY(loopfixture::writeWave(m_home.filePath(QStringLiteral("a-major-100.wav")), loopfixture::loop(100.0, 220.0)));
		m_action = getGUI()->mainWindow()->findChild<QAction*>(QStringLiteral("loopWarpAction"));
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
		Engine::getSong()->setTempo(140);
		Engine::getSong()->projectKey().set(QStringLiteral("C"), 0, QStringLiteral("Major"), 1.0, 1.0,
			QStringLiteral("test"), QString());
		m_sample = Track::create(Track::Type::Sample, Engine::getSong());
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), PixelsPerBar}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();
		QVERIFY(viewOf(m_sample) != nullptr);
	}

	QString loop() const { return m_home.filePath(QStringLiteral("a-major-100.wav")); }

	//! Sets the Edit menu toggle to \a on by clicking it, as a user does.
	void setAutoWarp(bool on)
	{
		QVERIFY(m_action != nullptr);
		if (m_action->isChecked() != on) { m_action->trigger(); }
		QCOMPARE(m_action->isChecked(), on);
	}

	SampleClip* dropLoop(Qt::KeyboardModifiers modifiers = Qt::NoModifier)
	{
		QMimeData mime;
		mime.setUrls({QUrl::fromLocalFile(loop())});
		const int before = m_sample->numOfClips();
		if (!dropOn(viewOf(m_sample)->getTrackContentWidget(), &mime, QPoint(2 * PixelsPerBar, 10), modifiers)) { return nullptr; }
		if (m_sample->numOfClips() != before + 1) { return nullptr; }
		return dynamic_cast<SampleClip*>(m_sample->getClips().back());
	}

	void theEditMenuCarriesTheToggleAndItRunsSettingsSet()
	{
		QVERIFY2(m_action != nullptr, "Edit > Auto-Warp Dropped Loops is missing");
		QVERIFY(m_action->isCheckable());
		setAutoWarp(true);
		QCOMPARE(preference(), QStringLiteral("follow_project"));
		setAutoWarp(false);
		QCOMPARE(preference(), QStringLiteral("keep_unstretched"));
	}

	//! Acceptance 1 through a real drop: detected, followed, transposed to the project key, and said.
	void aLoopDroppedWithAutoWarpOnFollowsTheProjectTempoAndKey()
	{
		setAutoWarp(true);
		const int depth = Engine::projectJournal()->undoDepth();
		SampleClip* clip = dropLoop();
		QVERIFY2(clip != nullptr, "the drop made no clip");
		QVERIFY(std::abs(clip->loopSource().bpm - 100.0) < 0.5);
		QCOMPARE(clip->loopSource().tonicPitchClass, 9);
		QCOMPARE(clip->warpTempoMode(), WarpTempoMode::SourceTempo);
		QCOMPARE(clip->sourceTempo(), 100.0f);
		QCOMPARE(clip->clipEdits().transpose, 3);
		QVERIFY(std::abs(clip->length().getTicks() - FourBars) <= 1);
		QCOMPARE(static_cast<int>(Engine::getSong()->getTempo()), 140);
		QCOMPARE(Engine::getSong()->projectKey().tonicPitchClass(), 0);
		const QString message = getGUI()->mainWindow()->statusBar()->currentMessage();
		QVERIFY2(message.contains(QStringLiteral("BPM")) && message.contains(QStringLiteral("A Major"))
			&& message.contains(QStringLiteral("+3")), qPrintable(message));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one drop, one undo step
		QCOMPARE(m_sample->numOfClips(), 0);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth);
	}

	//! Acceptance 2 through a real drop: with the toggle off the loop keeps its own rate; Alt flips it per drop.
	void autoWarpOffKeepsTheLoopUnstretchedAndAltFlipsOneDrop()
	{
		setAutoWarp(false);
		SampleClip* kept = dropLoop();
		QVERIFY(kept != nullptr);
		QVERIFY(kept->rendersLinearly());
		QCOMPARE(kept->clipEdits().transpose, 0);
		QVERIFY(kept->loopSource().analysed && !kept->loopSource().followProject);
		QVERIFY(getGUI()->mainWindow()->statusBar()->currentMessage().contains(QStringLiteral("Kept unstretched")));

		SampleClip* flipped = dropLoop(Qt::AltModifier);
		QVERIFY(flipped != nullptr);
		QCOMPARE(flipped->warpTempoMode(), WarpTempoMode::SourceTempo);
		QCOMPARE(flipped->clipEdits().transpose, 3);
		QCOMPARE(preference(), QStringLiteral("keep_unstretched"));  // the override did not change the preference

		setAutoWarp(true);
		SampleClip* flippedBack = dropLoop(Qt::AltModifier);
		QVERIFY(flippedBack != nullptr);
		QVERIFY(flippedBack->rendersLinearly());
	}

	//! The browser's sample drag on a sample track takes the same command and the same mode.
	void theBrowsersSampleDragImportsThroughTheSameCommand()
	{
		setAutoWarp(true);
		std::unique_ptr<QMimeData> mime(StringPairDrag::createMimeData(QStringLiteral("samplefile"), loop()));
		const int depth = Engine::projectJournal()->undoDepth();
		dropOn(viewOf(m_sample), mime.get(), QPoint(300, 10));
		QCOMPARE(m_sample->numOfClips(), 1);
		auto* clip = dynamic_cast<SampleClip*>(m_sample->getClip(0));
		QVERIFY(clip != nullptr);
		QVERIFY(clip->loopSource().analysed);
		QCOMPARE(clip->warpTempoMode(), WarpTempoMode::SourceTempo);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_sample->numOfClips(), 0);
	}

private:
	QTemporaryDir m_home;
	QAction* m_action = nullptr;
	Track* m_sample = nullptr;
};

QTEST_MAIN(LoopDropWarpTest)
#include "LoopDropWarpTest.moc"
