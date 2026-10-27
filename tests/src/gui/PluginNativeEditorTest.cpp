/*
 * PluginNativeEditorTest.cpp - the editor window every hosted plug-in format opens in, and its host header
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

/*! A hosted plug-in's own editor opens in a native window whose id the plug-in's view attaches to; the format supplies
 *  three hooks (attach, detach, size). Held here with a hook set that records what it is told: a refused attach
 *  leaves no window; an open window is sized to the view plus the host header above it; opening again raises it
 *  instead of attaching twice; closing from the window manager or from the editor detaches exactly once. The host
 *  header's buttons (Bypass, A/B, presets) run plugin.* commands on the device the window shows. Also the "Show
 *  plugin editor" button of a generated control view, and the owner address of a chain. Q050-490: that button against
 *  the in-tree CLAP effect fixture - its editor opens in this platform's window, or the refusal names the window. */

#include <QtTest>

#include <QApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QTemporaryDir>
#include <QToolButton>

#include "CommandForm.h"
#include "CommandListDialog.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "PluginNativeEditor.h"
#include "PluginWindowSystem.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! What the fake format was told.
struct Calls
{
	int attached = 0;
	int detached = 0;
	void* parent = nullptr;
};

PluginNativeEditor::Hooks hooksFor(Calls* calls, int width, int height, bool accept = true)
{
	PluginNativeEditor::Hooks hooks;
	hooks.attach = [calls, accept](void* parent, QString* error) {
		calls->parent = parent;
		if (!accept) { *error = QStringLiteral("no view for this plug-in"); return false; }
		++calls->attached;
		return true;
	};
	hooks.detach = [calls] { ++calls->detached; };
	hooks.size = [width, height](int* w, int* h) { *w = width; *h = height; };
	return hooks;
}

QToolButton* buttonTexted(QWidget* window, const QString& text)
{
	for (QToolButton* button : window->findChildren<QToolButton*>())
	{
		if (button->text() == text) { return button; }
	}
	return nullptr;
}

QWidget* editorWindow(const QString& title)
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (widget->windowTitle() == title) { return widget; }
	}
	return nullptr;
}

template<typename T>
int openCount()
{
	int count = 0;
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (qobject_cast<T*>(widget) != nullptr && widget->isVisible()) { ++count; }
	}
	return count;
}

//! A track whose view the arranger has built (it builds it from a queued signal that must not outlive the track).
Track* newTrack()
{
	Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
	QCoreApplication::processEvents();
	return track;
}

//! While alive the engine is not addressable, so every command is refused (and the refusal is the test's subject).
struct EngineNotReady
{
	EngineNotReady() { ControlRegistry::setReady(false); }
	~EngineNotReady() { ControlRegistry::setReady(true); }
};

} // namespace

