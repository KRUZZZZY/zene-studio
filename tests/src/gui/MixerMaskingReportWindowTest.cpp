/*
 * MixerMaskingReportWindowTest.cpp - Mixer View > Masking Report..., driven like a person
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
/*! Q050-231.1 (R-mixing-7): the Masking Report window is mixer.masking_report's face. The menu entry opens it; ticking
 *  channels, choosing priorities and clicking Report lists each clash with its proposed cut, and changes no EQ and no
 *  undo depth. Runs the real application on a throwaway home. */

#include <QtTest>

#include <QApplication>
#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QWindow>

#include <cmath>
#include <memory>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "MixerWindows.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {}) { return ControlRegistry::instance()->invoke(id, args); }

QWidget* windowTitled(const QString& title)
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (widget->windowTitle().startsWith(title)) { return widget; }
	}
	return nullptr;
}

template<typename T>
T* childNamed(QWidget* parent, const QString& accessibleName)
{
	for (T* child : parent->findChildren<T*>())
	{
		if (child->accessibleName() == accessibleName) { return child; }
	}
	return nullptr;
}

//! A sample track on @a channel playing a @a hz tone over one second, so the reporting band sees it.
SampleTrack* toneTrack(int channel, double hz)
{
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	track->mixerChannelModel()->setValue(channel);
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	std::vector<SampleFrame> frames(static_cast<std::size_t>(rate));
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		const auto v = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / rate));
		frames[i] = SampleFrame(v, v);
	}
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(frames.data(), frames.size(), rate));
	return track;
}

} // namespace

class MixerMaskingReportWindowTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		while (Engine::mixer()->numChannels() < 4) { Engine::mixer()->createChannel(); }
		for (int i = 0; i < 4; ++i) { m_ch[i] = control::channelIdOf(Engine::mixer()->mixerChannel(i)); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Q050-231.1: Mixer > Masking Report... ticks two channels fed the same 200 Hz tone, gives them priorities, and the
	//! Report button lists the clash with the cut on the back channel - through mixer.masking_report, changing no EQ.
	void theMaskingReportWindowListsTheClashAndChangesNothing()
	{
		SampleTrack* lead = toneTrack(1, 200.0);
		SampleTrack* pad = toneTrack(2, 200.0);
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const float padMid = Engine::mixer()->mixerChannel(2)->m_eqMidModel.value();
		QMenu menu;
		addMixerWindows(&menu);
		QAction* open = menuAction(&menu, {QStringLiteral("Masking Report...")});
		QVERIFY(open != nullptr);
		QCOMPARE(open->property("controlCommand").toString(), QStringLiteral("mixer.masking_report"));
		open->trigger();
		QWidget* window = windowTitled(QStringLiteral("Masking Report"));
		QVERIFY(window != nullptr);
		auto* channels = childNamed<QTableWidget>(window, QStringLiteral("Channels"));
		auto* clashes = childNamed<QTableWidget>(window, QStringLiteral("Clashes"));
		auto* report = childNamed<QPushButton>(window, QStringLiteral("Report"));
		QVERIFY(channels != nullptr && clashes != nullptr && report != nullptr);
		const auto tick = [channels](const QString& id, int priority) {
			for (int row = 0; row < channels->rowCount(); ++row)
			{
				if (channels->item(row, 0)->data(Qt::UserRole).toString() != id) { continue; }
				channels->item(row, 0)->setCheckState(Qt::Checked);
				static_cast<QComboBox*>(channels->cellWidget(row, 1))->setCurrentIndex(priority);
				return true;
			}
			return false;
		};
		QVERIFY(tick(m_ch[1], 0));  // front
		QVERIFY(tick(m_ch[2], 2));  // back
		QTest::mouseClick(report, Qt::LeftButton);
		QVERIFY(clashes->rowCount() >= 1);
		const QJsonObject clash = clashes->item(0, 0)->data(Qt::UserRole).toJsonObject();
		QCOMPARE(clash.value(QStringLiteral("a")).toString(), m_ch[1]);
		QCOMPARE(clash.value(QStringLiteral("b")).toString(), m_ch[2]);
		QCOMPARE(clash.value(QStringLiteral("target")).toString(), m_ch[2]);
		QString padName;
		for (int row = 0; row < channels->rowCount(); ++row)
		{
			if (channels->item(row, 0)->data(Qt::UserRole).toString() == m_ch[2]) { padName = channels->item(row, 0)->text(); }
		}
		QVERIFY2(clashes->item(0, 2)->text().endsWith(QStringLiteral(" on ") + padName), qPrintable(clashes->item(0, 2)->text()));
		QVERIFY(clash.value(QStringLiteral("low_hz")).toDouble() <= 200.0 && clash.value(QStringLiteral("high_hz")).toDouble() > 200.0);
		QTest::mouseClick(report, Qt::LeftButton);  // a refresh: the same rows, still nothing written
		QCOMPARE(clashes->item(0, 0)->data(Qt::UserRole).toJsonObject(), clash);
		QCOMPARE(Engine::mixer()->mixerChannel(2)->m_eqMidModel.value(), padMid);
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth);
		window->close();
		delete lead;
		delete pad;
	}

	//! Q050-231.2: the selected clash's Apply writes exactly the strip EQ the report proposed, as one undo step, and
	//! control.undo takes it back. With no row selected the button is disabled and does nothing.
	void applyingAProposalFromTheWindowIsOneUndoStep()
	{
		SampleTrack* lead = toneTrack(1, 200.0);
		SampleTrack* pad = toneTrack(2, 200.0);
		MixerChannel* target = Engine::mixer()->mixerChannel(2);
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const double low = target->m_eqLowModel.value();
		const double mid = target->m_eqMidModel.value();
		const double midFreq = target->m_eqMidFreqModel.value();
		const double high = target->m_eqHighModel.value();
		const double otherLow = Engine::mixer()->mixerChannel(0)->m_eqLowModel.value();
		const double otherHigh = Engine::mixer()->mixerChannel(3)->m_eqHighModel.value();
		QMenu menu;
		addMixerWindows(&menu);
		QAction* open = menuAction(&menu, {QStringLiteral("Masking Report...")});
		QVERIFY(open != nullptr);
		open->trigger();
		QWidget* window = windowTitled(QStringLiteral("Masking Report"));
		QVERIFY(window != nullptr);
		auto* channels = childNamed<QTableWidget>(window, QStringLiteral("Channels"));
		auto* clashes = childNamed<QTableWidget>(window, QStringLiteral("Clashes"));
		auto* report = childNamed<QPushButton>(window, QStringLiteral("Report"));
		auto* apply = childNamed<QPushButton>(window, QStringLiteral("Apply proposal"));
		QVERIFY(channels != nullptr && clashes != nullptr && report != nullptr && apply != nullptr);
		QCOMPARE(apply->property("controlCommand").toString(), QStringLiteral("mixer.apply_masking_proposal"));
		const auto tick = [channels](const QString& id, int priority) {
			for (int row = 0; row < channels->rowCount(); ++row)
			{
				if (channels->item(row, 0)->data(Qt::UserRole).toString() != id) { continue; }
				channels->item(row, 0)->setCheckState(Qt::Checked);
				static_cast<QComboBox*>(channels->cellWidget(row, 1))->setCurrentIndex(priority);
				return true;
			}
			return false;
		};
		QVERIFY(tick(m_ch[1], 0));  // front
		QVERIFY(tick(m_ch[2], 2));  // back
		QVERIFY(!apply->isEnabled());
		QTest::mouseClick(report, Qt::LeftButton);
		QVERIFY(clashes->rowCount() >= 1);
		clashes->selectRow(0);
		QVERIFY(apply->isEnabled());
		const QJsonObject clash = clashes->item(0, 0)->data(Qt::UserRole).toJsonObject();
		const QJsonObject strip = clash.value(QStringLiteral("strip_eq")).toObject();
		QVERIFY(strip.contains(QStringLiteral("mid")));
		QCOMPARE(strip.value(QStringLiteral("channel")).toString(), m_ch[2]);
		QTest::mouseClick(apply, Qt::LeftButton);
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth + 1);
		QVERIFY(std::abs(static_cast<double>(target->m_eqMidModel.value()) - strip.value(QStringLiteral("mid")).toDouble()) < 1e-3);
		QVERIFY(std::abs(static_cast<double>(target->m_eqMidFreqModel.value()) - strip.value(QStringLiteral("mid_freq")).toDouble()) < 1e-3);
		QCOMPARE(static_cast<double>(target->m_eqLowModel.value()), low);
		QCOMPARE(static_cast<double>(target->m_eqHighModel.value()), high);
		QCOMPARE(static_cast<double>(Engine::mixer()->mixerChannel(0)->m_eqLowModel.value()), otherLow);
		QCOMPARE(static_cast<double>(Engine::mixer()->mixerChannel(3)->m_eqHighModel.value()), otherHigh);
		// The refresh after Apply cleared the selection: the button is disabled and a click on it writes nothing.
		QVERIFY(!apply->isEnabled());
		QTest::mouseClick(apply, Qt::LeftButton);
		QCOMPARE(static_cast<double>(target->m_eqMidModel.value()), strip.value(QStringLiteral("mid")).toDouble());
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<double>(target->m_eqLowModel.value()), low);
		QCOMPARE(static_cast<double>(target->m_eqMidModel.value()), mid);
		QCOMPARE(static_cast<double>(target->m_eqMidFreqModel.value()), midFreq);
		QCOMPARE(static_cast<double>(target->m_eqHighModel.value()), high);
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth);
		window->close();
		delete lead;
		delete pad;
	}

	//! Changing a ticked channel or a priority after Report drops the stale rows and disables Apply; Report brings them back.
	void changingChannelsOrPrioritiesClearsTheStaleReport()
	{
		SampleTrack* lead = toneTrack(1, 200.0);
		SampleTrack* pad = toneTrack(2, 200.0);
		MixerChannel* target = Engine::mixer()->mixerChannel(2);
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const double low = target->m_eqLowModel.value();
		const double mid = target->m_eqMidModel.value();
		const double midFreq = target->m_eqMidFreqModel.value();
		const double high = target->m_eqHighModel.value();
		QMenu menu;
		addMixerWindows(&menu);
		QAction* open = menuAction(&menu, {QStringLiteral("Masking Report...")});
		QVERIFY(open != nullptr);
		open->trigger();
		QWidget* window = windowTitled(QStringLiteral("Masking Report"));
		QVERIFY(window != nullptr);
		auto* channels = childNamed<QTableWidget>(window, QStringLiteral("Channels"));
		auto* clashes = childNamed<QTableWidget>(window, QStringLiteral("Clashes"));
		auto* report = childNamed<QPushButton>(window, QStringLiteral("Report"));
		auto* apply = childNamed<QPushButton>(window, QStringLiteral("Apply proposal"));
		QVERIFY(channels != nullptr && clashes != nullptr && report != nullptr && apply != nullptr);
		const auto rowOf = [channels](const QString& id) {
			for (int row = 0; row < channels->rowCount(); ++row)
			{
				if (channels->item(row, 0)->data(Qt::UserRole).toString() == id) { return row; }
			}
			return -1;
		};
		const auto tick = [channels, rowOf](const QString& id, int priority) {
			const int row = rowOf(id);
			if (row < 0) { return false; }
			channels->item(row, 0)->setCheckState(Qt::Checked);
			static_cast<QComboBox*>(channels->cellWidget(row, 1))->setCurrentIndex(priority);
			return true;
		};
		const auto summary = [window] { return window->findChild<QLabel*>()->text(); };
		const auto untouchedEq = [&] {
			return static_cast<double>(target->m_eqLowModel.value()) == low && static_cast<double>(target->m_eqMidModel.value()) == mid
				&& static_cast<double>(target->m_eqMidFreqModel.value()) == midFreq && static_cast<double>(target->m_eqHighModel.value()) == high
				&& run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt() == depth;
		};
		QVERIFY(tick(m_ch[1], 0));  // front
		QVERIFY(tick(m_ch[2], 2));  // back
		QTest::mouseClick(report, Qt::LeftButton);
		QVERIFY(clashes->rowCount() >= 1);
		clashes->selectRow(0);
		QVERIFY(apply->isEnabled());

		// Ch2's priority moves to front after the report: its rows are stale, so they go and Apply is disabled.
		static_cast<QComboBox*>(channels->cellWidget(rowOf(m_ch[2]), 1))->setCurrentIndex(0);
		QCOMPARE(clashes->rowCount(), 0);
		QVERIFY(!apply->isEnabled());
		QCOMPARE(summary(), QStringLiteral("Channel selection or priorities changed. Press Report again."));
		QTest::mouseClick(apply, Qt::LeftButton);
		QVERIFY(untouchedEq());

		QTest::mouseClick(report, Qt::LeftButton);  // the refresh brings the rows back
		QVERIFY(clashes->rowCount() >= 1);
		clashes->selectRow(0);
		QVERIFY(apply->isEnabled());

		// Unticking ch1 makes the old rows stale again.
		channels->item(rowOf(m_ch[1]), 0)->setCheckState(Qt::Unchecked);
		QCOMPARE(clashes->rowCount(), 0);
		QVERIFY(!apply->isEnabled());
		QTest::mouseClick(apply, Qt::LeftButton);
		QVERIFY(untouchedEq());

		// Invalid input (only ch2 ticked) through the real Report button: no rows, Apply disabled, a message, nothing written.
		QTest::mouseClick(report, Qt::LeftButton);
		QCOMPARE(clashes->rowCount(), 0);
		QVERIFY(!apply->isEnabled());
		QVERIFY(!summary().isEmpty());
		QTest::mouseClick(apply, Qt::LeftButton);
		QVERIFY(untouchedEq());

		window->close();
		delete lead;
		delete pad;
	}

	//! Ctrl+Z in the active Masking Report window takes back the applied cut in one step: MainWindow's application-wide Undo
	//! shortcut reaches the same journal as control.undo, so every EQ value and the undo depth return to their values before Apply.
	void ctrlZInTheReportWindowUndoesTheAppliedProposal()
	{
		SampleTrack* lead = toneTrack(1, 200.0);
		SampleTrack* pad = toneTrack(2, 200.0);
		MixerChannel* target = Engine::mixer()->mixerChannel(2);
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const double low = target->m_eqLowModel.value();
		const double mid = target->m_eqMidModel.value();
		const double midFreq = target->m_eqMidFreqModel.value();
		const double high = target->m_eqHighModel.value();
		getGUI()->mainWindow()->show();
		QTest::qWait(50);
		QMenu menu;
		addMixerWindows(&menu);
		QAction* open = menuAction(&menu, {QStringLiteral("Masking Report...")});
		QVERIFY(open != nullptr);
		open->trigger();
		QWidget* window = windowTitled(QStringLiteral("Masking Report"));
		QVERIFY(window != nullptr);
		auto* channels = childNamed<QTableWidget>(window, QStringLiteral("Channels"));
		auto* clashes = childNamed<QTableWidget>(window, QStringLiteral("Clashes"));
		auto* report = childNamed<QPushButton>(window, QStringLiteral("Report"));
		auto* apply = childNamed<QPushButton>(window, QStringLiteral("Apply proposal"));
		QVERIFY(channels != nullptr && clashes != nullptr && report != nullptr && apply != nullptr);
		const auto tick = [channels](const QString& id, int priority) {
			for (int row = 0; row < channels->rowCount(); ++row)
			{
				if (channels->item(row, 0)->data(Qt::UserRole).toString() != id) { continue; }
				channels->item(row, 0)->setCheckState(Qt::Checked);
				static_cast<QComboBox*>(channels->cellWidget(row, 1))->setCurrentIndex(priority);
				return true;
			}
			return false;
		};
		QVERIFY(tick(m_ch[1], 0));  // front
		QVERIFY(tick(m_ch[2], 2));  // back
		QTest::mouseClick(report, Qt::LeftButton);
		QVERIFY(clashes->rowCount() >= 1);
		clashes->selectRow(0);
		QVERIFY(apply->isEnabled());
		const QJsonObject clash = clashes->item(0, 0)->data(Qt::UserRole).toJsonObject();
		const QJsonObject strip = clash.value(QStringLiteral("strip_eq")).toObject();
		QTest::mouseClick(apply, Qt::LeftButton);
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth + 1);
		QVERIFY(std::abs(static_cast<double>(target->m_eqMidModel.value()) - strip.value(QStringLiteral("mid")).toDouble()) < 1e-3);
		QVERIFY(std::abs(static_cast<double>(target->m_eqMidModel.value()) - mid) > 1e-3);
		QAction* undoAction = nullptr;
		for (QAction* action : getGUI()->mainWindow()->findChildren<QAction*>())
		{
			if (action->data().toString() == QStringLiteral("control.undo")) { undoAction = action; break; }
		}
		QVERIFY(undoAction != nullptr);
		QVERIFY(undoAction->isEnabled());
		QCOMPARE(undoAction->shortcutContext(), Qt::ApplicationShortcut);

		window->raise();
		window->activateWindow();
		QVERIFY(QTest::qWaitForWindowActive(window));
		QApplication::setActiveWindow(window);
		apply->setFocus();
		QTest::qWait(50);
		QWindow* keyWindow = window->windowHandle();
		QVERIFY(keyWindow != nullptr);
		QTest::keyClick(keyWindow, Qt::Key_Z, Qt::ControlModifier);
		QTest::qWait(50);
		QTRY_COMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth);
		QCOMPARE(static_cast<double>(target->m_eqLowModel.value()), low);
		QCOMPARE(static_cast<double>(target->m_eqMidModel.value()), mid);
		QCOMPARE(static_cast<double>(target->m_eqMidFreqModel.value()), midFreq);
		QCOMPARE(static_cast<double>(target->m_eqHighModel.value()), high);
		QVERIFY(window->isVisible());
		window->close();
		delete lead;
		delete pad;
	}

private:
	QTemporaryDir m_home;
	QString m_ch[4];
};

QTEST_MAIN(MixerMaskingReportWindowTest)
#include "MixerMaskingReportWindowTest.moc"
