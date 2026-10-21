/*
 * WatsynSelectedGraphTest.cpp - cluster new-p17-205 regression.
 *
 * Watsyn's editor keeps the selected oscillator graph (A1, A2, B1 or B2) in
 * `m_selectedGraph`. Its saveSettings() and loadSettings() had that line
 * commented out, so a project saved with B2 selected was reopened with the
 * default A1 selected.
 *
 * The test drives the real plugin through the Instrument save/load entry
 * points: a saved instrument must carry the selection, and a saved selection
 * must come back on a freshly loaded instrument. A project written before the
 * attribute existed must still load to the default selection.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <QDomDocument>
#include <QtTest>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

//! Loads a fresh Watsyn instrument on a new instrument track of the current song.
Instrument* newWatsyn()
{
	auto* track = dynamic_cast<InstrumentTrack*>(
		Track::create(Track::Type::Instrument, Engine::getSong()));
	return track ? track->loadInstrument(QStringLiteral("watsyn")) : nullptr;
}

//! Saves an instrument the way a project file stores it: its own element under a parent.
QDomElement savedElement(QDomDocument& doc, Instrument& instrument)
{
	QDomElement parent = doc.createElement(QStringLiteral("instruments"));
	return instrument.saveState(doc, parent);
}

} // namespace

class WatsynSelectedGraphTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		// The watsyn module is loaded through the plug-in factory, like the other
		// instrument tests (AudioFileProcessorPlayheadTest's pattern).
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! A project saved with B2 selected reopens with B2 selected. The selection is
	//! written as an attribute of the instrument element, and read back on load.
	void savedSelectionIsRestoredOnReopen()
	{
		Instrument* saver = newWatsyn();
		QVERIFY(saver != nullptr);
		QDomDocument doc;
		QDomElement saved = savedElement(doc, *saver);
		QVERIFY2(saved.hasAttribute(QStringLiteral("selgraph")),
			"Watsyn::saveSettings() does not write the selected graph (cluster new-p17-205)");

		// The editor's selection is B2 (A1=0, A2=1, B1=2, B2=3); stamp it onto the
		// saved element the way a project carrying that selection would hold it.
		saved.setAttribute(QStringLiteral("selgraph"), 3);

		Instrument* reopened = newWatsyn();
		QVERIFY(reopened != nullptr);
		reopened->restoreState(saved);

		QDomDocument resavedDoc;
		QDomElement resaved = savedElement(resavedDoc, *reopened);
		QCOMPARE(resaved.attribute(QStringLiteral("selgraph")), QStringLiteral("3"));
	}

	//! A project written before the selection was saved has no selgraph attribute.
	//! It must load and keep the default A1 selection, as it did before.
	void projectWithoutSelectionKeepsDefault()
	{
		Instrument* saver = newWatsyn();
		QVERIFY(saver != nullptr);
		QDomDocument doc;
		QDomElement oldProject = savedElement(doc, *saver);
		oldProject.removeAttribute(QStringLiteral("selgraph"));

		Instrument* reopened = newWatsyn();
		QVERIFY(reopened != nullptr);
		reopened->restoreState(oldProject);

		QDomDocument resavedDoc;
		QDomElement resaved = savedElement(resavedDoc, *reopened);
		QCOMPARE(resaved.attribute(QStringLiteral("selgraph")), QStringLiteral("0"));
	}
};

QTEST_GUILESS_MAIN(WatsynSelectedGraphTest)

#include "WatsynSelectedGraphTest.moc"
