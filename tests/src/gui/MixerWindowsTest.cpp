/*
 * MixerWindowsTest.cpp - the mixer's windows and channel-menu parts: routing matrix, large meters, EQ compare,
 *                        hardware I/O and surround menus, and the surround panner
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

/*! Every window and menu here is a face of a registered command, so each case drives the widget the way a person
 *  does (a click, a key, a menu entry, an Enter on a cell) and reads the result back through the registry: the
 *  routing matrix's sparse model and its sender filter, the activated cell that opens mixer.send_to's form, the
 *  Large Meters and Compare EQ windows, the Hardware I/O and Surround submenus, and the surround panner whose
 *  every drag, key and slider is one mixer.surround_pan call. Runs the real application on a throwaway home. */

#include <QtTest>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSlider>
#include <QTableView>
#include <QTemporaryDir>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "MixerHardwareMenu.h"
#include "MixerRoutingMatrix.h"
#include "MixerWindows.h"
#include "SurroundPanner.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The top-level widget titled (or starting with) @a title, or nullptr.
QWidget* windowTitled(const QString& title)
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (widget->windowTitle().startsWith(title)) { return widget; }
	}
	return nullptr;
}

int windowsTitled(const QString& title)
{
	int count = 0;
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (widget->windowTitle().startsWith(title)) { ++count; }
	}
	return count;
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

QJsonObject channelState(const QString& command, const QString& channel)
{
	const QJsonArray channels = run(command, {{QStringLiteral("channel"), channel}}).result.value(QStringLiteral("channels")).toArray();
	return channels.isEmpty() ? QJsonObject() : channels.at(0).toObject();
}

} // namespace

class MixerWindowsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
		while (Engine::mixer()->numChannels() < 4) { Engine::mixer()->createChannel(); }
		for (int i = 0; i < 4; ++i) { m_ch[i] = control::channelIdOf(Engine::mixer()->mixerChannel(i)); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theMatrixModelKeepsOnlyTheSendsThatExist()
	{
		const QJsonArray channels{
			QJsonObject{{QStringLiteral("channel"), QStringLiteral("ch-0")}, {QStringLiteral("name"), QStringLiteral("Master")},
				{QStringLiteral("sends"), QJsonArray{}}},
			QJsonObject{{QStringLiteral("channel"), QStringLiteral("ch-1")}, {QStringLiteral("name"), QStringLiteral("Drums")},
				{QStringLiteral("sends"), QJsonArray{QJsonObject{{QStringLiteral("to"), QStringLiteral("ch-2")}, {QStringLiteral("amount"), 0.5},
					{QStringLiteral("pre_fader"), true}, {QStringLiteral("enabled"), false}},
					QJsonObject{{QStringLiteral("to"), QStringLiteral("ch-9")}, {QStringLiteral("amount"), 1.0}}}}},
			QJsonObject{{QStringLiteral("channel"), QStringLiteral("ch-2")}, {QStringLiteral("name"), QStringLiteral("Verb")},
				{QStringLiteral("sends"), QJsonArray{}}}};
		RoutingMatrixModel model;
		model.setChannels(channels, {QStringLiteral("Master"), QString(), QStringLiteral("Return")});
		QCOMPARE(model.rowCount(), 3);
		QCOMPARE(model.columnCount(), 3);
		QCOMPARE(model.rowCount(model.index(0, 0)), 0);  // a flat table: a cell has no children
		QCOMPARE(model.storedCells(), 1);                // the send to an unknown channel is dropped
		QCOMPARE(model.channelId(2), QStringLiteral("ch-2"));
		QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Master (Master)"));
		QCOMPARE(model.headerData(1, Qt::Vertical).toString(), QStringLiteral("Drums"));
		QCOMPARE(model.headerData(2, Qt::Horizontal).toString(), QStringLiteral("Verb (Return)"));
		QCOMPARE(model.headerData(1, Qt::Vertical, Qt::ToolTipRole).toString(), QStringLiteral("Drums sends along this row"));
		QCOMPARE(model.headerData(2, Qt::Horizontal, Qt::ToolTipRole).toString(), QStringLiteral("Sends into Verb (Return) down this column"));
		QVERIFY(!model.headerData(0, Qt::Horizontal, Qt::DecorationRole).isValid());

		const QModelIndex send = model.index(1, 2);
		QCOMPARE(model.data(send).toString(), QStringLiteral("0.50 pre (off)"));
		QCOMPARE(model.data(send, RoutingMatrixModel::AmountRole).toDouble(), 0.5);
		QCOMPARE(model.data(send, RoutingMatrixModel::NameRole).toString(), QStringLiteral("Drums"));
		QCOMPARE(model.data(send, Qt::ToolTipRole).toString(), QStringLiteral("Drums to Verb: 0.50 pre (off)"));
		const QModelIndex none = model.index(2, 1);
		QVERIFY(!model.data(none).isValid());
		QVERIFY(!model.data(none, RoutingMatrixModel::AmountRole).isValid());
		QCOMPARE(model.data(none, Qt::AccessibleTextRole).toString(), QStringLiteral("Verb to Drums: no send (Enter makes one)"));
		QCOMPARE(model.data(model.index(1, 1), Qt::ToolTipRole).toString(), QStringLiteral("A channel cannot send to itself"));
		QVERIFY(!model.data(QModelIndex()).isValid());
		QCOMPARE(model.flags(model.index(1, 1)), Qt::ItemFlags(Qt::NoItemFlags));
		QVERIFY(model.flags(send) & Qt::ItemIsEnabled);
	}

	void theRoutingMatrixWindowFiltersSendersAndAnActivatedCellMakesASend()
	{
		showRoutingMatrix();
		QWidget* window = windowTitled(QStringLiteral("Routing Matrix"));
		QVERIFY(window != nullptr);
		showRoutingMatrix();  // asked for again: the same window, raised
		QCOMPARE(windowsTitled(QStringLiteral("Routing Matrix")), 1);

		auto* view = window->findChild<QTableView*>();
		auto* filter = window->findChild<QLineEdit*>();
		QVERIFY(view != nullptr && filter != nullptr);
		QCOMPARE(view->property("controlCommand").toString(), QStringLiteral("routing.matrix"));
		const int all = view->model()->rowCount();
		QVERIFY(all >= 4);
		filter->setText(QStringLiteral("zzz-no-such-sender"));
		QCOMPARE(view->model()->rowCount(), 0);
		filter->clear();
		QCOMPARE(view->model()->rowCount(), all);

		// Enter on an empty cell asks mixer.send_to for the amount, from 1.0; OK makes the send.
		const QModelIndex cell = view->model()->index(1, 2);
		emit view->activated(cell);
		auto* form = qobject_cast<CommandForm*>(formOpen());
		QVERIFY2(form != nullptr, "activating a cell opened no send form");
		form->accept();
		QTRY_VERIFY(sendExists(1, 2));
		// The diagonal is no send at all.
		const int formsBefore = formCount();
		emit view->activated(view->model()->index(1, 1));
		QCOMPARE(formCount(), formsBefore);
		// The window re-reads and shows the new send.
		QTRY_VERIFY(!view->model()->index(1, 2).data(Qt::DisplayRole).toString().isEmpty());
		window->close();
	}

	void theLargeMetersWindowReadsTheMasterAndTheLoudnessMeter()
	{
		QMenu menu;
		addMixerWindows(&menu);
		QAction* meters = menuAction(&menu, {QStringLiteral("Large Meters...")});
		QVERIFY(meters != nullptr);
		QCOMPARE(meters->property("controlCommand").toString(), QStringLiteral("meter.get_state"));
		meters->trigger();
		QWidget* window = windowTitled(QStringLiteral("Large Meters"));
		QVERIFY(window != nullptr);
		meters->trigger();
		QCOMPARE(windowsTitled(QStringLiteral("Large Meters")), 1);
		QVERIFY(window->grab().width() > 0);  // paints with the master at silence
		QVERIFY2(window->toolTip().contains(QStringLiteral("Loudness")), qPrintable(window->toolTip()));
		const QString disarmed = window->toolTip();

		MixerChannel* master = Engine::mixer()->mixerChannel(0);
		master->m_rmsLeft.set(0.5f);
		master->m_rmsRight.set(0.9f);
		QVERIFY(!window->grab().isNull());  // loud bars in the mid and high colours
		master->m_rmsLeft.set(0.f);
		master->m_rmsRight.set(0.f);

		QVERIFY(run(QStringLiteral("meter.arm"), {{QStringLiteral("enabled"), true}}).ok);
		QTRY_VERIFY_WITH_TIMEOUT(window->toolTip() != disarmed, 3000);
		QVERIFY(run(QStringLiteral("meter.arm"), {{QStringLiteral("enabled"), false}}).ok);
		window->close();
	}

	void theCompareEqWindowComparesTwoChannelsThroughTheCommand()
	{
		QMenu menu;
		addMixerWindows(&menu);
		QAction* compare = menuAction(&menu, {QStringLiteral("Compare Channel EQ...")});
		QVERIFY(compare != nullptr);
		compare->trigger();
		QWidget* window = windowTitled(QStringLiteral("Compare Channel EQ"));
		QVERIFY(window != nullptr);
		QCOMPARE(window->property("controlCommand").toString(), QStringLiteral("mixer.eq_compare"));
		auto* a = childNamed<QComboBox>(window, QStringLiteral("Channel A"));
		auto* b = childNamed<QComboBox>(window, QStringLiteral("Channel B"));
		QVERIFY(a != nullptr && b != nullptr);
		QVERIFY(a->count() >= 4);
		QCOMPARE(b->currentIndex(), 1);  // B starts on the second channel
		QString summary;
		for (QLabel* label : window->findChildren<QLabel*>())
		{
			if (label->text().startsWith(QStringLiteral("B - A:")) || label->text().startsWith(QStringLiteral("Could not compare"))) { summary = label->text(); }
		}
		QVERIFY2(!summary.isEmpty(), "the window said nothing about the comparison");
		a->setCurrentIndex(2);  // recompares
		QVERIFY(!window->grab().isNull());
		window->close();
	}

	void theHardwareMenuOffersTheOutputsAndSwitchesTheRoute()
	{
		QMenu menu;
		addHardwareMenu(&menu, m_ch[1]);
		QAction* none = menuAction(&menu, {QStringLiteral("Hardware I/O"), QStringLiteral("Output"), QStringLiteral("None")});
		QAction* standard = menuAction(&menu, {QStringLiteral("Hardware I/O"), QStringLiteral("Output"), QStringLiteral("Default")});
		QVERIFY(none != nullptr && standard != nullptr);
		QVERIFY(none->isCheckable() && standard->isCheckable());
		QVERIFY(standard->isChecked() && !none->isChecked());
		QCOMPARE(none->property("controlCommand").toString(), QStringLiteral("mixer.hardware_output"));

		none->trigger();
		const QJsonObject output = channelState(QStringLiteral("mixer.hardware_get"), m_ch[1]).value(QStringLiteral("output")).toObject();
		QCOMPARE(output.value(QStringLiteral("channels")).toString(), QStringLiteral("none"));
		QMenu again;
		addHardwareMenu(&again, m_ch[1]);
		QVERIFY(menuAction(&again, {QStringLiteral("Hardware I/O"), QStringLiteral("Output"), QStringLiteral("None")})->isChecked());
		QVERIFY(!menuAction(&again, {QStringLiteral("Hardware I/O"), QStringLiteral("Output"), QStringLiteral("Default")})->isChecked());
		QVERIFY(run(QStringLiteral("control.undo")).ok);

		// The other entries are the commands' own forms and reads; a latency probe needs an insert switched on.
		QAction* ping = menuAction(&menu, {QStringLiteral("Hardware I/O"), QStringLiteral("Measure Insert Latency")});
		QVERIFY(ping != nullptr);
		QVERIFY(!ping->isEnabled());
		for (const char* entry : {"Hardware Insert...", "Cue Mixes...", "Interface Channels...", "Hardware Routing"})
		{
			QAction* action = menuAction(&menu, {QStringLiteral("Hardware I/O"), QLatin1String(entry)});
			QVERIFY2(action != nullptr, entry);
			QVERIFY(!action->property("controlCommand").toString().isEmpty());
		}
		QMenu* cueSends = menuAction(&menu, {QStringLiteral("Hardware I/O"), QStringLiteral("Cue Sends")})->menu();
		QVERIFY(cueSends != nullptr);

		// "Hardware Insert..." asks its form.
		const int formsBefore = formCount();
		menuAction(&menu, {QStringLiteral("Hardware I/O"), QStringLiteral("Hardware Insert...")})->trigger();
		QCOMPARE(formCount(), formsBefore + 1);
		closeForms();
	}

	void theCueSendEntriesFollowTheCueMixes()
	{
		const ControlResult master = run(QStringLiteral("mixer.cue_master"), {{QStringLiteral("cue"), QStringLiteral("A")},
			{QStringLiteral("name"), QStringLiteral("Singer")}});
		if (!master.ok) { QSKIP("this build offers no cue mix"); }
		QMenu menu;
		addHardwareMenu(&menu, m_ch[2]);
		QAction* send = menuAction(&menu, {QStringLiteral("Hardware I/O"), QStringLiteral("Cue Sends"), QStringLiteral("Singer (A)...")});
		QVERIFY2(send != nullptr, "the named cue mix has no send entry");
		const int formsBefore = formCount();
		send->trigger();
		QCOMPARE(formCount(), formsBefore + 1);
		closeForms();
	}

	void theSurroundMenuSetsTheFormatAndOpensThePanner()
	{
		QMenu menu;
		addSurroundMenu(&menu, m_ch[2]);
		QAction* format = menuAction(&menu, {QStringLiteral("Surround"), QStringLiteral("Format"), QStringLiteral("5.1")});
		QVERIFY(format != nullptr);
		format->trigger();
		QCOMPARE(channelState(QStringLiteral("mixer.surround_get"), m_ch[2]).value(QStringLiteral("format")).toString(), QStringLiteral("5.1"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channelState(QStringLiteral("mixer.surround_get"), m_ch[2]).value(QStringLiteral("format")).toString(), QStringLiteral("stereo"));

		QAction* panner = menuAction(&menu, {QStringLiteral("Surround"), QStringLiteral("Surround Panner...")});
		QVERIFY(panner != nullptr);
		QCOMPARE(panner->property("controlCommand").toString(), QStringLiteral("mixer.surround_pan"));
		panner->trigger();
		QWidget* window = windowTitled(QStringLiteral("Surround Panner"));
		QVERIFY(window != nullptr);
		window->close();
		QVERIFY(menuAction(&menu, {QStringLiteral("Surround"), QStringLiteral("Downmix Options...")}) != nullptr);
		QVERIFY(menuAction(&menu, {QStringLiteral("Surround"), QStringLiteral("Surround Routing")}) != nullptr);
	}

	void theSurroundPannerPlacesTheChannelByMouseKeysAndSliders()
	{
		// Channel 1 sends into channel 3, a 5.1 channel: the panner names it and draws its speakers.
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_ch[3]}, {QStringLiteral("format"), QStringLiteral("5.1")}}).ok);
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[3]}, {QStringLiteral("amount"), 1.0}}).ok);
		showSurroundPanner(m_ch[1]);
		QWidget* window = windowTitled(QStringLiteral("Surround Panner"));
		QVERIFY(window != nullptr);
		window->show();
		QCOMPARE(window->property("controlCommand").toString(), QStringLiteral("mixer.surround_pan"));
		bool named = false;
		for (QLabel* label : window->findChildren<QLabel*>()) { named = named || label->text().startsWith(QStringLiteral("Pans into")); }
		QVERIFY2(named, "the panner did not name the surround channel it pans into");

		QWidget* pad = childNamed<QWidget>(window, QStringLiteral("Surround position"));
		QVERIFY(pad != nullptr);
		QVERIFY(!window->grab().isNull());  // the room, the speakers and the point

		// A click a quarter of the room right of the middle and a quarter up: the point lands there, panner on.
		const double side = std::min(pad->width(), pad->height()) - 56.0;
		const QPoint centre(pad->width() / 2, pad->height() / 2);
		QTest::mouseClick(pad, Qt::LeftButton, Qt::NoModifier, centre + QPoint(static_cast<int>(side / 4), -static_cast<int>(side / 4)));
		QJsonObject pan = channelState(QStringLiteral("mixer.surround_get"), m_ch[1]).value(QStringLiteral("pan")).toObject();
		QVERIFY(std::abs(pan.value(QStringLiteral("x")).toDouble() - 0.5) < 0.05);
		QVERIFY(std::abs(pan.value(QStringLiteral("y")).toDouble() - 0.5) < 0.05);
		QVERIFY(pan.value(QStringLiteral("enabled")).toBool());
		const double x0 = pan.value(QStringLiteral("x")).toDouble();

		// The arrow keys step it: 5 %, with Shift 25 %; any other key goes on to the dialog.
		pad->setFocus();
		QTest::keyClick(pad, Qt::Key_Left);
		pan = channelState(QStringLiteral("mixer.surround_get"), m_ch[1]).value(QStringLiteral("pan")).toObject();
		QVERIFY(std::abs(pan.value(QStringLiteral("x")).toDouble() - (x0 - 0.05)) < 0.01);
		for (int i = 0; i < 6; ++i) { QTest::keyClick(pad, Qt::Key_Right, Qt::ShiftModifier); }
		QTest::keyClick(pad, Qt::Key_Down);
		pan = channelState(QStringLiteral("mixer.surround_get"), m_ch[1]).value(QStringLiteral("pan")).toObject();
		QCOMPARE(pan.value(QStringLiteral("x")).toDouble(), 1.0);  // clamped at the room's wall
		QVERIFY(std::abs(pan.value(QStringLiteral("y")).toDouble() - 0.45) < 0.01);
		QVERIFY(pad->accessibleDescription().contains(QStringLiteral("1.00")));
		QTest::keyClick(pad, Qt::Key_Up);

		// A drag: press, move, release.
		QTest::mousePress(pad, Qt::LeftButton, Qt::NoModifier, centre);
		QTest::mouseMove(pad, centre + QPoint(0, static_cast<int>(side / 2)));
		QTest::mouseRelease(pad, Qt::LeftButton, Qt::NoModifier, centre + QPoint(0, -static_cast<int>(side / 2)));
		pan = channelState(QStringLiteral("mixer.surround_get"), m_ch[1]).value(QStringLiteral("pan")).toObject();
		QVERIFY(std::abs(pan.value(QStringLiteral("y")).toDouble() - 1.0) < 0.05);

		// The sliders and the switch each make one call when they are let go.
		auto* lfe = childNamed<QSlider>(window, QStringLiteral("LFE send"));
		auto* spread = childNamed<QSlider>(window, QStringLiteral("Spread"));
		auto* enabled = window->findChild<QCheckBox*>();
		QVERIFY(lfe != nullptr && spread != nullptr && enabled != nullptr);
		lfe->setValue(40);
		spread->setValue(70);
		pan = channelState(QStringLiteral("mixer.surround_get"), m_ch[1]).value(QStringLiteral("pan")).toObject();
		QVERIFY(std::abs(pan.value(QStringLiteral("lfe")).toDouble() - 0.4) < 0.01);
		QVERIFY(std::abs(pan.value(QStringLiteral("spread")).toDouble() - 0.7) < 0.01);
		QVERIFY(enabled->isChecked());
		enabled->click();
		QVERIFY(!channelState(QStringLiteral("mixer.surround_get"), m_ch[1]).value(QStringLiteral("pan")).toObject().value(QStringLiteral("enabled")).toBool());
		QVERIFY(!window->grab().isNull());
		window->close();
	}

	void theSurroundPannerOfAChannelThatSendsNowhereSaysSo()
	{
		showSurroundPanner(m_ch[2]);
		QWidget* window = windowTitled(QStringLiteral("Surround Panner"));
		QVERIFY(window != nullptr);
		bool says = false;
		for (QLabel* label : window->findChildren<QLabel*>()) { says = says || label->text().contains(QStringLiteral("sends into no surround channel")); }
		QVERIFY(says);
		QVERIFY(!window->grab().isNull());  // the 5.1 room is drawn as the default
		window->close();
	}

