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
#include <QTimer>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "MixerHardwareMenu.h"
#include "MixerRoutingMatrix.h"
#include "MixerSurround.h"
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

//! mixer.get_state's entry for @a channel (the command reads every channel, so find it by id).
QJsonObject getState(const QString& channel)
{
	for (const QJsonValue& value : run(QStringLiteral("mixer.get_state")).result.value(QStringLiteral("channels")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == channel) { return value.toObject(); }
	}
	return {};
}

//! The pixel of @a pad's grab at the room coordinates (x right, y front, both -1..1), the room being the square
//! SurroundPanner draws: the pad's smaller side less 56, centred.
QColor padPixelAtRoom(QWidget* pad, double x, double y)
{
	const double side = std::min(pad->width(), pad->height()) - 56.0;
	const QPointF at(pad->width() / 2.0 + x * side / 2.0, pad->height() / 2.0 - y * side / 2.0);
	return pad->grab().toImage().pixelColor(static_cast<int>(at.x()), static_cast<int>(at.y()));
}

//! The Large Meters window's two master bars: the pixel a little above each bar's bottom edge, left then right.
QColor meterBarPixel(QWidget* window, int side)
{
	const int masterWidth = window->width() - 64;  // LargeMeters::InputWidth
	const int bottom = window->height() - 60;
	const int left = 20 + side * (masterWidth / 2 - 10);
	const int width = masterWidth / 2 - 40;
	return window->grab().toImage().pixelColor(left + width / 2, bottom - 3);
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
		Engine::audioEngine()->audioDev()->stopProcessing();  // the mixer must not overwrite the meter readings set here
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
		const tokens::Palette& pal = tokens::palette();
		MixerChannel* master = Engine::mixer()->mixerChannel(0);
		master->m_rmsLeft.set(0.f);
		master->m_rmsRight.set(0.f);
		QCOMPARE(meterBarPixel(window, 0), pal.control);  // silent: both bars are the empty track
		QCOMPARE(meterBarPixel(window, 1), pal.control);
		QVERIFY2(window->toolTip().contains(QStringLiteral("Loudness")), qPrintable(window->toolTip()));
		const QString disarmed = window->toolTip();

		master->m_rmsLeft.set(0.5f);   // -6 dB: the middle colour
		master->m_rmsRight.set(0.9f);  // -0.9 dB: the high colour
		QCOMPARE(meterBarPixel(window, 0), pal.meterMid);
		QCOMPARE(meterBarPixel(window, 1), pal.meterHigh);
		master->m_rmsLeft.set(0.02f);  // -34 dB: the low colour
		QCOMPARE(meterBarPixel(window, 0), pal.meterLow);
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
		const auto summaryOf = [window] {
			for (QLabel* label : window->findChildren<QLabel*>())
			{
				if (label->text().startsWith(QStringLiteral("B - A:")) || label->text().startsWith(QStringLiteral("Could not compare"))) { return label->text(); }
			}
			return QString();
		};
		const QString flat = summaryOf();
		QVERIFY2(flat.startsWith(QStringLiteral("B - A: low 0.0 dB")) && flat.contains(QStringLiteral("high 0.0 dB")), qPrintable(flat));
		// B (channel 1) with a high shelf the others lack: the window recompares when a combo moves, and says by how much.
		Engine::mixer()->mixerChannel(2)->m_eqHighModel.setValue(6.f);
		b->setCurrentIndex(2);
		const QString shelved = summaryOf();
		QVERIFY2(shelved.contains(QStringLiteral("high 6.0 dB")), qPrintable(shelved));
		a->setCurrentIndex(2);  // A and B the same channel: no difference
		QVERIFY2(summaryOf().contains(QStringLiteral("high 0.0 dB")), qPrintable(summaryOf()));
		a->setCurrentIndex(0);
		b->setCurrentIndex(2);
		QCOMPARE(summaryOf(), shelved);
		// The curves are drawn in the two channels' colours: A's flat response in the accent, B's shelf in the middle colour.
		const QImage curves = window->grab().toImage();
		const tokens::Palette& pal = tokens::palette();
		int accentPixels = 0;
		int middlePixels = 0;
		for (int y = 0; y < curves.height(); ++y)
		{
			for (int x = 0; x < curves.width(); ++x)
			{
				accentPixels += curves.pixelColor(x, y) == pal.accent ? 1 : 0;
				middlePixels += curves.pixelColor(x, y) == pal.meterMid ? 1 : 0;
			}
		}
		QVERIFY2(accentPixels > 150, qPrintable(QString::number(accentPixels)));
		QVERIFY2(middlePixels > 150, qPrintable(QString::number(middlePixels)));
		Engine::mixer()->mixerChannel(2)->m_eqHighModel.setValue(0.f);
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
		QVERIFY2(master.ok, qPrintable(master.errorMessage));
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

		// The same schema-backed menu offers the mono and discrete layouts, and the readback reports how
		// many channels each has and how they are addressed.
		QAction* mono = menuAction(&menu, {QStringLiteral("Surround"), QStringLiteral("Format"), QStringLiteral("mono")});
		QVERIFY(mono != nullptr);
		mono->trigger();
		QJsonObject read = getState(m_ch[2]);
		QCOMPARE(read.value(QStringLiteral("format")).toString(), QStringLiteral("mono"));
		QCOMPARE(read.value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(read.value(QStringLiteral("channel_roles")).toArray().size(), 1);
		QCOMPARE(read.value(QStringLiteral("channel_roles")).toArray().at(0).toString(), QStringLiteral("M"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);

		QAction* discrete = menuAction(&menu, {QStringLiteral("Surround"), QStringLiteral("Format"), QStringLiteral("discrete-5")});
		QVERIFY(discrete != nullptr);
		discrete->trigger();
		read = getState(m_ch[2]);
		QCOMPARE(read.value(QStringLiteral("format")).toString(), QStringLiteral("discrete-5"));
		QCOMPARE(read.value(QStringLiteral("channels")).toInt(), 5);
		QVERIFY(read.value(QStringLiteral("discrete")).toBool());
		QVERIFY(read.value(QStringLiteral("speakers")).toArray().isEmpty());
		QCOMPARE(read.value(QStringLiteral("channel_roles")).toArray().size(), 5);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(getState(m_ch[2]).value(QStringLiteral("format")).toString(), QString());

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
		// The point starts at the front (0, 1) as the accent dot, and the 5.1 speakers are drawn as panel-coloured boxes.
		const tokens::Palette& pal = tokens::palette();
		QCOMPARE(padPixelAtRoom(pad, 0.0, 1.0), pal.accent);
		float speakerX = 0.f;
		float speakerY = 0.f;
		surround::speakerPoint(surround::Format::Surround51, 0, &speakerX, &speakerY);  // front left
		QCOMPARE(padPixelAtRoom(pad, speakerX, speakerY), pal.panel);
		QVERIFY(padPixelAtRoom(pad, -0.9, -0.2) != pal.accent);

		// A click a quarter of the room right of the middle and a quarter up: the point lands there, panner on.
		const double side = std::min(pad->width(), pad->height()) - 56.0;
		const QPoint centre(pad->width() / 2, pad->height() / 2);
		QTest::mouseClick(pad, Qt::LeftButton, Qt::NoModifier, centre + QPoint(static_cast<int>(side / 4), -static_cast<int>(side / 4)));
		QJsonObject pan = channelState(QStringLiteral("mixer.surround_get"), m_ch[1]).value(QStringLiteral("pan")).toObject();
		QCOMPARE(padPixelAtRoom(pad, 0.5, 0.5), pal.accent);  // the dot follows the point
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
		// The pad still shows the last place the point was put (the drag ended at the front, x 0).
		QCOMPARE(padPixelAtRoom(pad, 0.0, 1.0), pal.accent);
		window->close();
	}

	void theSurroundPannerSliderKeepsNewerPanValues()
	{
		QVERIFY(run(QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("x"), 0.1},
			{QStringLiteral("y"), 0.8}, {QStringLiteral("lfe"), 0.1}, {QStringLiteral("spread"), 0.2}, {QStringLiteral("enabled"), false}}).ok);
		showSurroundPanner(m_ch[1]);
		QWidget* window = windowTitled(QStringLiteral("Surround Panner"));
		QVERIFY(window != nullptr);
		auto* lfe = childNamed<QSlider>(window, QStringLiteral("LFE send"));
		QVERIFY(lfe != nullptr);

		QVERIFY(run(QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("x"), -0.4},
			{QStringLiteral("y"), -0.3}, {QStringLiteral("spread"), 0.65}, {QStringLiteral("enabled"), true}}).ok);
		lfe->setValue(45);

		const QJsonObject pan = channelState(QStringLiteral("mixer.surround_get"), m_ch[1]).value(QStringLiteral("pan")).toObject();
		QVERIFY(std::abs(pan.value(QStringLiteral("x")).toDouble() + 0.4) < 0.01);
		QVERIFY(std::abs(pan.value(QStringLiteral("y")).toDouble() + 0.3) < 0.01);
		QVERIFY(std::abs(pan.value(QStringLiteral("spread")).toDouble() - 0.65) < 0.01);
		QVERIFY(pan.value(QStringLiteral("enabled")).toBool());
		QVERIFY(std::abs(pan.value(QStringLiteral("lfe")).toDouble() - 0.45) < 0.01);
		window->close();
	}

	void theSurroundPannerRefreshesItsDestinationFormat()
	{
		QVERIFY(run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[2]}}).ok);
		QVERIFY(run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[3]}}).ok);
		const auto restoreRoutes = [this] {
			run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[2]}});
			run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[3]}});
			run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_ch[2]}, {QStringLiteral("format"), QStringLiteral("stereo")}});
			run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_ch[3]}, {QStringLiteral("format"), QStringLiteral("5.1")} });
			run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[2]}, {QStringLiteral("amount"), 1.0}});
			run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[3]}, {QStringLiteral("amount"), 1.0}});
		};
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_ch[3]}, {QStringLiteral("format"), QStringLiteral("5.1")}}).ok);
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_ch[2]}, {QStringLiteral("format"), QStringLiteral("7.1")}}).ok);
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[3]},
			{QStringLiteral("amount"), 1.0}}).ok);
		showSurroundPanner(m_ch[1]);
		QWidget* window = windowTitled(QStringLiteral("Surround Panner"));
		QVERIFY(window != nullptr);
		QLabel* destination = nullptr;
		for (QLabel* label : window->findChildren<QLabel*>())
		{
			if (label->text().startsWith(QStringLiteral("Pans into"))) { destination = label; }
		}
		QVERIFY(destination != nullptr);
		QCOMPARE(destination->text(), QStringLiteral("Pans into %1 (5.1).").arg(m_ch[3]));

		QVERIFY(run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[3]}}).ok);
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), m_ch[1]}, {QStringLiteral("to"), m_ch[2]},
			{QStringLiteral("amount"), 1.0}}).ok);
		auto* refresh = window->findChild<QTimer*>(QStringLiteral("surroundPannerRefresh"));
		if (refresh == nullptr)
		{
			window->close();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			restoreRoutes();
			QFAIL("the panner has no route refresh hook");
		}
		QVERIFY(QMetaObject::invokeMethod(refresh, "timeout", Qt::DirectConnection));
		QCOMPARE(destination->text(), QStringLiteral("Pans into %1 (7.1).").arg(m_ch[2]));

		QWidget* pad = childNamed<QWidget>(window, QStringLiteral("Surround position"));
		QVERIFY(pad != nullptr);
		float speakerX = 0.f;
		float speakerY = 0.f;
		surround::speakerPoint(surround::Format::Surround71, 6, &speakerX, &speakerY);
		QCOMPARE(padPixelAtRoom(pad, speakerX, speakerY), tokens::palette().panel);
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_ch[2]}, {QStringLiteral("format"), QStringLiteral("5.1")}}).ok);
		QVERIFY(QMetaObject::invokeMethod(refresh, "timeout", Qt::DirectConnection));
		QCOMPARE(destination->text(), QStringLiteral("Pans into %1 (5.1).").arg(m_ch[2]));
		QVERIFY(padPixelAtRoom(pad, speakerX, speakerY) != tokens::palette().panel);
		window->close();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		restoreRoutes();
	}

	void theSurroundPannerOfAChannelThatSendsNowhereSaysSo()
	{
		showSurroundPanner(m_ch[2]);
		QWidget* window = windowTitled(QStringLiteral("Surround Panner"));
		QVERIFY(window != nullptr);
		bool says = false;
		for (QLabel* label : window->findChildren<QLabel*>()) { says = says || label->text().contains(QStringLiteral("sends into no surround channel")); }
		QVERIFY(says);
		// ... and draws the 5.1 room as the default: a speaker box where 5.1 puts its front left.
		QWidget* pad = childNamed<QWidget>(window, QStringLiteral("Surround position"));
		QVERIFY(pad != nullptr);
		float speakerX = 0.f;
		float speakerY = 0.f;
		surround::speakerPoint(surround::Format::Surround51, 0, &speakerX, &speakerY);
		QCOMPARE(padPixelAtRoom(pad, speakerX, speakerY), tokens::palette().panel);
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
