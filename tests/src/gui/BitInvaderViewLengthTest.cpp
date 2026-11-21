/*
 * BitInvaderViewLengthTest.cpp - the BitInvader view's Sample length knob resizes its wavetable graph
 *
 * Drives the real BitInvader view inside a running GuiApplication: its Sample length Knob is bound to
 * the instrument's FloatModel, and setting the knob's model is the signal path the knob writes. The
 * test checks that a shrink and regrow through the knob keeps the samples that remain and zeroes the
 * newly exposed tail.
 *
 * The knob's minimum is 4 (BitInvader.cpp: m_sampleLength is a FloatModel with min 4), so a 2 -> 4
 * change cannot be made through the view. The regrow is therefore exercised as 8 -> 4 -> 8.
 *
 * With BITINVADER_PROOF_PNG set to a path, the view is also grabbed after the regrow and saved there.
 *
 * Cluster new-p23-104.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <QtTest>

#include <QApplication>
#include <QPixmap>
#include <QTemporaryDir>

#include <memory>
#include <vector>

#include "BitInvader.h"
#include "Engine.h"
#include "Graph.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "Knob.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

class BitInvaderViewLengthTest : public QObject
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
	}

	void cleanupTestCase() { Engine::destroy(); }

	void sampleLengthKnobShrinksAndRegrowsTheGraphKeepingTheKeptSamplesAndZeroingTheTail()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		const std::unique_ptr<BitInvader> instrument{new BitInvader(track)};

		// createView binds the view to the instrument's models; InstrumentView reads parentWidget()->parentWidget()
		// on construction, so the view needs a real parent (instantiateView(nullptr) crashes there).
		QWidget host;
		const std::unique_ptr<gui::PluginView> pluginView{instrument->createView(&host)};
		auto* view = dynamic_cast<gui::BitInvaderView*>(pluginView.get());
		QVERIFY(view != nullptr);
		host.show();
		view->show();
		QVERIFY(QTest::qWaitForWindowExposed(&host));
		QApplication::processEvents();

		const auto knobs = view->findChildren<Knob*>();
		QCOMPARE(knobs.size(), 1);
		Knob* knob = knobs.front();
		QVERIFY(knob->model() != nullptr);
		QCOMPARE(knob->model()->displayName(), QStringLiteral("Sample length"));

		const auto graphs = view->findChildren<Graph*>();
		QCOMPARE(graphs.size(), 1);
		Graph* graph = graphs.front();
		QVERIFY(graph->model() != nullptr);

		// Set the wavetable to eight samples through the knob, then seed eight nonzero samples.
		knob->model()->setValue(8.0f);
		QCOMPARE(graph->model()->length(), 8);
		const std::vector<float> seeded{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
		graph->model()->setSamples(seeded.data(), static_cast<int>(seeded.size()));

		// Shrink to 4 through the knob, then regrow to 8 through the knob.
		knob->model()->setValue(4.0f);
		QCOMPARE(graph->model()->length(), 4);
		knob->model()->setValue(8.0f);
		QCOMPARE(graph->model()->length(), 8);

		const float* samples = graph->model()->samples();
		for (int i = 0; i < 4; ++i) { QCOMPARE(samples[i], seeded[static_cast<std::size_t>(i)]); }
		for (int i = 4; i < 8; ++i) { QCOMPARE(samples[i], 0.0f); }

		if (qEnvironmentVariableIsSet("BITINVADER_PROOF_PNG"))
		{
			const QString path = qEnvironmentVariable("BITINVADER_PROOF_PNG");
			QVERIFY2(view->grab().save(path, "PNG"), qPrintable(path));
		}
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(BitInvaderViewLengthTest)

#include "BitInvaderViewLengthTest.moc"
