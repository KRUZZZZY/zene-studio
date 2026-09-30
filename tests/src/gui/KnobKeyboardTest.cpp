/*
 * KnobKeyboardTest.cpp - R8.5: a knob is reachable and operable from the keyboard, and named
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

/*! R8.5: every knob (FloatModelEditorBase) takes Tab focus and answers the keys a slider does -
 *  arrows step as one wheel notch does (with the wheel's modifiers), PageUp/PageDown step
 *  coarsely, Home/End go to the ends - and passes every other key on, so the transport's Space
 *  still plays with a knob focused. Its accessible name is its constructed name, else its
 *  model's display name, else its description, and a name set explicitly is kept. */

#include <QtTest>

#include "AutomatableModel.h"
#include "Engine.h"
#include "Knob.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! Records the keys that reach it, i.e. that the knob did not take.
class KeyCatcher : public QWidget
{
public:
	QList<int> keys;

protected:
	void keyPressEvent(QKeyEvent* ke) override
	{
		// QTest sends a modifier's own press before a chord; those pass on, rightly.
		const int key = ke->key();
		if (key != Qt::Key_Shift && key != Qt::Key_Control && key != Qt::Key_Alt && key != Qt::Key_Meta) { keys << key; }
	}
};

} // namespace

class KnobKeyboardTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }  // a model needs the engine
	void cleanupTestCase() { Engine::destroy(); }

	void tabReachesAKnob()
	{
		Knob knob(KnobType::Bright26);
		QCOMPARE(knob.focusPolicy(), Qt::StrongFocus);
	}

	void theKeysStepAndJumpLikeASlider()
	{
		FloatModel model(50.f, 0.f, 100.f, 1.f, nullptr, QStringLiteral("Cutoff"));
		KeyCatcher parent;
		Knob knob(KnobType::Bright26, &parent);
		knob.setModel(&model);
		parent.show();
		knob.setFocus();

		QTest::keyClick(&knob, Qt::Key_Up);
		QCOMPARE(model.value(), 51.f);  // one notch: 100 / 100 = 1 step
		QTest::keyClick(&knob, Qt::Key_Left);
		QCOMPARE(model.value(), 50.f);
		QTest::keyClick(&knob, Qt::Key_PageUp);
		QCOMPARE(model.value(), 60.f);  // coarse: a tenth of the range
		QTest::keyClick(&knob, Qt::Key_Up, Qt::ShiftModifier);
		QCOMPARE(model.value(), 70.f);  // Shift is coarse, as on the wheel
		QTest::keyClick(&knob, Qt::Key_Home);
		QCOMPARE(model.value(), 0.f);
		QTest::keyClick(&knob, Qt::Key_End);
		QCOMPARE(model.value(), 100.f);
		QTest::keyClick(&knob, Qt::Key_Down, Qt::ControlModifier);
		QCOMPARE(model.value(), 99.f);  // Control is fine; never less than one step

		QVERIFY2(parent.keys.isEmpty(), qPrintable(QStringLiteral("a stepping key reached the parent: 0x") + QString::number(parent.keys.value(0), 16)));
		QTest::keyClick(&knob, Qt::Key_Space);
		QCOMPARE(parent.keys, QList<int>{Qt::Key_Space});  // not taken: the transport still has it
		QCOMPARE(model.value(), 99.f);
	}

	void theNameComesFromTheKnobItsModelOrItsDescription()
	{
		FloatModel model(0.f, 0.f, 1.f, 0.01f, nullptr, QStringLiteral("Resonance"));
		Knob named(KnobType::Bright26, nullptr, QStringLiteral("Filter resonance"));
		named.setModel(&model);
		QCOMPARE(named.accessibleName(), QStringLiteral("Filter resonance"));

		Knob fromModel(KnobType::Bright26);
		fromModel.setModel(&model);
		QCOMPARE(fromModel.accessibleName(), QStringLiteral("Resonance"));

		FloatModel anonymous(0.f, 0.f, 1.f, 0.01f);
		Knob fromHint(KnobType::Bright26);
		fromHint.setModel(&anonymous);
		fromHint.setHintText(QStringLiteral("Attack:"), QStringLiteral("ms"));
		fromHint.show();  // the hint usually comes after the model; the name settles on show
		QCOMPARE(fromHint.accessibleName(), QStringLiteral("Attack"));

		Knob explicitName(KnobType::Bright26);
		explicitName.setAccessibleName(QStringLiteral("Master pitch"));
		explicitName.setModel(&model);
		QCOMPARE(explicitName.accessibleName(), QStringLiteral("Master pitch"));
	}
};

QTEST_MAIN(KnobKeyboardTest)
#include "KnobKeyboardTest.moc"