private:
	//! The CommandForm the last action opened (they are modeless and deleted on close), or nullptr.
	QWidget* formOpen() const
	{
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (qobject_cast<CommandForm*>(widget) != nullptr && widget->isVisible()) { return widget; }
		}
		return nullptr;
	}

	int formCount() const
	{
		int count = 0;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (qobject_cast<CommandForm*>(widget) != nullptr && widget->isVisible()) { ++count; }
		}
		return count;
	}

	void closeForms() const
	{
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (qobject_cast<CommandForm*>(widget) != nullptr) { widget->close(); }
		}
		QCoreApplication::processEvents();
	}

	bool sendExists(int from, int to) const
	{
		const QJsonArray channels = run(QStringLiteral("routing.matrix")).result.value(QStringLiteral("channels")).toArray();
		for (const QJsonValue& channel : channels)
		{
			if (channel.toObject().value(QStringLiteral("channel")).toString() != m_ch[from]) { continue; }
			for (const QJsonValue& send : channel.toObject().value(QStringLiteral("sends")).toArray())
			{
				if (send.toObject().value(QStringLiteral("to")).toString() == m_ch[to]) { return true; }
			}
		}
		return false;
	}

	QTemporaryDir m_home;
	QString m_ch[4];
};

QTEST_MAIN(MixerWindowsTest)
#include "MixerWindowsTest.moc"