class PluginNativeEditorTest : public QObject
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
		while (Engine::mixer()->numChannels() < 2) { Engine::mixer()->createChannel(); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aRefusedAttachLeavesNoWindow()
	{
		Calls calls;
		PluginNativeEditor editor;
		QString error;
		QVERIFY(!editor.open(hooksFor(&calls, 100, 100, false), QStringLiteral("Refused"), &error));
		QCOMPARE(error, QStringLiteral("no view for this plug-in"));
		QVERIFY(!editor.isOpen());
		QVERIFY(calls.parent != nullptr);  // the plug-in was handed a native parent window to try
		QCOMPARE(calls.detached, 0);
		QVERIFY(editorWindow(QStringLiteral("Refused")) == nullptr);

		PluginNativeEditor noHooks;
		QVERIFY(!noHooks.open({}, QStringLiteral("No hooks"), &error));  // a format that cannot even attach
		QVERIFY(!noHooks.isOpen());
	}

	void anOpenWindowIsSizedToTheViewAndOpeningAgainRaisesIt()
	{
		Calls calls;
		PluginNativeEditor editor;
		QString error;
		QVERIFY2(editor.open(hooksFor(&calls, 320, 200), QStringLiteral("Plain editor"), &error), qPrintable(error));
		QVERIFY(editor.isOpen());
		QWidget* window = editorWindow(QStringLiteral("Plain editor"));
		QVERIFY(window != nullptr);
		QVERIFY(window->isVisible());
		QCOMPARE(window->size(), QSize(320, 200));  // no owner: no header above the view
		QVERIFY(window->findChild<QWidget*>(QStringLiteral("pluginHostHeader")) == nullptr);
		QCOMPARE(calls.attached, 1);

		QVERIFY(editor.open(hooksFor(&calls, 10, 10), QStringLiteral("Plain editor"), &error));
		QCOMPARE(calls.attached, 1);  // raised, not attached a second time
		QCOMPARE(window->size(), QSize(320, 200));

		// Closing from the editor detaches once and takes the window away.
		editor.close();
		QCOMPARE(calls.detached, 1);
		QVERIFY(!editor.isOpen());
		QCoreApplication::processEvents();
		QVERIFY(editorWindow(QStringLiteral("Plain editor")) == nullptr);
		editor.close();  // idempotent
		QCOMPARE(calls.detached, 1);
	}

	void closingTheWindowDetachesOnceAndAllowsAFreshOpen()
	{
		Calls calls;
		PluginNativeEditor editor;
		QString error;
		QVERIFY(editor.open(hooksFor(&calls, 200, 120), QStringLiteral("Closable"), &error));
		editorWindow(QStringLiteral("Closable"))->close();  // the window manager's close
		QCOMPARE(calls.detached, 1);
		QVERIFY(!editor.isOpen());  // at once, though the window is deleted a turn later
		QVERIFY(editor.open(hooksFor(&calls, 200, 120), QStringLiteral("Closable"), &error));
		QCOMPARE(calls.attached, 2);
		QVERIFY(editor.isOpen());
		// Destroying the editor closes its window too.
		auto* temporary = new PluginNativeEditor;
		Calls other;
		QVERIFY(temporary->open(hooksFor(&other, 50, 50), QStringLiteral("Temporary"), &error));
		delete temporary;
		QCOMPARE(other.detached, 1);
		QCoreApplication::processEvents();
		QVERIFY(editorWindow(QStringLiteral("Temporary")) == nullptr);
		editor.close();
	}

	void theHostHeaderRunsBypassAndAbOnTheEffect()
	{
		Effect* effect = amplifier();
		if (effect == nullptr) { QSKIP("this host loads no effect module"); }
		Calls calls;
		PluginNativeEditor editor;
		QString error;
		PluginEditorOwner owner;
		owner.effect = effect;
		QVERIFY(editor.open(hooksFor(&calls, 300, 150), QStringLiteral("Amp editor"), &error, owner));
		QWidget* window = editorWindow(QStringLiteral("Amp editor"));
		QWidget* header = window->findChild<QWidget*>(QStringLiteral("pluginHostHeader"));
		QVERIFY(header != nullptr);
		QCOMPARE(window->height(), 150 + header->sizeHint().height());  // the view's height plus the header
		QVERIFY(header->sizeHint().height() > 0);
		for (const char* text : {"Bypass", "Store A", "Store B", "A/B", "Save Preset...", "Load Preset...", "Presets..."})
		{
			QVERIFY2(buttonTexted(window, QLatin1String(text)) != nullptr, text);
		}

		// Bypass switches the effect off, and back.
		QToolButton* bypass = buttonTexted(window, QStringLiteral("Bypass"));
		QVERIFY(bypass->isCheckable() && !bypass->isChecked());
		QCOMPARE(bypass->property("controlCommand").toString(), QStringLiteral("plugin.bypass"));
		bypass->click();
		QVERIFY(!effect->isEnabled());
		bypass->click();
		QVERIFY(effect->isEnabled());
		// ... and follows a change made elsewhere (the rack's LED, an agent).
		effect->setEnabled(false);
		QTRY_VERIFY_WITH_TIMEOUT(bypass->isChecked(), 3000);
		effect->setEnabled(true);
		QTRY_VERIFY_WITH_TIMEOUT(!bypass->isChecked(), 3000);

		// Store A / Store B / A/B each run plugin.ab on this device.
		const QJsonObject self{{QStringLiteral("target"), control::channelIdOf(Engine::mixer()->mixerChannel(1))},
			{QStringLiteral("plugin"), control::effectIdOf(effect)}, {QStringLiteral("action"), QStringLiteral("get")}};
		QVERIFY(!run(QStringLiteral("plugin.ab"), self).result.value(QStringLiteral("has_a")).toBool());
		buttonTexted(window, QStringLiteral("Store A"))->click();
		QVERIFY(run(QStringLiteral("plugin.ab"), self).result.value(QStringLiteral("has_a")).toBool());
		buttonTexted(window, QStringLiteral("Store B"))->click();
		QVERIFY(run(QStringLiteral("plugin.ab"), self).result.value(QStringLiteral("has_b")).toBool());
		const QString heard = run(QStringLiteral("plugin.ab"), self).result.value(QStringLiteral("current")).toString();
		buttonTexted(window, QStringLiteral("A/B"))->click();
		QVERIFY(run(QStringLiteral("plugin.ab"), self).result.value(QStringLiteral("current")).toString() != heard);

		// The preset buttons ask their forms; the list opens the device's presets.
		const int forms = openCount<CommandForm>();
		buttonTexted(window, QStringLiteral("Save Preset..."))->click();
		QCOMPARE(openCount<CommandForm>(), forms + 1);
		buttonTexted(window, QStringLiteral("Load Preset..."))->click();
		QCOMPARE(openCount<CommandForm>(), forms + 2);
		const int lists = openCount<CommandListDialog>();
		buttonTexted(window, QStringLiteral("Presets..."))->click();
		QCOMPARE(openCount<CommandListDialog>(), lists + 1);
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (qobject_cast<CommandForm*>(widget) != nullptr || qobject_cast<CommandListDialog*>(widget) != nullptr) { widget->close(); }
		}
		editor.close();
	}

	void aRefusedHeaderCommandSaysWhy()
	{
		Effect* effect = amplifier();
		if (effect == nullptr) { QSKIP("this host loads no effect module"); }
		Calls calls;
		PluginNativeEditor editor;
		QString error;
		PluginEditorOwner owner;
		owner.effect = effect;
		QVERIFY(editor.open(hooksFor(&calls, 300, 150), QStringLiteral("Refusal editor"), &error, owner));
		QWidget* window = editorWindow(QStringLiteral("Refusal editor"));
		QWidget* header = window->findChild<QWidget*>(QStringLiteral("pluginHostHeader"));
		QVERIFY(header != nullptr);
		QLabel* reason = header->findChild<QLabel*>(QStringLiteral("pluginHostReason"));
		QVERIFY(reason != nullptr && !reason->isVisible());
		QToolButton* bypass = buttonTexted(window, QStringLiteral("Bypass"));
		QVERIFY(bypass != nullptr);
		{
			const EngineNotReady notReady;
			// A refused Store A shows its reason in the header, as the editor button's refusal does.
			buttonTexted(window, QStringLiteral("Store A"))->click();
			QVERIFY2(reason->isVisible(), "the store refusal was swallowed");
			QVERIFY2(!reason->text().isEmpty(), "the store refusal has no text");
			// A refused bypass puts the button back and says why.
			bypass->click();
			QVERIFY(!bypass->isChecked());
			QVERIFY(effect->isEnabled());
			QVERIFY2(reason->isVisible() && !reason->text().isEmpty(), "the bypass refusal was swallowed");
		}
		// The next command that runs clears the reason.
		buttonTexted(window, QStringLiteral("Store A"))->click();
		QVERIFY(!reason->isVisible());
		editor.close();
	}

	void anInstrumentsHeaderHasPresetsButNoBypass()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(newTrack());
		QVERIFY(track != nullptr);
		track->loadInstrument(QStringLiteral("tripleoscillator"));
		if (track->instrument() == nullptr || track->instrument()->descriptor() == nullptr) { QSKIP("this host loads no instrument module"); }
		Calls calls;
		PluginNativeEditor editor;
		QString error;
		PluginEditorOwner owner;
		owner.instrument = track->instrument();
		QVERIFY(editor.open(hooksFor(&calls, 300, 150), QStringLiteral("Synth editor"), &error, owner));
		QWidget* window = editorWindow(QStringLiteral("Synth editor"));
		QVERIFY(window->findChild<QWidget*>(QStringLiteral("pluginHostHeader")) != nullptr);
		QVERIFY(buttonTexted(window, QStringLiteral("Bypass")) == nullptr);
		QVERIFY(buttonTexted(window, QStringLiteral("Store A")) == nullptr);
		QVERIFY(buttonTexted(window, QStringLiteral("Save Preset...")) != nullptr);
		const int forms = openCount<CommandForm>();
		buttonTexted(window, QStringLiteral("Load Preset..."))->click();
		QCOMPARE(openCount<CommandForm>(), forms + 1);  // it names the track's own instrument
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (qobject_cast<CommandForm*>(widget) != nullptr) { widget->close(); }
		}
		editor.close();
	}

	void theShowEditorButtonSaysWhyWhenThereIsNone()
	{
		Effect* effect = amplifier();
		if (effect == nullptr) { QSKIP("this host loads no effect module"); }
		QWidget* box = makeNativeEditorButton(effect, nullptr);
		box->show();
		auto* button = box->findChild<QToolButton*>();
		QVERIFY(button != nullptr);
		QCOMPARE(button->text(), QStringLiteral("Show plugin editor"));
		QCOMPARE(button->property("controlCommand").toString(), QStringLiteral("plugin.editor_open"));
		QLabel* reason = box->findChild<QLabel*>();
		QVERIFY(reason != nullptr && !reason->isVisible());
		button->click();  // a built-in effect has no editor of its own: the refusal is shown
		QVERIFY(reason->isVisible());
		QVERIFY2(!reason->text().isEmpty(), "the refusal was swallowed");
		delete box;

		auto* track = dynamic_cast<InstrumentTrack*>(newTrack());
		track->loadInstrument(QStringLiteral("tripleoscillator"));
		if (track->instrument() == nullptr) { QSKIP("this host loads no instrument module"); }
		QWidget* instrumentBox = makeNativeEditorButton(track->instrument(), nullptr);
		instrumentBox->show();
		instrumentBox->findChild<QToolButton*>()->click();
		QVERIFY(instrumentBox->findChild<QLabel*>()->isVisible());
		QVERIFY(!instrumentBox->findChild<QLabel*>()->text().isEmpty());
		delete instrumentBox;
	}

	/*! Q050-490 through the GUI, against the real CLAP host and fixture: clicking "Show plugin editor" opens the
	 *  fixture's editor in this platform's window (x11 here, win32 / cocoa on those jobs); restricted to another
	 *  platform's window API, the same click is refused and the line under the button names this platform and its
	 *  CLAP window API, with no editor and no window left. */
	void theShowEditorButtonOpensOnThisPlatformOrNamesTheWindow()
	{
#ifndef CLAP_TEST_EFFECT_DIR
		QSKIP("this build has no CLAP effect fixture");
#else
		if (!QFileInfo::exists(QStringLiteral(CLAP_TEST_EFFECT_DIR))) { QSKIP("the CLAP effect fixture is not built"); }
		ConfigManager::inst()->setVSTDir(QStringLiteral(CLAP_TEST_EFFECT_DIR));
		QString device;
		const ControlResult listed = run(QStringLiteral("plugin.list"), {{QStringLiteral("format"), QStringLiteral("clap")}});
		for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
		{
			const QJsonObject entry = value.toObject();
			if (entry.value(QStringLiteral("name")).toString() == QLatin1String("LMMS CLAP Test Gain")
				|| entry.value(QStringLiteral("display_name")).toString() == QLatin1String("LMMS CLAP Test Gain"))
			{
				device = entry.value(QStringLiteral("id")).toString();
			}
		}
		if (device.isEmpty()) { QSKIP("plugin.list carries no CLAP test gain (no clapeffect module in this build)"); }
		auto* track = dynamic_cast<InstrumentTrack*>(newTrack());
		const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), control::trackIdOf(track)},
			{QStringLiteral("device"), device}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		Effect* effect = track->audioBusHandle()->effects()->effects().back();

		const bool headless = ControlRegistry::instance()->isHeadless();
		ControlRegistry::instance()->setHeadless(false);  // the offscreen platform is a display
		QWidget* box = makeNativeEditorButton(effect, nullptr);
		box->show();
		auto* button = box->findChild<QToolButton*>();
		QLabel* reason = box->findChild<QLabel*>();
		QVERIFY(button != nullptr && reason != nullptr);
		button->click();
		const bool opened = effect->nativeEditorOpen();
		const QString openRefusal = reason->text();
		QWidget* window = editorWindow(effect->displayName());
		const bool windowShown = window != nullptr && window->isVisible();
		effect->closeNativeEditor();

		const PluginWindowSystem native = currentPluginWindowSystem();
		const char* api = native == PluginWindowSystem::Win32 ? "win32" : native == PluginWindowSystem::Cocoa ? "cocoa" : "x11";
		qputenv("CLAP_TEST_GAIN_GUI_API", native == PluginWindowSystem::Win32 ? "x11" : "win32");
		button->click();
		qunsetenv("CLAP_TEST_GAIN_GUI_API");
		const bool refusedOpen = effect->nativeEditorOpen();
		const QString refusal = reason->text();
		const bool reasonShown = reason->isVisible();
		effect->closeNativeEditor();
		delete box;
		ControlRegistry::instance()->setHeadless(headless);
		QVERIFY2(opened, qPrintable(QStringLiteral("refused: %1").arg(openRefusal)));
		QVERIFY(windowShown);
		QVERIFY2(!refusedOpen, "an editor that cannot embed here reported open");
		QVERIFY(reasonShown);
		const QString expected = QStringLiteral("cannot embed on %1 (CLAP window API %2)").arg(pluginWindowSystemName(native),
			QLatin1String(api));
		QVERIFY2(refusal.contains(expected), qPrintable(refusal));
		QWidget* left = editorWindow(effect->displayName());
		QVERIFY2(left == nullptr || !left->isVisible(), "a refused editor left a window on screen");
#endif
	}

	void aChainIsAddressedByTheTrackOrChannelThatOwnsIt()
	{
		QCOMPARE(targetIdOfChain(&Engine::mixer()->mixerChannel(1)->m_fxChain), control::channelIdOf(Engine::mixer()->mixerChannel(1)));
		auto* track = dynamic_cast<InstrumentTrack*>(newTrack());
		QCOMPARE(targetIdOfChain(track->audioBusHandle()->effects()), control::trackIdOf(track));
		EffectChain orphan(nullptr);
		QVERIFY(targetIdOfChain(&orphan).isEmpty());
	}

private:
	Effect* amplifier()
	{
		EffectChain& chain = Engine::mixer()->mixerChannel(1)->m_fxChain;
		chain.clear();
		Effect* effect = Effect::instantiate(QStringLiteral("amplifier"), &chain, nullptr);
		if (effect == nullptr || !effect->isOkay())
		{
			delete effect;
			return nullptr;
		}
		chain.appendEffect(effect);
		return effect;
	}

	QTemporaryDir m_home;
};

QTEST_MAIN(PluginNativeEditorTest)
#include "PluginNativeEditorTest.moc"
