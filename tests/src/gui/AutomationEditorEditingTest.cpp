/*
 * AutomationEditorEditingTest.cpp - the automation editor edited from the keyboard and from its item list
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

/*! ED-194: the automation editor's arrow keys only scrolled, and the editable item list had no automation surface.
 *  Against the REAL application and a real instrument's parameter: Ctrl+Left/Right pick a point, Left/Right move it a
 *  grid step and Up/Down change its value - each key one automation.move_point, so one undo step; a move onto another
 *  point is refused and leaves it; Escape gives the arrows back to scrolling. A control the command surface cannot
 *  name (the track's volume) moves the same way, one journal step. window.item_list's "automation" surface lists the
 *  open clip's points and its cell edits move them. ED-180: a sample clip's fades are drawn behind the automation. */

#include <QtTest>

#include <QTemporaryDir>

#include <cmath>

#include "../core/ReversibilityTestSupport.h"
#include "AudioEngine.h"
#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "AutomationTrack.h"
#include "ClipEdits.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "ItemList.h"
#include "MainWindow.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QList<int> ticksOf(const AutomationClip* clip)
{
	QList<int> ticks;
	for (auto it = clip->getTimeMap().constBegin(); it != clip->getTimeMap().constEnd(); ++it) { ticks.append(it.key()); }
	return ticks;
}

} // namespace

