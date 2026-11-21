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

#include <QDir>
#include <QDomDocument>
#include <QFile>
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

QString readSource(const QString& relativePath)
{
	QFile file{QDir{QStringLiteral(QT_TESTCASE_SOURCEDIR)}.filePath(relativePath)};
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { return {}; }
	return QString::fromUtf8(file.readAll());
}

QString functionBodyStartingAt(const QString& source, const QString& signature)
{
	const auto start = source.indexOf(signature);
	if (start < 0) { return {}; }
	const auto end = source.indexOf(QStringLiteral("\n}\n"), start);
	if (end < 0) { return {}; }
	return source.mid(start, end - start);
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

	//! Cluster new-p17-203: note creation must pin the published wavetable.
	void aNotePinsThePublishedWavetableSnapshot()
	{
		const auto source = readSource(QStringLiteral("../plugins/Watsyn/Watsyn.cpp"));
		QVERIFY2(!source.isEmpty(), "the Watsyn sources were not found");

		// Building a note runs on the audio thread: it must not copy any live table.
		const auto voiceConstruction = functionBodyStartingAt(source,
			QStringLiteral("WatsynVoice::WatsynVoice("));
		QVERIFY2(!voiceConstruction.isEmpty(), "the Watsyn voice was not found");
		for (const auto* wave : {"A1_wave", "A2_wave", "B1_wave", "B2_wave"})
		{
			QVERIFY2(!voiceConstruction.contains(QLatin1String(wave)),
				qPrintable(QStringLiteral("the Watsyn voice still copies the live wavetable %1 on the audio thread")
					.arg(QLatin1String(wave))));
		}
		QVERIFY2(!voiceConstruction.contains(QStringLiteral("memcpy")),
			"the Watsyn voice still copies a wavetable on the audio thread");

		// Rendering reads the immutable snapshot pinned to the note.
		const auto renderPath = functionBodyStartingAt(source,
			QStringLiteral("void WatsynVoice::renderOutput("));
		QVERIFY2(!renderPath.isEmpty(), "the render path of Watsyn was not found");
		QVERIFY2(renderPath.contains(QStringLiteral("m_wave->data")),
			"the Watsyn render path does not read the wavetable snapshot the note pinned");

		// The pre-migration source is a negative control: it copied the live table.
		const auto referenceSource = readSource(QStringLiteral("reference/Watsyn/Watsyn.cpp"));
		QVERIFY2(!referenceSource.isEmpty(), "the pre-migration Watsyn source was not found");
		const auto referenceConstruction = functionBodyStartingAt(referenceSource,
			QStringLiteral("WatsynObject::WatsynObject("));
		QVERIFY2(!referenceConstruction.isEmpty(), "the pre-migration Watsyn voice was not found");
		QVERIFY2(referenceConstruction.contains(QStringLiteral("memcpy")),
			"the pre-migration Watsyn voice does not copy the live wavetable into the note, "
			"so the assertion above proves nothing");
	}
};

QTEST_GUILESS_MAIN(WatsynSelectedGraphTest)

#include "WatsynSelectedGraphTest.moc"
