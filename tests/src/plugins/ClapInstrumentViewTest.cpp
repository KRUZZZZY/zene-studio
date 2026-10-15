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
 * A CLAP plug-in may change its clap.audio-ports layout while the host runs - it asks for a restart
 * and rescans its ports, the host's main-thread poll re-prepares, re-reads the layout and rebuilds the
 * transport (ClapInstrument::reprepare). That label was captured once at construction, so it kept the
 * OLD count after such a change.
 *
 * The subject is the in-tree fixture tests/data/clap-test-plugin/clap-test-instrument.c in its runtime
 * layout mode (CLAP_TEST_INSTRUMENT_LAYOUT): set before a re-prepare, the single-output class reports a
 * second stereo output port and announces it from activate() the way a real plug-in announces a new
 * routing layout. The test builds the real ClapInstrument and its real view, checks the initial count,
 * drives the two re-prepares the host's poll would run (one for the plug-in's announcement, one that
 * re-reads the changed layout), and asserts the label shows the new count.
 */

#include <QtTest>

#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QWidget>

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

	void outputChannelLabelFollowsARuntimeLayoutChange()
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

		// The plug-in changes its layout while running: it names the new layout at activate() and asks
		// for the restart/rescan. The host's poll answers it with re-prepare; the first re-prepare takes
		// the announcement, the second re-reads the layout, exactly as two poll ticks would.
		qputenv("CLAP_TEST_INSTRUMENT_LAYOUT", "aux");
		instrument->reprepare();
		instrument->reprepare();
		QCOMPARE(instrument->clapPlugin()->busLayout().outputs, 4);
		QVERIFY2(label->text().contains(QStringLiteral("4 channel")),
			qPrintable(QStringLiteral("the audio-output label went stale: ") + label->text()));

		qunsetenv("CLAP_TEST_INSTRUMENT_LAYOUT");
	}
};

QTEST_MAIN(ClapInstrumentViewTest)
#include "ClapInstrumentViewTest.moc"
