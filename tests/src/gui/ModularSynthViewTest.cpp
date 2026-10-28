/*
 * ModularSynthViewTest.cpp - Q050-059.1: the modular instrument's window
 *
 * The window has a knob bound to every module parameter an agent reads with plugin.param_get, and lists the wiring.
 * Turning a knob with the mouse wheel is the same change: plugin.param_get reads it back and the note is heard at
 * the new pitch. A graph replaced by a state restore rebuilds the window onto the new modules' parameters.
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

	//! The window: a knob bound to every parameter and the wiring; turning a knob is the change an agent reads back
	//! and hears; a replaced graph rebuilds the window onto the new modules.
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

		const auto knobs = [&] { return view->findChildren<gui::Knob*>(); };
		for (int i = 0; i < m_instrument->parameterCount(); ++i)
		{
			bool bound = false;
			for (const gui::Knob* k : knobs()) { bound |= k->model() == m_instrument->parameterModel(i); }
			QVERIFY2(bound, qPrintable(m_instrument->parameterName(i) + " has a knob in the window"));
		}
		auto* wiringLabel = view->findChild<QLabel*>(QStringLiteral("modularWiring"));
		QVERIFY(wiringLabel != nullptr);
		QVERIFY2(wiringLabel->text().contains(QStringLiteral("osc1 -> filter1.in")), qPrintable(wiringLabel->text()));

		const int crossingsBefore = zeroCrossings(playNote(57, 30), static_cast<std::size_t>(m_fpp));
		auto* coarse = view->findChild<gui::Knob*>(QStringLiteral("osc1.coarse"));
		QVERIFY(coarse != nullptr);
		QVERIFY(coarse->isVisible() || coarse->isVisibleTo(&parent));
		const QPointF at(coarse->width() / 2.0, coarse->height() / 2.0);
		for (int step = 0; step < 12; ++step)
		{
			QWheelEvent up(at, coarse->mapToGlobal(at.toPoint()), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
				Qt::NoScrollPhase, false);
			QApplication::sendEvent(coarse, &up);
		}
		const float turned = model(QStringLiteral("Osc 1 Coarse"))->value<float>();
		QVERIFY2(turned > 0.f, qPrintable(QStringLiteral("the wheel raised the pitch knob (%1)").arg(turned)));
		const ControlResult got = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), control::trackIdOf(m_track)},
			{QStringLiteral("plugin"), QStringLiteral("inst")}, {QStringLiteral("name"), QStringLiteral("Osc 1 Coarse")}});
		QVERIFY2(got.ok, qPrintable(got.errorMessage));
		QCOMPARE(got.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble(),
			static_cast<double>(turned));
		const int crossingsAfter = zeroCrossings(playNote(57, 30), static_cast<std::size_t>(m_fpp));
		const double expected = crossingsBefore * std::exp2(turned / 12.0);
		QVERIFY2(std::abs(crossingsAfter - expected) < 0.05 * expected,
			qPrintable(QStringLiteral("+%1 semitones: %2 -> %3 crossings").arg(turned).arg(crossingsBefore).arg(crossingsAfter)));

		QDomDocument smaller;
		restore(stateElement(smaller, QStringLiteral(
			"<module id=\"o\" type=\"oscillator\"/><module id=\"out\" type=\"output\"/><wire from=\"o\" to=\"out\" port=\"in\"/>")));
		QApplication::processEvents();
		QCOMPARE(knobs().size(), 5);
		for (const gui::Knob* k : knobs())
		{
			bool current = false;
			for (int i = 0; i < m_instrument->parameterCount(); ++i) { current |= k->model() == m_instrument->parameterModel(i); }
			QVERIFY2(current, "every knob is bound to a parameter of the new graph");
		}
		QVERIFY(view->findChild<QLabel*>(QStringLiteral("modularWiring"))->text().contains(QStringLiteral("o -> out.in")));
	}
};

QTEST_MAIN(ModularSynthViewTest)
#include "ModularSynthViewTest.moc"
