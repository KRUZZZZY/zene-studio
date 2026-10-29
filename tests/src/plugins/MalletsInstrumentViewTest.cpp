/*
 * MalletsInstrumentViewTest.cpp - the BandedWG "Bowed" setting has a control
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QRegularExpression>
#include <QtTest>
#include <algorithm>
#include <memory>
#include <vector>

#include "ConfigManager.h"
#include "ComboBox.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "LedCheckBox.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

#include "Mallets.h"

#ifndef MALLETS_TEST_STK_DIR
#define MALLETS_TEST_STK_DIR ""
#endif

#ifndef MALLETS_TEST_PLUGIN_DIR
#define MALLETS_TEST_PLUGIN_DIR ""
#endif

using namespace lmms;

namespace
{

//! The instrument view's "Bowed" checkbox, or nullptr when the view has none.
gui::LedCheckBox* bowedCheckBox(QWidget* view)
{
	for (gui::LedCheckBox* box : view->findChildren<gui::LedCheckBox*>())
	{
		if (box->text() == QStringLiteral("Bowed")) { return box; }
	}
	return nullptr;
}

//! The "strike" attribute the instrument writes into a project: "1" bowed, "0" struck.
QString savedStrike(MalletsInstrument* instrument)
{
	QDomDocument doc;
	QDomElement element = doc.createElement(QStringLiteral("instrument"));
	instrument->saveSettings(doc, element);
	return element.attribute(QStringLiteral("strike"));
}

//! The "preset" attribute the instrument writes into a project: the index the synth plays.
QString savedPreset(MalletsInstrument* instrument)
{
	QDomDocument doc;
	QDomElement element = doc.createElement(QStringLiteral("instrument"));
	instrument->saveSettings(doc, element);
	return element.attribute(QStringLiteral("preset"));
}

//! The product voice limit of one Mallets instrument
//! (plugins/Stk/Mallets/Mallets.cpp, MALLETS_VOICE_LIMIT). The limit is the
//! instrument's own voice budget, so it applies per instrument.
constexpr int malletsVoiceLimit = 128;

//! Renders one period of a Mallets note into a cleared buffer and reports
//! whether the note produced any audio. A note that owns a prepared voice is
//! audible; a note that waits for the instrument's voice budget is not.
bool renderMalletsNote(MalletsInstrument* instrument, NotePlayHandle* handle,
	std::vector<SampleFrame>& data)
{
	std::fill(data.begin(), data.end(), SampleFrame{0.0f, 0.0f});
	instrument->playNote(handle, std::span<SampleFrame>{data});
	for (const auto& frame : data)
	{
		if (frame.left() != 0.0f || frame.right() != 0.0f) { return true; }
	}
	return false;
}

} // namespace

class MalletsInstrumentViewTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		// The instrument view opens a modal "missing files" dialog when the STK rawwaves are not found,
		// which would block an offscreen test. Point the instrument at the directory the build found.
		ConfigManager::inst()->setSTKDir(QString::fromUtf8(MALLETS_TEST_STK_DIR));
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void theBowedCheckBoxIsBoundToTheStrikeSetting()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		const std::unique_ptr<MalletsInstrument> instrument{new MalletsInstrument(track)};

		QWidget parent;
		const std::unique_ptr<gui::PluginView> pluginView{instrument->createView(&parent)};
		auto* view = dynamic_cast<gui::MalletsInstrumentView*>(pluginView.get());
		QVERIFY(view != nullptr);
		auto* presets = view->findChild<gui::ComboBox*>();
		QVERIFY(presets != nullptr);
		// The first BandedWG preset is index 10; select it through the instrument's preset control.
		for (int i = 0; i < 10; ++i) { presets->selectNext(); }
		QCOMPARE(presets->model()->value(), 10);
		parent.show();
		view->show();

		gui::LedCheckBox* bowed = bowedCheckBox(view);
		QVERIFY2(bowed != nullptr, "the BandedWG view has no Bowed checkbox, so the setting cannot be changed in the UI");
		QVERIFY2(bowed->isVisible(), "the Bowed checkbox is hidden for the selected BandedWG preset");

		// The setting defaults to bowed, and the checkbox shows it.
		QVERIFY(bowed->model()->value());
		QCOMPARE(savedStrike(instrument.get()), QStringLiteral("1"));

		// A click on the checkbox writes through to the instrument's setting.
		bowed->click();
		QVERIFY(!bowed->model()->value());
		QCOMPARE(savedStrike(instrument.get()), QStringLiteral("0"));

		bowed->click();
		QVERIFY(bowed->model()->value());
		QCOMPARE(savedStrike(instrument.get()), QStringLiteral("1"));

		// The other direction: a struck setting loaded from a project shows on the checkbox.
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("instrument"));
		element.setAttribute(QStringLiteral("strike"), QStringLiteral("0"));
		instrument->loadSettings(element);
		QVERIFY2(!bowed->model()->value(), "a loaded Bowed=0 setting does not reach the checkbox");
	}

	void aLoadedPresetOutsideTheScalerTableIsFittedIntoIt()
	{
		// The synth indexes its scaler table with the preset the model holds; the table has
		// the 14 presets 0-13. A project value outside that range must be fitted on load.
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		const std::unique_ptr<MalletsInstrument> instrument{new MalletsInstrument(track)};

		const struct { const char* loaded; const char* saved; } cases[] = {
			{"-1", "0"}, {"0", "0"}, {"13", "13"}, {"14", "13"}, {"99", "13"},
		};
		for (const auto& c : cases)
		{
			QDomDocument doc;
			QDomElement element = doc.createElement(QStringLiteral("instrument"));
			element.setAttribute(QStringLiteral("preset"), QString::fromUtf8(c.loaded));
			instrument->loadSettings(element);
			QCOMPARE(savedPreset(instrument.get()), QString::fromUtf8(c.saved));
		}
	}

	//! The instrument's voice budget must start empty: a fresh Mallets
	//! instrument has no note playing, so its shared counter is 0 and the
	//! first malletsVoiceLimit notes are voiced. A budget that started
	//! indeterminate (garbage) either silences every note or lets notes past
	//! the limit. The three phases pin the whole contract: the limit is
	//! reached exactly, the next note waits, and releasing one note lets the
	//! waiting note through - so the counter must start at zero and count
	//! exactly once per note.
	void aFreshInstrumentsVoiceBudgetStartsEmpty()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		const std::unique_ptr<MalletsInstrument> instrument{new MalletsInstrument(track)};

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(frames > 0);
		std::vector<SampleFrame> data(static_cast<std::size_t>(frames));

		const Note note{TimePos{static_cast<tick_t>(frames)}, TimePos{0}, DefaultKey};
		std::vector<NotePlayHandle*> notes;
		notes.reserve(static_cast<std::size_t>(malletsVoiceLimit) + 1);
		for (int i = 0; i < malletsVoiceLimit + 1; ++i)
		{
			notes.push_back(NotePlayHandleManager::acquire(track, 0, frames, note,
				nullptr, -1, NotePlayHandle::Origin::MidiClip));
		}

		// Phase 1: the budget is empty, so the first malletsVoiceLimit notes
		// are all voiced. A garbage budget that reads at or above the limit
		// silences the very first note here.
		std::vector<char> audible(notes.size(), 0);
		for (std::size_t i = 0; i < notes.size(); ++i)
		{
			audible[i] = renderMalletsNote(instrument.get(), notes[i], data) ? 1 : 0;
		}
		for (int i = 0; i < malletsVoiceLimit; ++i)
		{
			QVERIFY2(audible[static_cast<std::size_t>(i)] != 0,
				qPrintable(QString{"note %1 of a fresh Mallets instrument was silenced although "
					"its voice budget starts empty"}.arg(i)));
		}

		// Phase 2: the budget is full, so the next note waits for a voice. A
		// garbage budget that reads below zero would voice it, and an
		// over-large budget would too.
		QVERIFY2(audible[static_cast<std::size_t>(malletsVoiceLimit)] == 0,
			"note 129 was voiced although a fresh instrument holds only 128 voices");

		// Phase 3: releasing one note returns exactly one voice to the budget,
		// so the waiting note is voiced on its next period and the released
		// note's slot is free for it.
		instrument->deleteNotePluginData(notes[0]);
		NotePlayHandleManager::release(notes[0]);
		notes[0] = nullptr;
		QVERIFY2(renderMalletsNote(instrument.get(), notes[static_cast<std::size_t>(malletsVoiceLimit)], data),
			"the waiting note was not voiced after a running note released its voice");

		for (auto* handle : notes)
		{
			if (handle != nullptr) { NotePlayHandleManager::release(handle); }
		}
	}

	//! The voice budget belongs to the instrument, so its counter must be
	//! initialised where it is declared. The behavioural test above passes
	//! even without that initialiser, because C++20 (P0883,
	//! __cpp_lib_atomic_value_initialization) value-initialises a
	//! default-constructed std::atomic in this toolchain. The contract the
	//! finding asks for is therefore not observable at runtime here: change
	//! the declaration to a different toolchain, or a build below C++20, and
	//! the counter would start indeterminate. This pins the declaration so
	//! the initialiser cannot be dropped silently.
	void theVoiceBudgetIsInitialisedWhereItIsDeclared()
	{
		QVERIFY2(QString::fromUtf8(MALLETS_TEST_PLUGIN_DIR).size() > 0,
			"MALLETS_TEST_PLUGIN_DIR is not defined for this target");
		QFile header(QString::fromUtf8(MALLETS_TEST_PLUGIN_DIR) + QStringLiteral("/Mallets.h"));
		QVERIFY2(header.open(QIODevice::ReadOnly), qPrintable(header.errorString()));
		const QString text = QString::fromUtf8(header.readAll());

		// The one shared budget, with no other atomic of the same spelling.
		const QRegularExpression declaration(
			QStringLiteral(R"(\bstd::atomic<\s*int\s*>\s+m_voicesInUse\b[^;]*;)"));
		auto matches = declaration.globalMatch(text);
		QVERIFY2(matches.hasNext(), "Mallets.h no longer declares the shared voice budget m_voicesInUse");
		const QString declared = matches.next().captured(0);
		QVERIFY2(!matches.hasNext(), "Mallets.h declares m_voicesInUse more than once");

		// `{0}` (or `= 0` / `(0)`) is the initialiser the finding requires.
		QVERIFY2(declared.contains(QRegularExpression(QStringLiteral(R"(\{\s*0\s*\}|\b0\s*\)|=\s*0\b)"))),
			qPrintable(QString{"the voice budget is declared without an initialiser (%1); a build "
				"where std::atomic is not value-initialised would start it indeterminate"}
				.arg(declared.trimmed())));
	}
};

QTEST_MAIN(MalletsInstrumentViewTest)

#include "MalletsInstrumentViewTest.moc"
