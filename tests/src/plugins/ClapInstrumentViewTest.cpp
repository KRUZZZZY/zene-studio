/*
 * ClapInstrumentViewTest.cpp - the CLAP instrument view follows a runtime audio-port layout change
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
 */

/*
 * WHAT THIS PROVES
 * ----------------
 * The instrument view shows the plug-in's audio-output channel count ("Audio output: N channel(s)").
 * When ClapInstrument::reprepare() consumes a pending CLAP audio-port list rescan and reads a changed
 * layout, the view must refresh that count. This test directly drives the two reprepare phases; it does
 * not verify that the host poll services a runtime restart request.
 *
 * The subject is the in-tree fixture tests/data/clap-test-plugin/clap-test-instrument.c in its runtime
 * layout mode (CLAP_TEST_INSTRUMENT_LAYOUT): set before a re-prepare, the single-output class reports a
 * second stereo output port and announces it from activate() using the CLAP list-rescan and restart
 * callbacks. The test builds the real ClapInstrument and its real view, checks the initial count, calls
 * reprepare() to make that announcement, then calls it again to consume the inactive rescan and assert
 * that the label shows the new count. prepare() clears the restart flag raised during activation, so
 * these direct calls are not presented as coverage of poll() servicing that request.
 */

#include <QtTest>

#include <QApplication>
#include <QDomDocument>
#include <QDomElement>
#include <QFileInfo>
#include <QLabel>
#include <QWidget>

#include <cmath>
#include <memory>

#include "ClapInstrument.h"
#include "ClapInstrumentView.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "Track.h"
#include "plugin_export.h"

#ifndef CLAP_TEST_INSTRUMENT_PATH
#define CLAP_TEST_INSTRUMENT_PATH ""
#endif

using namespace lmms;

namespace
{

//! The view's audio-output channel-count label, or nullptr.
QLabel* outputLabel(QWidget* view)
{
	for (QLabel* label : view->findChildren<QLabel*>())
	{
		if (label->text().startsWith(QStringLiteral("Audio output:"))) { return label; }
	}
	return nullptr;
}

} // namespace

// The module's own descriptor, the same symbol the plug-in factory exposes.
extern "C" Plugin::Descriptor PLUGIN_EXPORT clapinstrument_plugin_descriptor;

class ClapInstrumentViewTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase()
	{
		qunsetenv("CLAP_TEST_INSTRUMENT_LAYOUT");
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void outputChannelLabelFollowsLayoutReadOnReprepare()
	{
		const QString module = QStringLiteral(CLAP_TEST_INSTRUMENT_PATH);
		if (module.isEmpty() || !QFileInfo::exists(module)) { QSKIP("no CLAP instrument fixture available"); }

		qunsetenv("CLAP_TEST_INSTRUMENT_LAYOUT");  // the class starts at one stereo output port
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);

		Plugin::Descriptor::SubPluginFeatures::Key key;
		key.desc = &clapinstrument_plugin_descriptor;
		key.name = QStringLiteral("LMMS CLAP Test Instrument");
		key.attributes[QStringLiteral("file")] = module;
		key.attributes[QStringLiteral("id")] = QStringLiteral("org.lmms.test.clap-instrument");

		const std::unique_ptr<ClapInstrument> instrument{new ClapInstrument(track, &key)};
		QVERIFY2(instrument->clapPlugin()->isLoaded(), "the CLAP test instrument did not load");
		QCOMPARE(instrument->clapPlugin()->busLayout().outputs, 2);

		QWidget parent;
		const std::unique_ptr<gui::ClapInstrumentView> view{new gui::ClapInstrumentView(instrument.get(), &parent)};
		QLabel* label = outputLabel(view.get());
		QVERIFY2(label != nullptr, "the instrument view shows no audio-output channel count");
		QVERIFY2(label->text().contains(QStringLiteral("2 channel")), qPrintable(label->text()));

		// The fixture announces its added port during activation. prepare() clears the restart request
		// from that callback, so this test drives the subsequent reprepare directly and checks both
		// phases: first the announcement, then the inactive rescan and changed layout read.
		qputenv("CLAP_TEST_INSTRUMENT_LAYOUT", "aux");
		instrument->reprepare();
		QCOMPARE(instrument->clapPlugin()->busLayout().outputs, 2);
		QVERIFY(!instrument->clapPlugin()->needsReprepare());
		QVERIFY2(label->text().contains(QStringLiteral("2 channel")), qPrintable(label->text()));

		instrument->reprepare();
		QCOMPARE(instrument->clapPlugin()->busLayout().outputs, 4);
		QVERIFY2(label->text().contains(QStringLiteral("4 channel")),
			qPrintable(QStringLiteral("the audio-output label went stale: ") + label->text()));

		qunsetenv("CLAP_TEST_INSTRUMENT_LAYOUT");
	}

	/*! stress-4 (b): a SUCCESSFUL clap.state load that does not bring a writable parameter back must not lose it
	 *  (the stress run lost Surge XT's this way). Gain is set and the instrument saved with no block in between, so
	 *  the plug-in's own state still holds 1.0 while the host's saved <parameters> hold 0.4; loading it back must
	 *  leave Gain at 0.4 in the host's model and in the value it sends the plug-in. */
	void aValueTheStateDoesNotCarryIsRestoredFromTheHost()
	{
		const QString module = QStringLiteral(CLAP_TEST_INSTRUMENT_PATH);
		if (module.isEmpty() || !QFileInfo::exists(module)) { QSKIP("no CLAP instrument fixture available"); }
		qunsetenv("CLAP_TEST_INSTRUMENT_LAYOUT");
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Plugin::Descriptor::SubPluginFeatures::Key key;
		key.desc = &clapinstrument_plugin_descriptor;
		key.name = QStringLiteral("LMMS CLAP Test Instrument");
		key.attributes[QStringLiteral("file")] = module;
		key.attributes[QStringLiteral("id")] = QStringLiteral("org.lmms.test.clap-instrument");
		const std::unique_ptr<ClapInstrument> instrument{new ClapInstrument(track, &key)};
		QVERIFY2(instrument->clapPlugin()->isLoaded(), "the CLAP test instrument did not load");

		ClapParamModel* gain = nullptr;
		for (ClapParamModel* model : instrument->paramModels()) { if (model->paramId() == 1) { gain = model; } }
		QVERIFY2(gain != nullptr, "the fixture has no Gain parameter");
		gain->setValue(0.4f);

		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("clapinstrument"));
		instrument->saveSettings(doc, element);
		QVERIFY2(!element.firstChildElement(QStringLiteral("state")).isNull(),
			"the instrument wrote no plug-in state: this case is about a SUCCESSFUL state load");

		gain->setValue(0.8f);
		instrument->loadSettings(element);
		QVERIFY2(std::abs(gain->value() - 0.4f) < 1e-6f, qPrintable(QStringLiteral("restored %1").arg(gain->value())));
		QVERIFY2(std::abs(instrument->clapPlugin()->paramPlain(1) - 0.4) < 1e-6,
			qPrintable(QStringLiteral("sent %1").arg(instrument->clapPlugin()->paramPlain(1))));
	}
};

QTEST_MAIN(ClapInstrumentViewTest)
#include "ClapInstrumentViewTest.moc"
