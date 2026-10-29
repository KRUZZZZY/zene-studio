/*
 * GraphWaveformEditorGuiTest.cpp - Q050-486: the waveform a user sees after loading saved graph data
 *
 * Drives the real Wave Shaper editor (its EffectControlDialog and the Graph widget the user draws
 * the curve in) inside a running GuiApplication: a saved curve shows in the widget exactly; a
 * damaged saved curve leaves the widget showing the curve it had and lands in the load error
 * report; a stroke drawn on the widget with the mouse is saved and comes back exactly on a fresh
 * editor through the same loader.
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <QtTest>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include <QDomDocument>
#include <QTemporaryDir>

#include "Effect.h"
#include "EffectChain.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "Engine.h"
#include "Graph.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Mixer.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QString encode(const std::vector<float>& values)
{
	const QByteArray bytes{reinterpret_cast<const char*>(values.data()), static_cast<int>(values.size() * sizeof(float))};
	return QString::fromLatin1(bytes.toBase64());
}

std::vector<float> shown(Graph* graph)
{
	const graphModel* model = graph->model();
	return {model->samples(), model->samples() + model->length()};
}

//! A soft-knee curve in the Wave Shaper's 0..1 range, distinct from its default straight line.
std::vector<float> drawnCurve(int count)
{
	std::vector<float> values(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i) { values[static_cast<std::size_t>(i)] = std::sqrt((i + 1.0f) / count); }
	return values;
}

} // namespace

class GraphWaveformEditorGuiTest : public QObject
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

	void theEditorShowsTheLoadedCurveKeepsItOnDamageAndSavesWhatTheUserDraws()
	{
		EffectChain* chain = &Engine::mixer()->mixerChannel(0)->m_fxChain;
		std::unique_ptr<Effect> shaper{Effect::instantiate(QStringLiteral("waveshaper"), chain, nullptr)};
		if (shaper == nullptr || !shaper->isOkay()) { QSKIP("this host loads no waveshaper module"); }
		std::unique_ptr<EffectControlDialog> dialog{shaper->controls()->createView()};
		QVERIFY(dialog != nullptr);
		dialog->show();
		QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
		auto graphs = dialog->findChildren<Graph*>();
		QCOMPARE(graphs.size(), 1);
		Graph* graph = graphs.front();
		const int length = graph->model()->length();

		// A saved, drawn curve is what the editor shows, value for value.
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("effect"));
		QDomElement saved = shaper->controls()->saveState(doc, parent);
		const std::vector<float> curve = drawnCurve(length);
		saved.setAttribute(QStringLiteral("waveShape"), encode(curve));
		shaper->controls()->restoreState(saved);
		QCOMPARE(shown(graph), curve);

		// A damaged curve (cut one value short) leaves the editor showing the curve it had, and is reported.
		Engine::getSong()->clearErrors();
		QDomElement damaged = saved.cloneNode(true).toElement();
		damaged.setAttribute(QStringLiteral("waveShape"), encode(std::vector<float>(curve.begin(), curve.end() - 1)));
		shaper->controls()->restoreState(damaged);
		QCOMPARE(shown(graph), curve);
		const QStringList errors = Engine::getSong()->errors().keys();
		QVERIFY2(errors.size() == 1 && errors.front().startsWith(QStringLiteral("Wave Shaper:"))
			&& errors.front().contains(QStringLiteral("\"waveShape\"")), qPrintable(errors.join(QStringLiteral(" | "))));
		Engine::getSong()->clearErrors();

		// The user draws on the curve; what they drew is saved and comes back exactly on a fresh editor.
		QTest::mouseClick(graph, Qt::LeftButton, Qt::NoModifier, QPoint(graph->width() / 4, graph->height() - 10));
		QTest::mouseClick(graph, Qt::LeftButton, Qt::NoModifier, QPoint(graph->width() / 2, 10));
		const std::vector<float> edited = shown(graph);
		QVERIFY2(edited != curve, "clicking the Wave Shaper graph drew nothing");
		QDomDocument editedDoc;
		QDomElement editedParent = editedDoc.createElement(QStringLiteral("effect"));
		const QDomElement editedState = shaper->controls()->saveState(editedDoc, editedParent);

		std::unique_ptr<Effect> reopened{Effect::instantiate(QStringLiteral("waveshaper"), chain, nullptr)};
		QVERIFY(reopened != nullptr);
		std::unique_ptr<EffectControlDialog> reopenedDialog{reopened->controls()->createView()};
		reopenedDialog->show();
		reopened->controls()->restoreState(editedState);
		auto reopenedGraphs = reopenedDialog->findChildren<Graph*>();
		QCOMPARE(reopenedGraphs.size(), 1);
		QCOMPARE(shown(reopenedGraphs.front()), edited);
		QVERIFY(Engine::getSong()->errors().isEmpty());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(GraphWaveformEditorGuiTest)

#include "GraphWaveformEditorGuiTest.moc"
