/*
 * EffectEditorTeardownTest.cpp - a device's own editor is closed and detached before the device goes (Q050-367)
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*! Q050-367: removing a VST2 effect while its own editor is open must not crash. The suspect is the teardown
 *  order: a device's editor (VST2 through Carla or the legacy host, VST3, CLAP, an AU - every format through
 *  Plugin::openNativeEditor/closeNativeEditor) is the plug-in's, and the plug-in is destroyed with the effect, so an
 *  editor still open when its device goes is a plug-in UI torn down after its plug-in. The chain that every removal
 *  path ends in - the rack's "Remove this plugin" entry, plugin.unload, a chain cleared with its track - now closes
 *  the device's editor first.
 *
 *  Held here with a probe device whose editor is its own state: removal through the rack's real context-menu entry
 *  (a QAction triggered on the menu a right-click opens) and through plugin.unload, a chain cleared under it, and
 *  three removals in a row. The probe records, in order, the host's close and its own destruction, so a run that
 *  destroys a device with its editor still open fails on the record, not on a lucky crash. Nothing here needs a
 *  VST2 host: the record is the contract every host's teardown relies on. */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "EffectRackView.h"
#include "EffectView.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MenuTestSupport.h"
#include "Mixer.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! What the probe device was told, in order: "close" for the host's close, then how it was destroyed.
QStringList g_record;
//! The chain the probe was on when the host closed its editor; null when the close came after the detach.
const EffectChain* g_chainAtClose = nullptr;
//! Set by the probe's destructor: it went with its editor still open.
bool g_destroyedWithEditorOpen = false;

//! The probe device's controls: no parameters, no view - the editor is the device's own, as a plug-in's is.
class ProbeControls : public EffectControls
{
	Q_OBJECT
public:
	explicit ProbeControls(Effect* effect) : EffectControls(effect) {}
	int controlCount() override { return 0; }
	gui::EffectControlDialog* createView() override { return nullptr; }
	QString nodeName() const override { return QStringLiteral("q050367probecontrols"); }
	void saveSettings(QDomDocument&, QDomElement&) override {}
	void loadSettings(const QDomElement&) override {}
};

/*! A hosted device whose own editor is a flag it owns, opened and closed through the same
 *  openNativeEditor/closeNativeEditor/nativeEditorOpen a plug-in format implements. */
class ProbeEffect : public Effect
{
	Q_OBJECT
public:
	explicit ProbeEffect(Model* parent) : Effect(&descriptor(), parent, nullptr), m_controls(new ProbeControls(this)) {}

	~ProbeEffect() override
	{
		g_record.append(m_editorOpen ? QStringLiteral("destroyed-open") : QStringLiteral("destroyed-closed"));
		if (m_editorOpen) { g_destroyedWithEditorOpen = true; }
	}

	EffectControls* controls() override { return m_controls; }

	bool openNativeEditor(QString*) override
	{
		m_editorOpen = true;
		g_record.append(QStringLiteral("open"));
		return true;
	}

	void closeNativeEditor() override
	{
		if (!m_editorOpen) { return; }  // the formats' close is a no-op when nothing is open
		m_editorOpen = false;
		g_chainAtClose = effectChain();
		g_record.append(QStringLiteral("close"));
	}

	bool nativeEditorOpen() const override { return m_editorOpen; }

	static const Plugin::Descriptor& descriptor()
	{
		static const Plugin::Descriptor value{
			"q050367probe", "Q050-367 probe device", "A device with an editor of its own", "Zene Studio contributors",
			0x0100, Plugin::Type::Effect, nullptr, nullptr, nullptr};
		return value;
	}

protected:
	ProcessStatus processImpl(SampleFrame*, const f_cnt_t) override { return ProcessStatus::Continue; }

private:
	ProbeControls* m_controls;
	bool m_editorOpen = false;
};

//! The probe's record for one device, cleared before each case.
void resetRecord()
{
	g_record.clear();
	g_chainAtClose = nullptr;
	g_destroyedWithEditorOpen = false;
}

