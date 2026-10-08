/*
 * EffectDeviceMenuTest.cpp - a device's menu (insert, replace, copy, A/B, parameters), the channel picker it
 *                            uses, and the effect picker's organiser
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

/*! What a device's menu does, held on real effects loaded from the build's own modules: every entry is a registered
 *  command, so each case triggers the entry the way a click does and reads the result off the chain or back through
 *  the registry. The pickers the entries open are modal, so each is answered from a timer once it is up. Also: the
 *  channel picker (OK needs a ticked channel, the sender is never offered), the device address of an effect on a
 *  channel, a track and a rack chain. (The effect picker's organiser is in EffectPickerOrganiserTest.) */

#include <QtTest>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

#include "ChannelPickDialog.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePicker.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectDeviceMenu.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "Rack.h"
#include "Song.h"
#include "TakeRecorder.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Runs @a handle on the modal widget the next action opens, once it is up (it is shown inside exec()).
void onceModal(const std::function<void(QWidget*)>& handle, int triesLeft = 100)
{
	QTimer::singleShot(10, [handle, triesLeft] {
		QWidget* modal = QApplication::activeModalWidget();
		if (modal == nullptr && triesLeft > 0) { onceModal(handle, triesLeft - 1); return; }
		handle(modal);
	});
}

//! Answers the device picker with the effect called @a name, or cancels it when there is none.
void pickDeviceNamed(const QString& name, bool* picked = nullptr)
{
	onceModal([name, picked](QWidget* modal) {
		auto* picker = qobject_cast<DevicePicker*>(modal);
		if (picker == nullptr) { if (auto* dialog = qobject_cast<QDialog*>(modal)) { dialog->reject(); } return; }
		picker->setQuery(name);
		if (picker->selectedDevice().isEmpty()) { picker->reject(); return; }
		if (picked != nullptr) { *picked = true; }
		static_cast<QDialog*>(picker)->accept();
	});
}

QStringList commandsOf(QMenu* menu)
{
	QStringList out;
	for (QAction* action : menu->actions())
	{
		if (!action->isSeparator() && action->menu() == nullptr) { out << action->property("controlCommand").toString(); }
	}
	return out;
}

int formCount()
{
	int count = 0;
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (qobject_cast<CommandForm*>(widget) != nullptr && widget->isVisible()) { ++count; }
	}
	return count;
}

void closeForms()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (qobject_cast<CommandForm*>(widget) != nullptr) { widget->close(); }
	}
	QCoreApplication::processEvents();
}

//! A track whose view the arranger has built (it builds it from a queued signal that must not outlive the track).
Track* newTrack()
{
	Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
	QCoreApplication::processEvents();
	return track;
}

} // namespace

