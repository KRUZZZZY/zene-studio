/*
 * MixerModesTest.cpp - 0.5.0 theme 16: the mixer's modes, channel types and routing, in words
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

/*! Theme 16 (approved as recommended, adjusted by the research): explicit Mix and Routing views and,
 *  independent of them, Compact and Expanded densities, set by window.mixer_view and by a mode bar that
 *  names every mode (what the faders drive, solo mode, listen, the master's output and format); each strip
 *  wears a type badge (with its surround format), names its routes by channel name - never an id - and
 *  shows the modes it is in; the routing matrix keeps only the sends that exist. Needs the real
 *  application (a Fader cannot be built without it). */

#include <QtTest>

#include <QComboBox>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

#include "AutomatableButton.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MacroKnobs.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerModeBar.h"
#include "MixerRoutingMatrix.h"
#include "MixerStripInfo.h"
#include "MixerView.h"
#include "MixerViewOptions.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

//! The view's half-second sync, now: a count change rebuilds the strips, the next pass applies the modes.
void syncMixer()
{
	getGUI()->mixerView()->syncWithMixer();
	getGUI()->mixerView()->syncWithMixer();
}

MixerChannelView* strip(int index) { return getGUI()->mixerView()->channelView(index); }
MixerModeBar* bar() { return getGUI()->mixerView()->modeBar(); }

} // namespace

class MixerModesTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->mainWindow()->revealPanel(QStringLiteral("mixer"));
		m_sender = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(m_sender)->m_name = QStringLiteral("Lead");
		const ControlResult created = run(QStringLiteral("bus.create"));
		m_return = created.result.value(QStringLiteral("index")).toInt(-1);
		QVERIFY(m_return > 0);
		Engine::mixer()->mixerChannel(m_return)->m_name = QStringLiteral("Verb");
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), ch(m_sender)}, {QStringLiteral("to"), ch(m_return)},
			{QStringLiteral("amount"), 0.5}}).ok);
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("view"), QStringLiteral("mix")},
			{QStringLiteral("density"), QStringLiteral("expanded")}}).ok);
		syncMixer();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void everyStripWearsItsTypeInWords()
	{
		QCOMPARE(strip(0)->typeBadge()->text(), QStringLiteral("Master"));
		QCOMPARE(strip(m_return)->typeBadge()->text(), QStringLiteral("Return"));
		QVERIFY(strip(m_sender)->typeBadge()->isVisibleTo(getGUI()->mixerView()));
		// The strip says it all in sentences for a screen reader.
		QVERIFY(strip(m_return)->accessibleDescription().startsWith(QStringLiteral("Return")));
	}

	void theSurroundFormatJoinsTheBadge()
	{
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(m_return)},
			{QStringLiteral("format"), QStringLiteral("5.1")}}).ok);
		syncMixer();
		QCOMPARE(strip(m_return)->typeBadge()->toolTip(), QStringLiteral("Return 5.1"));
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), ch(m_return)},
			{QStringLiteral("format"), QStringLiteral("stereo")}}).ok);
		syncMixer();
		QCOMPARE(strip(m_return)->typeBadge()->toolTip(), QStringLiteral("Return"));
	}

	void routesAreNamedByChannelNeverById()
	{
		const QString route = strip(m_sender)->routingLine()->toolTip();
		QVERIFY2(route.contains(QStringLiteral("Verb")), qPrintable(route));
		QVERIFY2(!route.contains(QStringLiteral("ch-")), qPrintable(route));
		// The answer an agent reads names the same things.
		const QJsonArray strips = run(QStringLiteral("window.mixer_view")).result.value(QStringLiteral("strips")).toArray();
		QCOMPARE(strips.size(), Engine::mixer()->numChannels());
		const QJsonObject sender = strips.at(m_sender).toObject();
		QCOMPARE(sender.value(QStringLiteral("type")).toString(), QStringLiteral("channel"));
		bool found = false;
		for (const QJsonValue& output : sender.value(QStringLiteral("outputs")).toArray())
		{
			found = found || output.toObject().value(QStringLiteral("to")).toString() == QStringLiteral("Verb");
		}
		QVERIFY(found);
		QCOMPARE(strips.at(m_return).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("return"));
	}

	void theRoutingViewShowsWhereSignalComesFrom()
	{
		const ControlResult set = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("view"), QStringLiteral("routing")}});
		QVERIFY(set.ok);
		QCOMPARE(set.result.value(QStringLiteral("view")).toString(), QStringLiteral("routing"));
		syncMixer();
		const QString route = strip(m_return)->routingLine()->toolTip();
		QVERIFY2(route.contains(QStringLiteral("Lead")), qPrintable(route));  // Lead sends into Verb
		QVERIFY(bar()->viewButton(QStringLiteral("routing"))->isChecked());
		QVERIFY(!strip(m_sender)->insertList()->isVisibleTo(getGUI()->mixerView()));  // processing is the mix view's
	}

	void theCompactDensityKeepsTheEssentials()
	{
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("view"), QStringLiteral("mix")},
			{QStringLiteral("density"), QStringLiteral("compact")}}).ok);
		syncMixer();
		QVERIFY(bar()->densityButton(QStringLiteral("compact"))->isChecked());
		QVERIFY(!strip(m_sender)->macroKnobs()->isVisibleTo(getGUI()->mixerView()));
		QVERIFY(!strip(m_sender)->routingLine()->text().contains(QLatin1Char('\n')));  // one line
		QVERIFY(strip(m_sender)->fader()->isVisibleTo(getGUI()->mixerView()));
		// Density never narrows a strip: the name keeps strip_width's width.
		QCOMPARE(strip(m_sender)->nameEdit()->width(), 56);
	}

	void theModeBarSetsTheViewThroughTheCommand()
	{
		bar()->densityButton(QStringLiteral("expanded"))->click();
		QCOMPARE(mixerviewoptions::options().density, QStringLiteral("expanded"));
		bar()->viewButton(QStringLiteral("routing"))->click();
		QCOMPARE(mixerviewoptions::options().view, QStringLiteral("routing"));
		bar()->viewButton(QStringLiteral("mix"))->click();
		QCOMPARE(mixerviewoptions::options().view, QStringLiteral("mix"));
		QComboBox* filter = bar()->filterBox();
		const int returns = filter->findData(QStringLiteral("returns"));
		QVERIFY(returns >= 0);
		emit filter->activated(returns);
		QCOMPARE(mixerviewoptions::options().filter, QStringLiteral("returns"));
		emit filter->activated(filter->findData(QStringLiteral("all")));
		QCOMPARE(mixerviewoptions::options().filter, QStringLiteral("all"));
	}

	void listeningShowsOnTheStripAndTheBar()
	{
		QVERIFY(run(QStringLiteral("mixer.listen"), {{QStringLiteral("channel"), ch(m_sender)}}).ok);
		syncMixer();
		QVERIFY(strip(m_sender)->stateLine()->toolTip().contains(QStringLiteral("AFL")));
		QVERIFY(bar()->listenButton()->text().contains(QStringLiteral("Lead")));
		QVERIFY(bar()->listenButton()->property("active").toBool());
		QVERIFY(run(QStringLiteral("mixer.listen_mode"), {{QStringLiteral("clear"), true}}).ok);
		syncMixer();
		QVERIFY(!strip(m_sender)->stateLine()->isVisibleTo(getGUI()->mixerView()));
		QCOMPARE(bar()->listenButton()->text(), QStringLiteral("Listen: Off"));
	}

	void theSoloModeIsNamedOnTheBarAndEveryStrip()
	{
		QVERIFY(run(QStringLiteral("mixer.solo_mode"), {{QStringLiteral("mode"), QStringLiteral("additive")}}).ok);
		syncMixer();
		QCOMPARE(bar()->soloButton()->text(), QStringLiteral("Solo: Additive"));
		QVERIFY(strip(m_sender)->soloButton()->toolTip().contains(QStringLiteral("additive")));
		QVERIFY(run(QStringLiteral("mixer.solo_mode"), {{QStringLiteral("mode"), QStringLiteral("exclusive")}}).ok);
		syncMixer();
		QCOMPARE(bar()->soloButton()->text(), QStringLiteral("Solo: Exclusive"));
	}

	void theBarNamesWhatTheFadersDriveAndTheMastersOutput()
	{
		QCOMPARE(bar()->fadersButton()->text(), QStringLiteral("Faders: Volume"));
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), ch(m_return)}}).ok);
		syncMixer();
		QCOMPARE(bar()->fadersButton()->text(), QStringLiteral("Faders: Sends to Verb"));
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), QString()}}).ok);
		syncMixer();
		QVERIFY(bar()->masterButton()->text().startsWith(QStringLiteral("Master: ")));
		QVERIFY(!bar()->masterButton()->text().contains(QStringLiteral("ch-")));
	}

	void renamingFromTheStripIsOneUndoStep()
	{
		// Through mixer.rename_channel (0.5.0 wave 3), so Ctrl+Z and an agent's control.undo take it back.
		MixerChannelView* view = strip(m_sender);
		view->renameChannel();
		view->nameEdit()->setText(QStringLiteral("Lead 2"));
		emit view->nameEdit()->editingFinished();
		QCOMPARE(Engine::mixer()->mixerChannel(m_sender)->m_name, QStringLiteral("Lead 2"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::mixer()->mixerChannel(m_sender)->m_name, QStringLiteral("Lead"));
	}

	void theRoutingMatrixKeepsOnlyTheSendsThatExist()
	{
		// A thousand channels and three sends: three cells kept, not a million (the research's 8 MB).
		QJsonArray channels;
		QStringList types;
		for (int i = 0; i < 1000; ++i)
		{
			QJsonArray sends;
			if (i > 0 && i <= 3) { sends.append(QJsonObject{{QStringLiteral("to"), QStringLiteral("ch-0")}, {QStringLiteral("amount"), 0.25}}); }
			channels.append(QJsonObject{{QStringLiteral("channel"), QStringLiteral("ch-%1").arg(i)},
				{QStringLiteral("name"), i == 0 ? QStringLiteral("Master") : QStringLiteral("Ch %1").arg(i)}, {QStringLiteral("sends"), sends}});
			types << (i == 0 ? QStringLiteral("Master") : QStringLiteral("Channel"));
		}
		RoutingMatrixModel model;
		model.setChannels(channels, types);
		QCOMPARE(model.rowCount(), 1000);
		QCOMPARE(model.columnCount(), 1000);
		QCOMPARE(model.storedCells(), 3);
		QCOMPARE(model.data(model.index(1, 0)).toString(), QStringLiteral("0.25"));
		QVERIFY(!model.data(model.index(5, 0)).isValid());
		QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Master (Master)"));
		QCOMPARE(model.data(model.index(5, 0), Qt::AccessibleTextRole).toString(), QStringLiteral("Ch 5 to Master: no send (Enter makes one)"));
		QVERIFY(model.flags(model.index(7, 7)) == Qt::NoItemFlags);  // no channel sends to itself
		QCOMPARE(model.channelId(4), QStringLiteral("ch-4"));
	}

// Deleting a channel renumbers the ones behind it. The selection follows the channel itself, not
	// the number it used to sit at: removing one in front of it left it pointing at whatever channel
	// then took that number.
	void deletingAChannelInFrontKeepsTheSelectedChannelSelected()
	{
		MixerView* mixer = getGUI()->mixerView();
		const int front = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(front)->m_name = QStringLiteral("Wedge");
		const int middle = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(middle)->m_name = QStringLiteral("Room");
		const int last = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(last)->m_name = QStringLiteral("Plate");
		mixer->syncWithMixer();
		// A channel with channels behind it, so the stale number names a real neighbour.
		mixer->setCurrentMixerChannel(middle);
		QCOMPARE(mixer->currentMixerChannel()->channelIndex(), middle);

		mixer->deleteChannel(front);
		QCOMPARE(Engine::mixer()->numChannels(), last);  // one fewer
		// The same channel, renumbered: its strip, and the selection with it.
		QCOMPARE(mixer->currentMixerChannel()->channelIndex(), middle - 1);
		QCOMPARE(Engine::mixer()->mixerChannel(middle - 1)->m_name, QStringLiteral("Room"));
	}

// A channel added while the mixer was open rebuilds the strips. The selection then jumped to Master
	// and the user's channel was lost, whatever the rebuild was for.
	void addingAChannelKeepsTheSelectedChannelSelected()
	{
		MixerView* mixer = getGUI()->mixerView();
		const int selected = Engine::mixer()->createChannel();
		Engine::mixer()->mixerChannel(selected)->m_name = QStringLiteral("Selected");
		mixer->syncWithMixer();
		mixer->setCurrentMixerChannel(selected);
		QCOMPARE(Engine::mixer()->mixerChannel(mixer->currentMixerChannel()->channelIndex())->m_name,
			QStringLiteral("Selected"));

		// A channel appears from outside the mixer: a script, or the TrackContainer above.
		QVERIFY(Engine::mixer()->createChannel() > 0);
		syncMixer();

		QCOMPARE(Engine::mixer()->mixerChannel(mixer->currentMixerChannel()->channelIndex())->m_name,
			QStringLiteral("Selected"));
	}

private:
	QTemporaryDir m_home;
	int m_sender = -1;
	int m_return = -1;
};

QTEST_MAIN(MixerModesTest)
#include "MixerModesTest.moc"