class AutomationEditorEditingTest : public QObject
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
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		m_track = revtest::addInstrumentTrack();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void arrowKeysMoveAPointThroughTheCommand()
	{
#ifdef Q_OS_WIN
		if (m_track.isEmpty()) { QSKIP("plugin modules cannot load in a Windows test host (ControlAutomationModesTest)"); }
#endif
		QVERIFY2(!m_track.isEmpty(), "no instrument to drive (LMMS_TEST_PLUGIN_DIR)");
		AutomatableModel* model = nullptr;
		QString parameter;
		QVERIFY(firstParameter(&model, &parameter));
		const float low = model->minValue<float>();
		const float high = model->maxValue<float>();
		const QJsonObject which{{QStringLiteral("track"), m_track}, {QStringLiteral("parameter"), parameter}};
		for (const auto& [tick, value] : {std::pair{0, low}, std::pair{96, high}})
		{
			QJsonObject add = which;
			add.insert(QStringLiteral("ticks"), tick);
			add.insert(QStringLiteral("value"), static_cast<double>(value));
			QVERIFY(invoke(QStringLiteral("automation.add_point"), add).ok);
		}
		AutomationClip* clip = control::existingAutomationClip(model);
		QVERIFY(clip != nullptr);
		AutomationEditor* editor = open(clip);
		const int q = AutomationClip::quantization();
		QVERIFY(q > 1 && q < 96);

		QTest::keyClick(editor, Qt::Key_Right, Qt::ControlModifier);  // the first point
		QTest::keyClick(editor, Qt::Key_Right);  // a grid step later
		QCOMPARE(ticksOf(clip), (QList<int>{q, 96}));
		QVERIFY2(!revtest::stateOf(QStringLiteral("automation.move_point")).isEmpty(), "the key did not run automation.move_point");
		QVERIFY(invoke(QStringLiteral("control.undo")).ok);  // one key, one step
		QCOMPARE(ticksOf(clip), (QList<int>{0, 96}));

		// The undo took the chosen point (at q) away: Ctrl+Right, from no point, chooses the first again.
		QTest::keyClick(editor, Qt::Key_Right, Qt::ControlModifier);
		QTest::keyClick(editor, Qt::Key_Up);
		const float raised = clip->getTimeMap().value(TimePos(0)).getInValue();
		QVERIFY2(raised > low, "Up did not raise the point's value");
		QTest::keyClick(editor, Qt::Key_Down);
		QVERIFY(std::abs(clip->getTimeMap().value(TimePos(0)).getInValue() - low) < (high - low) * 1e-4f);

		// The next point; a coarse move onto the first is refused and leaves both where they were.
		QTest::keyClick(editor, Qt::Key_Right, Qt::ControlModifier);
		QTest::keyClick(editor, Qt::Key_Left, Qt::ShiftModifier);
		QTest::keyClick(editor, Qt::Key_Left, Qt::ShiftModifier);
		QCOMPARE(ticksOf(clip), (QList<int>{0, 96}));
		QTest::keyClick(editor, Qt::Key_Left);
		QCOMPARE(ticksOf(clip), (QList<int>{0, 96 - q}));
		QVERIFY(invoke(QStringLiteral("control.undo")).ok);

		// Escape: the arrows scroll again and move nothing.
		QTest::keyClick(editor, Qt::Key_Escape);
		QTest::keyClick(editor, Qt::Key_Right);
		QCOMPARE(ticksOf(clip), (QList<int>{0, 96}));
	}

	void aControlTheSurfaceCannotNameMovesToo()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* automation = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		QVERIFY(track != nullptr && automation != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(automation->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->addObject(track->volumeModel(), false);  // seeds one point at 0
		clip->putValue(TimePos(48), 40.f, false);
		control::ParameterAddress address;
		QVERIFY(!control::addressableParameterForModel(track->volumeModel(), &address));
		AutomationEditor* editor = open(clip);
		const int q = AutomationClip::quantization();
		QTest::keyClick(editor, Qt::Key_Left, Qt::ControlModifier);  // from none, Ctrl+Left picks the last point
		QTest::keyClick(editor, Qt::Key_Right);
		QCOMPARE(ticksOf(clip), (QList<int>{0, 48 + q}));
		QVERIFY(invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(ticksOf(clip), (QList<int>{0, 48}));
		QTest::keyClick(editor, Qt::Key_Escape);
	}

	void theItemListEditsTheAutomationPoints()
	{
		AutomationEditor* editor = getGUI()->automationEditor()->m_editor;
		QVERIFY(editor->validClip());
		const ControlResult opened = invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("automation")}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QCOMPARE(opened.result.value(QStringLiteral("count")).toInt(), 2);
		ItemListPanel* panel = ItemListPanel::find(editor, QStringLiteral("automation"));
		QVERIFY(panel != nullptr);
		ItemListSource* source = panel->source();
		const AutomationClip* clip = editor->currentClip();
		// The second point (48, in song ticks from the clip's start at 0) to 72, then to the top of its range.
		QVERIFY2(source->setValue(1, 0, 72), qPrintable(source->lastError()));
		QCOMPARE(ticksOf(clip), (QList<int>{0, 72}));
		QVERIFY2(source->setValue(1, 1, 100), qPrintable(source->lastError()));
		QCOMPARE(clip->getTimeMap().value(TimePos(72)).getInValue(), clip->firstObject()->maxValue<float>());
		QVERIFY(!source->setValue(1, 0, 0));  // onto the first point: refused
		QVERIFY(!source->lastError().isEmpty());
		QCOMPARE(ticksOf(clip), (QList<int>{0, 72}));
		QVERIFY(invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("automation")},
			{QStringLiteral("visible"), false}}).ok);
	}

	//! ED-180: a sample clip's fades (clip.fade_curve's gain curve) are drawn in the automation editor, over the clip
	//! drawn behind the automation; nothing drew them before.
	void aSampleClipsFadesAreDrawnBehindTheCurve()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		// Two bars of a steady tone, the clip two bars long.
		const int ticks = 2 * DefaultTicksPerBar;
		std::vector<SampleFrame> tone(static_cast<std::size_t>(Engine::framesPerTick() * ticks), SampleFrame(0.5f));
		clip->setSampleBuffer(std::make_shared<SampleBuffer>(std::move(tone), static_cast<int>(Engine::audioEngine()->outputSampleRate())));
		clip->changeLength(TimePos(ticks));
		AutomationEditorWindow* window = getGUI()->automationEditor();
		QVERIFY(window->m_editor->validClip());
		window->setGhostSample(clip);
		AutomationEditor* editor = window->m_editor;
		editor->resize(std::max(editor->width(), 800), std::max(editor->height(), 400));
		const QImage plain = editor->grab().toImage();
		ClipEdits faded = clip->clipEdits();
		faded.fadeInTicks = DefaultTicksPerBar;
		faded.fadeOutTicks = DefaultTicksPerBar / 2;
		clip->setClipEdits(faded);
		const QImage withFades = editor->grab().toImage();
		QVERIFY2(withFades != plain, "the clip's fades changed nothing the automation editor draws");
		clip->setClipEdits(ClipEdits{});
		QCOMPARE(editor->grab().toImage(), plain);  // the fades, and nothing else, made the difference
	}

private:
	bool firstParameter(AutomatableModel** model, QString* id) const
	{
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(m_track, &target, &error)) { return false; }
		for (const control::AutomationParameter& parameter : control::automationParameters(target))
		{
			auto* knob = dynamic_cast<FloatModel*>(parameter.model);
			if (knob != nullptr && knob->maxValue() > knob->minValue() && !knob->isScaleLogarithmic())
			{
				*model = knob;
				*id = parameter.id();
				return true;
			}
		}
		return false;
	}

	AutomationEditor* open(AutomationClip* clip) const
	{
		getGUI()->automationEditor()->open(clip);
		AutomationEditor* editor = getGUI()->automationEditor()->m_editor;
		editor->setFocus();
		QCoreApplication::processEvents();
		return editor;
	}

	QTemporaryDir m_home;
	QString m_track;
};

QTEST_MAIN(AutomationEditorEditingTest)
#include "AutomationEditorEditingTest.moc"