//! One turn of the event loop: queued calls, then the deferred deletions, then timers.
void runEvents()
{
	QCoreApplication::sendPostedEvents();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QCoreApplication::processEvents();
}

EffectView* effectViewOf(const EffectRackView* rack, const Effect* effect)
{
	for (EffectView* view : rack->findChildren<EffectView*>())
	{
		if (view->effect() == effect) { return view; }
	}
	return nullptr;
}

struct MenuPick
{
	QString entry;
	bool offered = false;
	bool done = false;
};

//! Waits for the popup a right-click opens and triggers the entry named @a pick->entry on it.
void pollForMenu(const std::shared_ptr<MenuPick>& pick, int triesLeft)
{
	QTimer::singleShot(10, [pick, triesLeft] {
		if (pick->done) { return; }
		auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
		if (menu == nullptr)
		{
			if (triesLeft > 0) { pollForMenu(pick, triesLeft - 1); }
			return;
		}
		if (QAction* action = lmms::test::menuAction(menu, {pick->entry}))
		{
			pick->offered = true;
			action->trigger();
		}
		menu->close();
	});
}

//! Opens @a view's context menu the way a right-click does and triggers @a entry on it. Returns whether it was there.
bool pickFromViewMenu(QWidget* view, const QString& entry)
{
	auto pick = std::make_shared<MenuPick>();
	pick->entry = entry;
	pollForMenu(pick, 300);
	QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(5, 5), view->mapToGlobal(QPoint(5, 5)));
	QApplication::sendEvent(view, &event);
	pick->done = true;
	return pick->offered;
}

} // namespace

class EffectEditorTeardownTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		getGUI()->mainWindow()->show();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	/*! The rack's own "Remove this plugin" entry, on a right-click of the device's view: the device's editor is closed
	 *  while the device is still on its chain, and the device then goes with its editor closed. */
	void theRacksRemoveEntryClosesTheEditorBeforeTheDeviceGoes()
	{
		resetRecord();
		auto* chain = new EffectChain(nullptr);
		auto* rack = new EffectRackView(chain, nullptr);
		rack->show();
		auto* effect = new ProbeEffect(chain);
		chain->appendEffect(effect);
		EffectView* view = effectViewOf(rack, effect);
		QVERIFY2(view != nullptr, "the rack drew no view for the device");
		QVERIFY(effect->openNativeEditor(nullptr));

		QVERIFY2(pickFromViewMenu(view, QStringLiteral("Remove this plugin")),
			"the view's context menu offered no Remove this plugin entry");
		runEvents();  // the rack's queued removal: the view, then EffectChain::removeEffect + deleteLater
		runEvents();  // the deferred deletion of the device

		QCOMPARE(static_cast<int>(chain->effects().size()), 0);
		QVERIFY2(!g_destroyedWithEditorOpen, qPrintable(QStringLiteral("the device was destroyed with its editor "
			"still open: %1").arg(g_record.join(QLatin1Char(',')))));
		QCOMPARE(g_record, (QStringList{QStringLiteral("open"), QStringLiteral("close"), QStringLiteral("destroyed-closed")}));
		QVERIFY2(g_chainAtClose == chain, "the host closed the editor after the device left its chain");
		QCOMPARE(rack->findChildren<EffectView*>().size(), 0);
		runEvents();
		delete rack;
		delete chain;
	}

	/*! plugin.unload, the agent's removal - the same chain, the same close, before the same destruction. */
	void theAgentsUnloadClosesTheEditorBeforeTheDeviceGoes()
	{
		resetRecord();
		MixerChannel* channel = Engine::mixer()->mixerChannel(Engine::mixer()->createChannel());
		EffectChain* chain = &channel->m_fxChain;
		auto* effect = new ProbeEffect(chain);
		chain->appendEffect(effect);
		const QString id = control::effectIdOf(effect);
		QString error;
		QVERIFY2(effect->openNativeEditor(&error), qPrintable(error));

		// An agent's socket is addressable once the program says startup finished; this process runs without one.
		ControlRegistry::setReady(true);
		const ControlResult unloaded = ControlRegistry::instance()->invoke(QStringLiteral("plugin.unload"),
			{{QStringLiteral("target"), control::channelIdOf(channel)}, {QStringLiteral("plugin"), id}});
		ControlRegistry::setReady(false);
		QVERIFY2(unloaded.ok, qPrintable(unloaded.errorMessage));
		runEvents();

		QCOMPARE(static_cast<int>(chain->effects().size()), 0);
		QVERIFY2(!g_destroyedWithEditorOpen, qPrintable(QStringLiteral("the device was destroyed with its editor "
			"still open: %1").arg(g_record.join(QLatin1Char(',')))));
		QCOMPARE(g_record, (QStringList{QStringLiteral("open"), QStringLiteral("close"), QStringLiteral("destroyed-closed")}));
		QVERIFY2(g_chainAtClose == chain, "the host closed the editor after the device left its chain");
	}

	/*! A chain cleared under its devices (a track or a channel going, a preset replacing what the chain carries) is the
	 *  same teardown by another door: every device's editor is closed before any of them is deleted. */
	void aClearedChainClosesTheEditorBeforeTheDeviceGoes()
	{
		resetRecord();
		auto* chain = new EffectChain(nullptr);
		auto* effect = new ProbeEffect(chain);
		chain->appendEffect(effect);
		QVERIFY(effect->openNativeEditor(nullptr));

		chain->clear();
		runEvents();

		QCOMPARE(static_cast<int>(chain->effects().size()), 0);
		QVERIFY2(!g_destroyedWithEditorOpen, qPrintable(QStringLiteral("the device was destroyed with its editor "
			"still open: %1").arg(g_record.join(QLatin1Char(',')))));
		QCOMPARE(g_record, (QStringList{QStringLiteral("open"), QStringLiteral("close"), QStringLiteral("destroyed-closed")}));
		delete chain;
	}

	/*! Three removals in a row, each with its editor open and the event loop run in between: the removal a person
	 *  repeats is the one that crashed. */
	void repeatedRemovalWithAnOpenEditorStaysStable()
	{
		for (int round = 0; round < 3; ++round)
		{
			resetRecord();
			auto* chain = new EffectChain(nullptr);
			auto* rack = new EffectRackView(chain, nullptr);
			rack->show();
			auto* effect = new ProbeEffect(chain);
			chain->appendEffect(effect);
			EffectView* view = effectViewOf(rack, effect);
			QVERIFY(view != nullptr);
			QVERIFY(effect->openNativeEditor(nullptr));
			QVERIFY2(pickFromViewMenu(view, QStringLiteral("Remove this plugin")), "no Remove this plugin entry");
			runEvents();
			runEvents();
			QCOMPARE(static_cast<int>(chain->effects().size()), 0);
			QVERIFY2(!g_destroyedWithEditorOpen, qPrintable(QStringLiteral("round %1: the device was destroyed with "
				"its editor still open: %2").arg(round).arg(g_record.join(QLatin1Char(',')))));
			delete rack;
			delete chain;
		}
	}

	/*! A device whose editor was never opened is not told anything: the close is the formats' own no-op, and the
	 *  record says the host did not invent a close. */
	void aDeviceWithoutAnOpenEditorIsRemovedAsBefore()
	{
		resetRecord();
		auto* chain = new EffectChain(nullptr);
		auto* effect = new ProbeEffect(chain);
		chain->appendEffect(effect);
		QVERIFY(!effect->nativeEditorOpen());

		chain->removeEffect(effect);
		effect->deleteLater();
		runEvents();

		QCOMPARE(static_cast<int>(chain->effects().size()), 0);
		QCOMPARE(g_record, (QStringList{QStringLiteral("destroyed-closed")}));
		delete chain;
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(EffectEditorTeardownTest)
#include "EffectEditorTeardownTest.moc"
