/*
 * ModularSynthViewTest.cpp - Q050-059.1: the modular instrument's window
 *
 * The window has a knob for every module parameter an agent reads with plugin.param_get, and lists the wiring.
 * Turning a knob with the mouse wheel runs plugin.param_set, the agent's command: plugin.param_get reads it back, the
 * note is heard at the new pitch and one control.undo takes the turn back. A graph replaced by a state restore rebuilds
 * the window onto the new modules' parameters.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QApplication>
#include <QDomDocument>
#include <QJsonObject>
#include <QLabel>
#include <QWheelEvent>

#include <cmath>
#include <memory>

#include "../plugins/ModularSynthTestSupport.h"
#include "ControlVocabulary.h"
#include "InstrumentView.h"
#include "Knob.h"

using namespace lmms;
using namespace lmms::modular_test;


class ModularSynthViewTest : public ModularSynthFixture
{
	Q_OBJECT

private slots:
	void initTestCase() { initEngine(); }
	void cleanupTestCase() { cleanupEngine(); }

	//! The window: a knob for every parameter and the wiring. Turning a knob is the plugin.param_set an agent runs - an
	//! agent reads the value back, the note is heard at the new pitch, ONE control.undo (reporting plugin.param_set)
	//! takes the whole turn back and the knob follows. An agent's write moves the knob. A replaced graph rebuilds the
	//! window onto the new modules.
	void theWindowHasAKnobForEveryParameterAndTurningOneIsHeard()
	{
		fresh();
		setValue(QStringLiteral("LFO 1 Depth"), 0.f);
		setValue(QStringLiteral("Filter 1 Cutoff"), 20000.f);
		QWidget parent;
		std::unique_ptr<gui::PluginView> view(m_instrument->createView(&parent));
		QVERIFY(view != nullptr);
		parent.resize(260, 260);
		parent.show();
		QApplication::processEvents();
		verifyEveryParameterHasAKnob(view.get());
		auto* wiringLabel = view->findChild<QLabel*>(QStringLiteral("modularWiring"));
		QVERIFY(wiringLabel != nullptr);
		QVERIFY2(wiringLabel->text().contains(QStringLiteral("osc1 -> filter1.in")), qPrintable(wiringLabel->text()));

		const QString track = control::trackIdOf(m_track);
		const int crossingsBefore = zeroCrossings(playNote(57, 30), static_cast<std::size_t>(m_fpp));
		auto* coarse = view->findChild<gui::Knob*>(QStringLiteral("osc1.coarse"));
		QVERIFY(coarse != nullptr);
		QVERIFY(coarse->isVisibleTo(&parent));
		const QPointF at(coarse->width() / 2.0, coarse->height() / 2.0);
		for (int step = 0; step < 12; ++step)
		{
			QWheelEvent up(at, coarse->mapToGlobal(at.toPoint()), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
				Qt::NoScrollPhase, false);
			QApplication::sendEvent(coarse, &up);
		}
		const float turned = model(QStringLiteral("Osc 1 Coarse"))->value<float>();
		QVERIFY2(turned > 0.f, qPrintable(QStringLiteral("the wheel raised the pitch knob (%1)").arg(turned)));
		QCOMPARE(paramValue(track, QStringLiteral("Osc 1 Coarse")), static_cast<double>(turned));
		const int crossingsAfter = zeroCrossings(playNote(57, 30), static_cast<std::size_t>(m_fpp));
		const double expected = crossingsBefore * std::exp2(turned / 12.0);
		QVERIFY2(std::abs(crossingsAfter - expected) < 0.05 * expected,
			qPrintable(QStringLiteral("+%1 semitones: %2 -> %3 crossings").arg(turned).arg(crossingsBefore).arg(crossingsAfter)));

		// The turn was plugin.param_set calls on one parameter, coalesced into one undo step.
		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(undone.result.value(QStringLiteral("undone_command")).toString(), QStringLiteral("plugin.param_set"));
		QCOMPARE(paramValue(track, QStringLiteral("Osc 1 Coarse")), 0.0);
		QCOMPARE(coarse->model()->value(), 0.f);

		// An agent's write moves the knob; the knob is not a parameter of its own.
		QVERIFY(run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), track}, {QStringLiteral("plugin"), QStringLiteral("inst")},
			{QStringLiteral("name"), QStringLiteral("Osc 1 Coarse")}, {QStringLiteral("value"), -7.0}}).ok);
		QCOMPARE(coarse->model()->value(), -7.f);
		QCOMPARE(m_instrument->parameterCount(), 16);

		QDomDocument smaller;
		restore(stateElement(smaller, QStringLiteral(
			"<module id=\"o\" type=\"oscillator\"/><module id=\"out\" type=\"output\"/><wire from=\"o\" to=\"out\" port=\"in\"/>")));
		QApplication::processEvents();
		QCOMPARE(view->findChildren<gui::Knob*>().size(), 5);
		verifyEveryParameterHasAKnob(view.get());
		QVERIFY(view->findChild<QLabel*>(QStringLiteral("modularWiring"))->text().contains(QStringLiteral("o -> out.in")));
	}

private:
	//! Every parameter has a knob standing for it (by name) that shows its value.
	void verifyEveryParameterHasAKnob(QWidget* view)
	{
		for (int i = 0; i < m_instrument->parameterCount(); ++i)
		{
			const QString name = m_instrument->parameterName(i);
			bool shown = false;
			for (const gui::Knob* k : view->findChildren<gui::Knob*>())
			{
				shown |= k->property("parameter").toString() == name
					&& k->model()->value() == m_instrument->parameterModel(i)->value<float>();
			}
			QVERIFY2(shown, qPrintable(name + " has a knob in the window showing its value"));
		}
	}

	double paramValue(const QString& track, const QString& name)
	{
		const ControlResult got = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), track},
			{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("name"), name}});
		return got.ok ? got.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble() : -1000.0;
	}
};

QTEST_MAIN(ModularSynthViewTest)
#include "ModularSynthViewTest.moc"