class EffectDeviceMenuTest : public QObject
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
		while (Engine::mixer()->numChannels() < 5) { Engine::mixer()->createChannel(); }
		m_probe = Effect::instantiate(QStringLiteral("amplifier"), &chain(1), nullptr);
		m_haveModules = m_probe != nullptr && m_probe->isOkay();
		delete m_probe;
		m_probe = nullptr;
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		for (int i = 1; i < 5; ++i) { chain(i).clear(); }
		closeForms();
	}

	void aDeviceIsAddressedByItsOwnerAndItsId()
	{
		if (!m_haveModules) { QSKIP("this host loads no effect module"); }
		QVERIFY(deviceAddress(nullptr).isEmpty());

		Effect* onChannel = add(1, QStringLiteral("amplifier"));
		const QJsonObject channel = chainAddress(&chain(1));
		QCOMPARE(channel.value(QStringLiteral("target")).toString(), control::channelIdOf(Engine::mixer()->mixerChannel(1)));
		QVERIFY(!channel.contains(QStringLiteral("chain")));
		const QJsonObject device = deviceAddress(onChannel);
		QCOMPARE(device.value(QStringLiteral("plugin")).toString(), control::effectIdOf(onChannel));
		QCOMPARE(device.value(QStringLiteral("target")), channel.value(QStringLiteral("target")));

		// An instrument track's chain is addressed by the track.
		auto* track = dynamic_cast<InstrumentTrack*>(newTrack());
		QVERIFY(track != nullptr);
		EffectChain* trackChain = track->audioBusHandle()->effects();
		QCOMPARE(chainAddress(trackChain).value(QStringLiteral("target")).toString(), control::trackIdOf(track));

		// A rack's parallel chain by its channel and its number.
		Rack& rack = Engine::mixer()->mixerChannel(2)->m_rack;
		const int extra = rack.addChain();
		const QJsonObject parallel = chainAddress(rack.chain(extra));
		QCOMPARE(parallel.value(QStringLiteral("target")).toString(), control::channelIdOf(Engine::mixer()->mixerChannel(2)));
		QCOMPARE(parallel.value(QStringLiteral("chain")).toInt(), extra);
		rack.removeChain(extra);

		// A chain nothing owns has no address, so its devices get no menu.
		EffectChain orphan(nullptr);
		QVERIFY(chainAddress(&orphan).isEmpty());
		Effect* stray = Effect::instantiate(QStringLiteral("amplifier"), &orphan, nullptr);
		orphan.appendEffect(stray);
		QVERIFY(deviceAddress(stray).isEmpty());
		QWidget host;
		QMenu menu(&host);
		addEffectDeviceMenus(&menu, stray);
		QVERIFY(menu.actions().isEmpty());
	}

	void theDeviceMenuNamesEveryCommandItRuns()
	{
		if (!m_haveModules) { QSKIP("this host loads no effect module"); }
		Effect* effect = add(1, QStringLiteral("amplifier"));
		QWidget host;
		QMenu menu(&host);
		addEffectDeviceMenus(&menu, effect);
		QCOMPARE(commandsOf(&menu), (QStringList{QStringLiteral("plugin.load"), QStringLiteral("plugin.load"), QStringLiteral("plugin.replace"),
			QStringLiteral("plugin.copy"), QStringLiteral("chain.copy"), QStringLiteral("plugin.preset_list"), QStringLiteral("plugin.fold"),
			QStringLiteral("plugin.delta"), QStringLiteral("plugin.options")}));
		QVERIFY(menuAction(&menu, {QStringLiteral("A/B"), QStringLiteral("Blend A and B...")}) != nullptr);
		QVERIFY(menuAction(&menu, {QStringLiteral("Parameters"), QStringLiteral("Randomise by...")}) != nullptr);
		QVERIFY(menuAction(&menu, {QStringLiteral("Load Impulse Response...")}) == nullptr);  // not a convolution reverb
		QVERIFY(menuAction(&menu, {QStringLiteral("Record What Passes Here")}) == nullptr);   // and not a recorder
	}

	void insertBeforeAndAfterPlaceTheNewDeviceBesideThisOne()
	{
		if (!m_haveModules) { QSKIP("this host loads no effect module"); }
		Effect* first = add(1, QStringLiteral("amplifier"));
		QWidget host;
		QMenu menu(&host);
		addEffectDeviceMenus(&menu, first);

		bool picked = false;
		pickDeviceNamed(QStringLiteral("Amplifier"), &picked);
		menuAction(&menu, {QStringLiteral("Insert Effect After...")})->trigger();
		if (!picked) { QSKIP("this host catalogues no effect called Amplifier"); }
		QCOMPARE(chain(1).effects().size(), std::size_t{2});
		QCOMPARE(chain(1).effects().front(), first);

		picked = false;
		pickDeviceNamed(QStringLiteral("Amplifier"), &picked);
		menuAction(&menu, {QStringLiteral("Insert Effect Before...")})->trigger();
		QCOMPARE(chain(1).effects().size(), std::size_t{3});
		QCOMPARE(chain(1).effects().at(1), first);  // pushed back by the one inserted in front of it

		// One undo takes the last insertion back.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(chain(1).effects().size(), std::size_t{2});

		// A cancelled picker adds nothing.
		pickDeviceNamed(QStringLiteral("zz-no-device-is-called-this"));
		menuAction(&menu, {QStringLiteral("Insert Effect After...")})->trigger();
		QCOMPARE(chain(1).effects().size(), std::size_t{2});
	}

	void replaceWithPutsAnotherDeviceInItsPlace()
	{
		if (!m_haveModules) { QSKIP("this host loads no effect module"); }
		Effect* effect = add(1, QStringLiteral("amplifier"));
		const QPointer<Effect> old(effect);
		QWidget host;
		QMenu menu(&host);
		addEffectDeviceMenus(&menu, effect);
		bool picked = false;
		pickDeviceNamed(QStringLiteral("Amplifier"), &picked);
		menuAction(&menu, {QStringLiteral("Replace With...")})->trigger();
		if (!picked) { QSKIP("this host catalogues no effect called Amplifier"); }
		QCOMPARE(chain(1).effects().size(), std::size_t{1});
		QVERIFY2(old.isNull() || chain(1).effects().front() != old.data(), "the device was not replaced");
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(chain(1).effects().size(), std::size_t{1});
	}

	void copyToChannelsCopiesTheDeviceAndTheWholeChainInOneStep()
	{
		if (!m_haveModules) { QSKIP("this host loads no effect module"); }
		Effect* effect = add(1, QStringLiteral("amplifier"));
		add(1, QStringLiteral("amplifier"));
		QWidget host;
		QMenu menu(&host);
		addEffectDeviceMenus(&menu, effect);

		// The picker never offers the master or the sender, and OK waits for a tick.
		int offered = -1;
		bool okAtFirst = true;
		bool okAfterTick = false;
		onceModal([&](QWidget* modal) {
			auto* list = modal->findChild<QListWidget*>();
			QPushButton* ok = modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
			offered = list->count();
			okAtFirst = ok->isEnabled();
			list->item(0)->setCheckState(Qt::Checked);
			list->item(2)->setCheckState(Qt::Checked);
			okAfterTick = ok->isEnabled();
			ok->click();
		});
		menuAction(&menu, {QStringLiteral("Copy to Channels...")})->trigger();
		QCOMPARE(offered, int(Engine::mixer()->numChannels()) - 2);  // not the master, not channel 1
		QVERIFY(!okAtFirst);
		QVERIFY(okAfterTick);
		QCOMPARE(chain(2).effects().size(), std::size_t{1});
		QCOMPARE(chain(4).effects().size(), std::size_t{1});
		QCOMPARE(chain(3).effects().size(), std::size_t{0});  // not ticked
		QVERIFY(run(QStringLiteral("control.undo")).ok);       // ONE undo takes both copies back
		QCOMPARE(chain(2).effects().size(), std::size_t{0});
		QCOMPARE(chain(4).effects().size(), std::size_t{0});

		// The whole chain: both devices land on the ticked channel.
		onceModal([](QWidget* modal) {
			auto* list = modal->findChild<QListWidget*>();
			list->item(1)->setCheckState(Qt::Checked);
			modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
		});
		menuAction(&menu, {QStringLiteral("Copy Chain to Channels...")})->trigger();
		QCOMPARE(chain(3).effects().size(), std::size_t{2});

		// Cancelling the picker copies nothing.
		onceModal([](QWidget* modal) { modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click(); });
		menuAction(&menu, {QStringLiteral("Copy to Channels...")})->trigger();
		QCOMPARE(chain(2).effects().size(), std::size_t{0});
	}

	void theChannelPickerListsEveryChannelButTheMasterAndTheExcludedOne()
	{
		const QString excluded = control::channelIdOf(Engine::mixer()->mixerChannel(3));
		QStringList seen;
		onceModal([&seen](QWidget* modal) {
			QCOMPARE(modal->windowTitle(), QStringLiteral("Pick"));
			auto* list = modal->findChild<QListWidget*>();
			for (int i = 0; i < list->count(); ++i) { seen << list->item(i)->data(Qt::UserRole).toString(); }
			QVERIFY(list->item(0)->flags() & Qt::ItemIsUserCheckable);
			QVERIFY(list->item(0)->text().contains(list->item(0)->data(Qt::UserRole).toString()));
			list->item(1)->setCheckState(Qt::Checked);
			list->item(1)->setCheckState(Qt::Unchecked);  // ticked and unticked: nothing left to confirm
			QVERIFY(!modal->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
			list->item(0)->setCheckState(Qt::Checked);
			list->item(2)->setCheckState(Qt::Checked);
			static_cast<QDialog*>(modal)->accept();
		});
		const QStringList picked = pickChannels(nullptr, QStringLiteral("Pick"), excluded);
		QVERIFY(!seen.contains(excluded));
		QVERIFY(!seen.contains(control::channelIdOf(Engine::mixer()->mixerChannel(0))));
		QCOMPARE(seen.size(), int(Engine::mixer()->numChannels()) - 2);
		QCOMPARE(picked, (QStringList{seen.at(0), seen.at(2)}));

		onceModal([](QWidget* modal) { static_cast<QDialog*>(modal)->reject(); });
		QVERIFY(pickChannels(nullptr, QStringLiteral("Pick"), QString()).isEmpty());
	}

	void theAbMenuStoresAndTogglesThroughPluginAb()
	{
		if (!m_haveModules) { QSKIP("this host loads no effect module"); }
		Effect* effect = add(1, QStringLiteral("amplifier"));
		QWidget host;
		QMenu menu(&host);
		addEffectDeviceMenus(&menu, effect);
		const auto state = [effect] {
			return run(QStringLiteral("plugin.ab"), {{QStringLiteral("target"), deviceAddress(effect).value(QStringLiteral("target"))},
				{QStringLiteral("plugin"), control::effectIdOf(effect)}, {QStringLiteral("action"), QStringLiteral("get")}}).result;
		};
		QVERIFY(!state().value(QStringLiteral("has_a")).toBool());
		menuAction(&menu, {QStringLiteral("A/B"), QStringLiteral("Store in A")})->trigger();
		QVERIFY(state().value(QStringLiteral("has_a")).toBool());
		QVERIFY(!state().value(QStringLiteral("has_b")).toBool());
		menuAction(&menu, {QStringLiteral("A/B"), QStringLiteral("Store in B")})->trigger();
		QVERIFY(state().value(QStringLiteral("has_b")).toBool());
		const QString heard = state().value(QStringLiteral("current")).toString();
		menuAction(&menu, {QStringLiteral("A/B"), QStringLiteral("Toggle A/B")})->trigger();
		QVERIFY2(state().value(QStringLiteral("current")).toString() != heard, "Toggle did not switch the slot heard");

		// Blend asks for the amount in a form.
		const int before = formCount();
		menuAction(&menu, {QStringLiteral("A/B"), QStringLiteral("Blend A and B...")})->trigger();
		QCOMPARE(formCount(), before + 1);
		closeForms();
	}

	void theParameterEntriesFoldListenAndAskTheirForms()
	{
		if (!m_haveModules) { QSKIP("this host loads no effect module"); }
		Effect* effect = add(1, QStringLiteral("amplifier"));
		QWidget host;
		QMenu menu(&host);
		addParameterMenus(&menu, effect);
		QMenu* parameters = menuAction(&menu, {QStringLiteral("Parameters")})->menu();
		QVERIFY(parameters != nullptr);

		// Randomise runs at once and is one recorded step; the "by" form asks for the amount.
		const auto depth = [] { return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(); };
		const int before = depth();
		menuAction(&menu, {QStringLiteral("Parameters"), QStringLiteral("Randomise")})->trigger();
		QCOMPARE(depth(), before + 1);
		const int forms = formCount();
		for (const char* entry : {"Find Parameter...", "Rename Parameter...", "Randomise by..."})
		{
			menuAction(&menu, {QStringLiteral("Parameters"), QLatin1String(entry)})->trigger();
		}
		QCOMPARE(formCount(), forms + 3);
		closeForms();

		// Fold is a toggle on the device's own options; the entry's text follows the state.
		QVERIFY(!effect->deviceOptions().folded);
		menuAction(&menu, {QStringLiteral("Fold")})->trigger();
		QVERIFY(effect->deviceOptions().folded);
		QMenu folded(&host);
		addParameterMenus(&folded, effect);
		menuAction(&folded, {QStringLiteral("Unfold")})->trigger();
		QVERIFY(!effect->deviceOptions().folded);

		// "Hear only what it changes" is a checkable switch on the delta tap.
		QAction* delta = menuAction(&menu, {QStringLiteral("Hear Only What It Changes")});
		QVERIFY(delta->isCheckable() && !delta->isChecked());
		delta->trigger();
		QVERIFY(effect->deltaTap().enabled());
		QMenu listening(&host);
		addParameterMenus(&listening, effect);
		QVERIFY(menuAction(&listening, {QStringLiteral("Hear Only What It Changes")})->isChecked());
		menuAction(&listening, {QStringLiteral("Hear Only What It Changes")})->trigger();
		QVERIFY(!effect->deltaTap().enabled());

		const int more = formCount();
		menuAction(&menu, {QStringLiteral("Compatibility and Editor...")})->trigger();
		QCOMPARE(formCount(), more + 1);
		closeForms();
	}

	void aRecorderOffersToRecordAndLandItsTake()
	{
		Effect* recorder = add(1, QStringLiteral("insertrecorder"));
		if (recorder == nullptr || TakeRecorder::of(recorder) == nullptr) { QSKIP("this host loads no recorder module"); }
		QWidget host;
		QMenu menu(&host);
		addParameterMenus(&menu, recorder);
		QAction* land = menuAction(&menu, {QStringLiteral("Put the Last Take on a New Track")});
		QVERIFY(land != nullptr);
		QVERIFY2(!land->isEnabled(), "a take that does not exist can be landed");
		menuAction(&menu, {QStringLiteral("Record What Passes Here")})->trigger();
		QVERIFY(TakeRecorder::of(recorder)->recording());

		QMenu recording(&host);
		addParameterMenus(&recording, recorder);
		QVERIFY(!menuAction(&recording, {QStringLiteral("Put the Last Take on a New Track")})->isEnabled());
		menuAction(&recording, {QStringLiteral("Stop Recording")})->trigger();
		QVERIFY(!TakeRecorder::of(recorder)->recording());
		QVERIFY(!TakeRecorder::of(recorder)->lastTake().isEmpty());

		QMenu stopped(&host);
		addParameterMenus(&stopped, recorder);
		QAction* landAfter = menuAction(&stopped, {QStringLiteral("Put the Last Take on a New Track")});
		QCOMPARE(landAfter->isEnabled(), QFileInfo::exists(TakeRecorder::of(recorder)->lastTake()));
		QVERIFY(menuAction(&stopped, {QStringLiteral("Record What Passes Here")}) != nullptr);
	}

	void aConvolutionReverbOffersItsImpulseResponse()
	{
		Effect* reverb = add(1, QStringLiteral("convolutionreverb"));
		if (reverb == nullptr) { QSKIP("this host loads no convolution reverb module"); }
		QWidget host;
		QMenu menu(&host);
		addParameterMenus(&menu, reverb);
		QAction* load = menuAction(&menu, {QStringLiteral("Load Impulse Response...")});
		QVERIFY(load != nullptr);
		QCOMPARE(load->property("controlCommand").toString(), QStringLiteral("plugin.impulse_load"));
		const int before = formCount();
		load->trigger();
		QCOMPARE(formCount(), before + 1);
		closeForms();
	}

	void aFinishedTakeCanBeLanded()
	{
		Effect* recorder = add(1, QStringLiteral("insertrecorder"));
		if (recorder == nullptr || TakeRecorder::of(recorder) == nullptr) { QSKIP("this host loads no recorder module"); }
		// A take that exists on disk enables the entry; landing it makes a track.
		const QString take = m_home.filePath(QStringLiteral("take.wav"));
		QFile file(take);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.close();
		TakeRecorder::of(recorder)->setLastTake(take);
		QWidget host;
		QMenu menu(&host);
		addParameterMenus(&menu, recorder);
		QAction* land = menuAction(&menu, {QStringLiteral("Put the Last Take on a New Track")});
		QVERIFY(land->isEnabled());
		const std::size_t tracks = Engine::getSong()->tracks().size();
		land->trigger();
		QCOMPARE(Engine::getSong()->tracks().size(), tracks + 1);
		QCOMPARE(Engine::getSong()->tracks().back()->name(), QStringLiteral("take"));
		// Let the arranger build the new track's view first: a track removed in the same event-loop turn it was
		// added in leaves the queued createTrackView() a dangling Track* (src/gui/editors/TrackContainerView.cpp:124-126).
		QCoreApplication::processEvents();
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one undo step removes the track
		QCOMPARE(Engine::getSong()->tracks().size(), tracks);
	}

private:
	EffectChain& chain(int channel) { return Engine::mixer()->mixerChannel(channel)->m_fxChain; }

	Effect* add(int channel, const QString& name)
	{
		Effect* effect = Effect::instantiate(name, &chain(channel), nullptr);
		if (effect == nullptr || !effect->isOkay())
		{
			delete effect;
			return nullptr;
		}
		chain(channel).appendEffect(effect);
		return effect;
	}

	QTemporaryDir m_home;
	Effect* m_probe = nullptr;
	bool m_haveModules = false;
};

QTEST_MAIN(EffectDeviceMenuTest)
#include "EffectDeviceMenuTest.moc"
