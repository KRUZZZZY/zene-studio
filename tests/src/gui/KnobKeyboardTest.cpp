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
 *  model's display name, else its description, and a name set explicitly is kept.
 *
 *  0.5.0 UX decisions (themes 10 and 17): the other shared controls work without a mouse too - a fader,
 *  an LCD number and a combo box take Tab focus (not a click's) and answer the same keys, a model button
 *  takes Tab focus - and each tells a screen reader what it is (AccessibleWidgets.h): its role, its name
 *  and, for a value control, the value as shown plus a settable value interface. Nothing here installs the
 *  descriptions: the application object did, as it does in the application. */

#include <QtTest>

#include <QAccessible>
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QLayout>
#include <QLineEdit>
#include <QProcess>
#include <QStringList>
#include <QPixmap>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWheelEvent>
#include <QWidget>

#include "AutomatableModel.h"
#include "BarModelEditor.h"
#include "ColorChooser.h"
#include "ComboBox.h"
#include "ComboBoxModel.h"
#include "ConfigManager.h"
#include "ControlCues.h"
#include "DesignTokens.h"
#include "Draggable.h"
#include "Engine.h"
#include "Fader.h"
#include "Knob.h"
#include "LcdFloatSpinBox.h"
#include "LcdSpinBox.h"
#include "LedCheckBox.h"
#include "TempoSyncBarModelEditor.h"
#include "TempoSyncKnob.h"
#include "TempoSyncKnobModel.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QStringList qtWarnings;

void collectQtWarning(QtMsgType type, const QMessageLogContext&, const QString& message)
{
	if (type == QtWarningMsg || type == QtCriticalMsg)
	{
		qtWarnings.append(message);
	}
}

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

//! Exposes the knob's sizing reset so the accessible-mode regression can exercise the real path.
class ResizableKnob : public Knob
{
public:
	using Knob::Knob;
	using Knob::setFixedFontSizeLabelRendering;
};

class PaintCountingBarModelEditor final : public BarModelEditor
{
public:
	using BarModelEditor::BarModelEditor;

	int paintCount() const { return m_paintCount; }

protected:
	void paintEvent(QPaintEvent* event) override
	{
		++m_paintCount;
		BarModelEditor::paintEvent(event);
	}

private:
	int m_paintCount = 0;
};

//! A layout that reports a null item at index 0 below its count, which QLayout::itemAt allows ("must return nullptr").
class GapLayout final : public QLayout
{
public:
	using QLayout::QLayout;
	~GapLayout() override
	{
		qDeleteAll(m_items);
		m_items.clear();
	}

	void addItem(QLayoutItem* item) override { m_items.append(item); }
	QLayoutItem* itemAt(int index) const override { return index >= 0 && index < m_items.size() ? m_items.at(index) : nullptr; }
	QLayoutItem* takeAt(int index) override { return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr; }
	int count() const override { return static_cast<int>(m_items.size()); }
	QSize sizeHint() const override { return {}; }

private:
	QList<QLayoutItem*> m_items;
};

} // namespace

class KnobKeyboardTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// The focused accessible-sizing ctest runs this executable in a fresh process. Configure
		// accessible mode before the design tokens capture the startup appearance.
		if (qEnvironmentVariableIsSet("ZENE_TEST_ACCESSIBLE"))
		{
			ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("density"), QStringLiteral("accessible"));
		}
		Engine::init(true);  // a model needs the engine
	}
	void cleanupTestCase() { Engine::destroy(); }

	//! Controls shown before the first event-loop turn still receive their interaction cues.
	void cuesPrepareControlsShownBeforeTheEventLoop()
	{
		FloatModel model(0.5f, 0.f, 1.f, 0.01f, nullptr, QStringLiteral("Level"));
		QWidget window;
		auto* knob = new Knob(KnobType::Bright26, &window);
		knob->setModel(&model);
		window.show();

		QVERIFY(knob->testAttribute(Qt::WA_Hover));
		QVERIFY(knob->whatsThis().contains(cues::interactionHelp(cues::Kind::Knob)));
	}

	void tabReachesAKnob()
	{
		Knob knob(KnobType::Bright26);
		QCOMPARE(knob.focusPolicy(), Qt::StrongFocus);
	}

	//! A TempoSyncKnob whose model is detached has no TempoSyncKnobModel to show, so
	//! modelChanged() warns and stops. It used to warn and then dereference the null
	//! model in updateDescAndIcon(), crashing on the detach.
	void aTempoSyncKnobStopsAfterWarningAboutADetachedModel()
	{
		TempoSyncKnobModel model(1.f, 0.f, 100.f, 1.f, 1.f, nullptr, QStringLiteral("Time"));
		TempoSyncKnob knob(KnobType::Bright26);
		knob.setModel(&model);
		QCOMPARE(knob.model(), &model);

		qtWarnings.clear();
		const auto oldHandler = qInstallMessageHandler(collectQtWarning);
		knob.setModel(nullptr);  // detach: modelChanged() runs with no TempoSyncKnobModel
		qInstallMessageHandler(oldHandler);

		QVERIFY(knob.model() == nullptr);
		QVERIFY2(qtWarnings.contains(QStringLiteral("no TempoSyncKnobModel has been set!")),
			qPrintable(qtWarnings.join(QLatin1Char('\n'))));
		QCOMPARE(knob.syncDescription(), QStringLiteral("Tempo Sync"));
	}

	//! The bar editor has the knob's contract: a detached model warns and stops. A context
	//! menu on the detached bar, or a later sync change on the model it left, must not reach
	//! the missing TempoSyncKnobModel (modelChanged() used to dereference it).
	void aTempoSyncBarModelEditorStopsAfterWarningAboutADetachedModel()
	{
		TempoSyncKnobModel model(1.f, 0.f, 100.f, 1.f, 1.f, nullptr, QStringLiteral("Time"));
		TempoSyncBarModelEditor editor(QStringLiteral("Time"), &model);
		QCOMPARE(editor.model(), &model);

		qtWarnings.clear();
		const auto oldHandler = qInstallMessageHandler(collectQtWarning);
		editor.setModel(nullptr);  // detach: modelChanged() runs with no TempoSyncKnobModel
		qInstallMessageHandler(oldHandler);

		QVERIFY(editor.model() == nullptr);
		QVERIFY2(qtWarnings.contains(QStringLiteral("no TempoSyncKnobModel has been set!")),
			qPrintable(qtWarnings.join(QLatin1Char('\n'))));
		QCOMPARE(editor.syncDescription(), QStringLiteral("Tempo Sync"));

		model.setTempoSync(TempoSyncKnobModel::SyncMode::QuarterNote);
		QCOMPARE(editor.syncDescription(), QStringLiteral("Tempo Sync"));

		QContextMenuEvent menuEvent(QContextMenuEvent::Mouse, QPoint(1, 1));
		QApplication::sendEvent(&editor, &menuEvent);
	}

	//! A visible bar editor schedules a repaint when its model is detached; painting it must
	//! not dereference the now-missing FloatModel.
	void aDetachedTempoSyncBarModelEditorCanPaint()
	{
		TempoSyncKnobModel model(1.f, 0.f, 100.f, 1.f, 1.f, nullptr, QStringLiteral("Time"));
		TempoSyncBarModelEditor editor(QStringLiteral("Time"), &model);
		editor.resize(120, 24);
		editor.show();
		QApplication::processEvents();

		editor.setModel(nullptr);
		QVERIFY(editor.model() == nullptr);
		editor.repaint();
	}

	//! A plain FloatModel is not a TempoSyncKnobModel, yet the constructor accepts it. The
	//! editor must warn and stay empty rather than dereference the failed cast.
	void aTempoSyncBarModelEditorRefusesAPlainFloatModel()
	{
		FloatModel plain(0.5f, 0.f, 1.f, 0.01f, nullptr, QStringLiteral("Level"));

		qtWarnings.clear();
		const auto oldHandler = qInstallMessageHandler(collectQtWarning);
		TempoSyncBarModelEditor editor(QStringLiteral("Level"), &plain);
		qInstallMessageHandler(oldHandler);

		QVERIFY(editor.model() == nullptr);
		QVERIFY2(qtWarnings.contains(QStringLiteral("no TempoSyncKnobModel has been set!")),
			qPrintable(qtWarnings.join(QLatin1Char('\n'))));
		QCOMPARE(editor.syncDescription(), QStringLiteral("Tempo Sync"));
	}

	//! The guard must not change the normal path: a valid model still drives the description.
	void aTempoSyncBarModelEditorStillFollowsItsModel()
	{
		TempoSyncKnobModel model(1.f, 0.f, 100.f, 1.f, 1.f, nullptr, QStringLiteral("Time"));
		TempoSyncBarModelEditor editor(QStringLiteral("Time"), &model);

		model.setTempoSync(TempoSyncKnobModel::SyncMode::QuarterNote);
		QCOMPARE(editor.syncDescription(), QStringLiteral("Synced to Quarter Note"));
	}

	void barModelEditorStoresItsFixedVerticalPolicy()
	{
		FloatModel model(0.5f, 0.f, 1.f, 0.01f, nullptr, QStringLiteral("Level"));
		BarModelEditor editor(QStringLiteral("Level"), &model);

		auto const storedPolicy = static_cast<QWidget const &>(editor).sizePolicy();
		QVERIFY(storedPolicy.horizontalPolicy() == QSizePolicy::Preferred);
		QVERIFY(storedPolicy.verticalPolicy() == QSizePolicy::Fixed);
	}

	void changingBarModelEditorColorsSchedulesRepaints()
	{
		FloatModel model(0.5f, 0.f, 1.f, 0.01f);
		PaintCountingBarModelEditor editor(QStringLiteral("Gain"), &model);
		editor.resize(120, 24);
		editor.show();
		QTRY_VERIFY(editor.paintCount() > 0);

		int paintCount = editor.paintCount();
		editor.setBackgroundBrush(QBrush(Qt::red));
		QTRY_VERIFY(editor.paintCount() > paintCount);

		paintCount = editor.paintCount();
		editor.setBarBrush(QBrush(Qt::green));
		QTRY_VERIFY(editor.paintCount() > paintCount);

		paintCount = editor.paintCount();
		editor.setTextColor(Qt::blue);
		QTRY_VERIFY(editor.paintCount() > paintCount);
	}

	void aDraggableWithAConstantModelStaysPut()
	{
		FloatModel model(5.f, 0.f, 10.f, 0.1f);
		QWidget parent;
		Draggable draggable(FloatModelEditorBase::DirectionOfManipulation::Vertical,
			&model, QPixmap(8, 8), 20, 80, &parent);
		const QPoint expectedPosition(23, 29);

		model.setRange(5.f, 5.f, 0.1f);
		draggable.move(expectedPosition);
		QVERIFY(QMetaObject::invokeMethod(&draggable, "handleMovement", Qt::DirectConnection));
		QCOMPARE(draggable.pos(), expectedPosition);
	}

	void aDraggableWithCoincidentEndpointsDoesNotChangeTheModel()
	{
		FloatModel model(50.f, 0.f, 100.f, 1.f);
		QWidget parent;
		Draggable draggable(FloatModelEditorBase::DirectionOfManipulation::Vertical,
			&model, QPixmap(10, 10), 50, 50, &parent);
		parent.show();

		QTest::mousePress(&draggable, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
		QTest::mouseMove(&draggable, QPoint(6, 6));
		QTest::mouseRelease(&draggable, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
		QCOMPARE(model.value(), 50.f);
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

	void lcdFloatWheelHonorsInvertedDirection()
	{
		FloatModel model(5.f, 0.f, 10.f, 1.f, nullptr, QStringLiteral("Frequency"));
		LcdFloatSpinBox spinBox(2, 2, QStringLiteral("Frequency"));
		spinBox.setModel(&model);
		spinBox.show();
		QApplication::processEvents();

		const QPointF localPosition(1, 1);
		const QPointF globalPosition(spinBox.mapToGlobal(localPosition.toPoint()));
		QWheelEvent regularWheel(localPosition, globalPosition, QPoint(), QPoint(0, 120), Qt::NoButton,
			Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(&spinBox, &regularWheel);
		QCOMPARE(model.value(), 6.f);

		QWheelEvent invertedWheel(localPosition, globalPosition, QPoint(), QPoint(0, 120), Qt::NoButton,
			Qt::NoModifier, Qt::NoScrollPhase, true);
		QApplication::sendEvent(&spinBox, &invertedWheel);
		QCOMPARE(model.value(), 5.f);
	}

	void parentlessColorChooserHandlesKeyRelease()
	{
		ColorChooser chooser;
		qtWarnings.clear();
		const auto oldHandler = qInstallMessageHandler(collectQtWarning);
		QKeyEvent event(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
		QApplication::sendEvent(&chooser, &event);
		qInstallMessageHandler(oldHandler);

		QVERIFY2(qtWarnings.isEmpty(), qPrintable(qtWarnings.join(QLatin1Char('\n'))));
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

	void aFaderAnswersTheSliderKeys()
	{
		FloatModel model(1.f, 0.f, 2.f, 0.01f, nullptr, QStringLiteral("Volume"));
		KeyCatcher parent;
		Fader fader(&model, QStringLiteral("Channel volume"), &parent);
		QCOMPARE(fader.focusPolicy(), Qt::TabFocus);
		parent.show();

		QTest::keyClick(&fader, Qt::Key_Up);
		QVERIFY(model.value() > 1.f);  // a step of the wheel's size, in dB
		QTest::keyClick(&fader, Qt::Key_Down);
		QTest::keyClick(&fader, Qt::Key_Down);
		QVERIFY(model.value() < 1.f);
		QTest::keyClick(&fader, Qt::Key_End);
		QCOMPARE(model.value(), 2.f);
		QTest::keyClick(&fader, Qt::Key_Home);
		QCOMPARE(model.value(), 0.f);
		QVERIFY(parent.keys.isEmpty());
		QTest::keyClick(&fader, Qt::Key_Space);
		QCOMPARE(parent.keys, QList<int>{Qt::Key_Space});
	}

	void anLcdNumberAnswersTheSliderKeys()
	{
		IntModel model(5, 0, 100, nullptr, QStringLiteral("Steps"));
		KeyCatcher parent;
		LcdSpinBox lcd(3, &parent);
		lcd.setModel(&model);
		QCOMPARE(lcd.focusPolicy(), Qt::TabFocus);
		parent.show();

		QTest::keyClick(&lcd, Qt::Key_Up);
		QCOMPARE(model.value(), 6);
		QTest::keyClick(&lcd, Qt::Key_PageUp);
		QCOMPARE(model.value(), 16);  // ten steps
		QTest::keyClick(&lcd, Qt::Key_End);
		QCOMPARE(model.value(), 100);
		QTest::keyClick(&lcd, Qt::Key_Home);
		QCOMPARE(model.value(), 0);
		QTest::keyClick(&lcd, Qt::Key_Space);
		QCOMPARE(parent.keys, QList<int>{Qt::Key_Space});
	}

	void horizontalWheelEventIsIgnoredByIntegerLcdSpinBox()
	{
		IntModel integerModel(50, 0, 100);
		LcdSpinBox integerLcd(3, nullptr);
		integerLcd.setModel(&integerModel);
		QWheelEvent integerWheel(QPointF(0, 0), QPointF(0, 0), QPoint(120, 0), QPoint(120, 0),
			Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QCoreApplication::sendEvent(&integerLcd, &integerWheel);
		QCOMPARE(integerModel.value(), 50);
		QVERIFY(!integerWheel.isAccepted());
	}

	void horizontalWheelEventIsIgnoredByFloatLcdSpinBox()
	{
		FloatModel floatModel(50.f, 0.f, 100.f, 1.f);
		LcdFloatSpinBox floatLcd(3, 2);
		floatLcd.setModel(&floatModel);
		QWheelEvent floatWheel(QPointF(0, 0), QPointF(0, 0), QPoint(120, 0), QPoint(120, 0),
			Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QCoreApplication::sendEvent(&floatLcd, &floatWheel);
		QCOMPARE(floatModel.value(), 50.f);
		QVERIFY(!floatWheel.isAccepted());
	}

	void aComboBoxStepsLikeAList()
	{
		ComboBoxModel model(nullptr, QStringLiteral("Shape"));
		for (const char* item : {"Sine", "Square", "Saw"}) { model.addItem(QString::fromLatin1(item)); }
		KeyCatcher parent;
		ComboBox combo(&parent);
		combo.setModel(&model);
		QCOMPARE(combo.focusPolicy(), Qt::TabFocus);
		parent.show();

		QTest::keyClick(&combo, Qt::Key_Down);
		QCOMPARE(model.value(), 1);  // down the list: the next item, as the wheel turned towards you
		QTest::keyClick(&combo, Qt::Key_End);
		QCOMPARE(model.value(), 2);
		QTest::keyClick(&combo, Qt::Key_Up);
		QCOMPARE(model.value(), 1);
		QTest::keyClick(&combo, Qt::Key_Home);
		QCOMPARE(model.value(), 0);
		QTest::keyClick(&combo, Qt::Key_A);
		QCOMPARE(parent.keys, QList<int>{Qt::Key_A});
	}

	void aModelButtonIsReachedByTab()
	{
		LedCheckBox led(QStringLiteral("Loop"), nullptr);
		QCOMPARE(led.focusPolicy(), Qt::TabFocus);
	}

	void aKnobIsADialWithItsValue()
	{
		FloatModel cutoff(440.f, 20.f, 20000.f, 1.f, nullptr, QStringLiteral("Cutoff"));
		Knob knob(KnobType::Bright26);
		knob.setModel(&cutoff);
		knob.setHintText(QStringLiteral("Cutoff:"), QStringLiteral(" Hz"));
		QAccessibleInterface* face = QAccessible::queryAccessibleInterface(&knob);
		QVERIFY(face != nullptr);
		QCOMPARE(face->role(), QAccessible::Dial);
		QCOMPARE(face->text(QAccessible::Name), QStringLiteral("Cutoff"));
		QCOMPARE(face->text(QAccessible::Value), QStringLiteral("440 Hz"));  // as shown, without the caption
		QAccessibleValueInterface* value = face->valueInterface();
		QVERIFY(value != nullptr);
		QCOMPARE(value->currentValue().toFloat(), 440.f);
		QCOMPARE(value->minimumValue().toFloat(), 20.f);
		QCOMPARE(value->maximumValue().toFloat(), 20000.f);
		QCOMPARE(value->minimumStepSize().toFloat(), 1.f);
		value->setCurrentValue(1000.f);  // a screen reader's edit is the parameter's
		QCOMPARE(cutoff.value(), 1000.f);
		face->actionInterface()->doAction(QAccessibleActionInterface::increaseAction());
		QCOMPARE(cutoff.value(), 1001.f);
	}

	void theOtherControlsSayWhatTheyAre()
	{
		FloatModel level(1.f, 0.f, 2.f, 0.01f, nullptr, QStringLiteral("Volume"));
		QWidget strip;  // a control's constructed name is its title only inside a window, never a window's own
		Fader fader(&level, QStringLiteral("Channel volume"), &strip);
		QAccessibleInterface* face = QAccessible::queryAccessibleInterface(&fader);
		QCOMPARE(face->role(), QAccessible::Slider);
		QCOMPARE(face->text(QAccessible::Name), QStringLiteral("Channel volume"));  // its constructed name
		QCOMPARE(face->text(QAccessible::Value), QStringLiteral("0.00 dB"));
		QVERIFY(face->valueInterface() != nullptr);

		IntModel steps(5, 0, 100, nullptr, QStringLiteral("Steps"));
		LcdSpinBox lcd(3, nullptr);
		lcd.setModel(&steps);
		lcd.setDisplayOffset(1);
		face = QAccessible::queryAccessibleInterface(&lcd);
		QCOMPARE(face->role(), QAccessible::SpinBox);
		QCOMPARE(face->text(QAccessible::Name), QStringLiteral("Steps"));
		QCOMPARE(face->text(QAccessible::Value), QStringLiteral("6"));  // as shown, offset included

		ComboBoxModel shapes(nullptr, QStringLiteral("Shape"));
		shapes.addItem(QStringLiteral("Sine"));
		shapes.addItem(QStringLiteral("Saw"));
		ComboBox combo(nullptr);
		combo.setModel(&shapes);
		face = QAccessible::queryAccessibleInterface(&combo);
		QCOMPARE(face->role(), QAccessible::ComboBox);
		QCOMPARE(face->text(QAccessible::Value), QStringLiteral("Sine"));
		face->actionInterface()->doAction(QAccessibleActionInterface::increaseAction());
		QCOMPARE(face->text(QAccessible::Value), QStringLiteral("Saw"));

		LedCheckBox led(QStringLiteral("Loop"), nullptr, QStringLiteral("Loop playback"));
		face = QAccessible::queryAccessibleInterface(&led);
		QCOMPARE(face->role(), QAccessible::CheckBox);
		QCOMPARE(face->text(QAccessible::Name), QStringLiteral("Loop playback"));
		QVERIFY(face->state().checkable);
		QVERIFY(!face->state().checked);
		face->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
		QVERIFY(led.model()->value());
		QVERIFY(face->state().checked);
	}

	//! The focus ring follows the keyboard onto a custom control that draws none itself (CSS's :focus-visible):
	//! Tab shows it over the control, a click does not, and a screen reader never sees it as a child.
	void keyboardFocusShowsTheRing()
	{
		FloatModel model(0.f, 0.f, 1.f, 0.01f, nullptr, QStringLiteral("Drive"));
		QWidget window;
		auto* knob = new Knob(KnobType::Bright26, &window);
		knob->setModel(&model);
		auto* field = new QLineEdit(&window);
		field->move(0, 60);
		window.show();
		window.activateWindow();
		if (!QTest::qWaitForWindowActive(&window)) { QSKIP("this platform does not activate windows"); }

		field->setFocus(Qt::OtherFocusReason);  // activation may have focused the knob already; Tab moves focus from elsewhere
		knob->setFocus(Qt::TabFocusReason);
		const auto ringOn = [knob] { return knob->findChild<QWidget*>(QStringLiteral("zeneFocusRing"), Qt::FindDirectChildrenOnly); };
		QWidget* ring = ringOn();
		QVERIFY(ring != nullptr && ring->isVisible());
		QCOMPARE(ring->geometry(), knob->rect());
		QCOMPARE(QAccessible::queryAccessibleInterface(knob)->childCount(), 0);
		field->setFocus(Qt::TabFocusReason);
		QVERIFY(ringOn() == nullptr);  // a stock field rings itself (the stylesheet)
		knob->setFocus(Qt::MouseFocusReason);
		QVERIFY(ringOn() == nullptr);  // a click shows no ring outside accessible mode
	}

	//! 0.5.0 theme 23: every shared control says how it works in its What's This (after a panel's own text, never instead
	//! of it) and asks for hover repaints; the kinds are told apart.
	void everySharedControlSaysHowItWorks()
	{
		cues::install();  // runs by itself at startup; again does nothing
		FloatModel level(0.5f, 0.f, 1.f, 0.01f, nullptr, QStringLiteral("Level"));
		IntModel number(4, 0, 99, nullptr, QStringLiteral("Steps"));
		ComboBoxModel choice(nullptr, QStringLiteral("Wave"));
		choice.addItem(QStringLiteral("Sine"));
		QWidget window;
		auto* knob = new Knob(KnobType::Bright26, &window);
		knob->setModel(&level);
		knob->setWhatsThis(QStringLiteral("The cutoff frequency."));
		auto* fader = new Fader(&level, QStringLiteral("Volume"), &window);
		auto* lcd = new LcdSpinBox(2, &window);
		lcd->setModel(&number);
		auto* combo = new ComboBox(&window);
		combo->setModel(&choice);
		auto* led = new LedCheckBox(QStringLiteral("Loop"), &window);
		window.show();  // the first show polishes them: the cues are added then

		const QList<std::pair<QWidget*, cues::Kind>> expected{{knob, cues::Kind::Knob}, {fader, cues::Kind::Fader},
			{lcd, cues::Kind::Number}, {combo, cues::Kind::Choice}, {led, cues::Kind::Toggle}};
		for (const auto& [widget, kind] : expected)
		{
			QCOMPARE(cues::kindOf(widget), kind);
			QVERIFY2(widget->whatsThis().contains(cues::interactionHelp(kind)), qPrintable(widget->metaObject()->className()));
			QVERIFY(widget->whatsThis().contains(QStringLiteral("Right-click")));
			QVERIFY(widget->testAttribute(Qt::WA_Hover));
		}
		QVERIFY(knob->whatsThis().startsWith(QStringLiteral("The cutoff frequency.")));
		QCOMPARE(cues::kindOf(&window), cues::Kind::None);
		QVERIFY(cues::interactionHelp(cues::Kind::Knob).contains(QStringLiteral("wheel")));
	}

	//! A control with a tooltip shows the one-line reminder under it; one without shows none.
	void aTooltipCarriesTheReminder()
	{
		cues::install();
		FloatModel level(1.f, 0.f, 2.f, 0.01f, nullptr, QStringLiteral("Volume"));
		QWidget window;
		auto* fader = new Fader(&level, QStringLiteral("Volume"), &window);
		fader->setToolTip(QStringLiteral("-6.0 dB"));
		window.show();
		QHelpEvent help(QEvent::ToolTip, QPoint(2, 2), fader->mapToGlobal(QPoint(2, 2)));
		QApplication::sendEvent(fader, &help);
		QVERIFY2(QToolTip::text().contains(cues::interactionHint(cues::Kind::Fader)), qPrintable(QToolTip::text()));
		QVERIFY(QToolTip::text().startsWith(QStringLiteral("-6.0 dB")));
		QToolTip::hideText();
	}

	//! The hover state is drawn only for an enabled control under the pointer; outside accessible mode a control keeps the
	//! size it was given (the one global zoom does every other sizing).
	void hoverIsForEnabledControlsAndSizesStay()
	{
		FloatModel level(0.5f, 0.f, 1.f, 0.01f, nullptr, QStringLiteral("Level"));
		QWidget window;
		auto* row = new QHBoxLayout(&window);
		auto* knob = new Knob(KnobType::Small17, &window);
		knob->setModel(&level);
		row->addWidget(knob);
		const QSize before = knob->size();
		window.show();
		QCOMPARE(knob->size(), before);
		knob->setAttribute(Qt::WA_UnderMouse, true);
		QVERIFY(cues::hovered(knob));
		knob->setEnabled(false);
		QVERIFY(!cues::hovered(knob));
		QVERIFY(!cues::hovered(nullptr));
	}

	//! A knob's own fixed-size reset must keep the larger hit target applied at polish time.
	void accessibleTargetSurvivesKnobSizingReset()
	{
		if (!qEnvironmentVariableIsSet("ZENE_TEST_ACCESSIBLE"))
		{
			// Accessible mode is fixed for the whole process at startup, and every other slot here assumes the
			// normal mode, so the whole-executable run cannot set it. Run this slot for real in a fresh process
			// that has it (the KnobAccessibleTargetSizingTest registration does the same directly).
			QProcess child;
			QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
			environment.insert(QStringLiteral("ZENE_TEST_ACCESSIBLE"), QStringLiteral("1"));
			child.setProcessEnvironment(environment);
			child.setProcessChannelMode(QProcess::MergedChannels);
			child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("accessibleTargetSurvivesKnobSizingReset")});
			QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
			QVERIFY2(child.waitForFinished(120000), "the accessible-mode child did not finish");
			QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
				qPrintable(QString::fromLocal8Bit(child.readAll())));
			return;
		}
		QVERIFY(qEnvironmentVariableIsSet("ZENE_TEST_ACCESSIBLE"));
		QVERIFY(tokens::accessibleMode());
		const int target = tokens::targetSize();
		QVERIFY(target > 26);

		QWidget window;
		auto* layout = new QVBoxLayout{&window};
		auto* knob = new ResizableKnob(KnobType::Small17, &window);
		layout->addWidget(knob);
		window.show();
		QTRY_VERIFY(knob->minimumWidth() >= target);
		QTRY_VERIFY(knob->minimumHeight() >= target);

		knob->setFixedFontSizeLabelRendering();  // updateFixedSize() resets both fixed dimensions
		QVERIFY2(knob->minimumWidth() >= target, qPrintable(QStringLiteral("width reset to %1, target is %2")
			.arg(knob->minimumWidth()).arg(target)));
		QVERIFY2(knob->minimumHeight() >= target, qPrintable(QStringLiteral("height reset to %1, target is %2")
			.arg(knob->minimumHeight()).arg(target)));

		knob->setknobNum(KnobType::Bright26);  // the pixmap update previously bypassed the accessible target
		QVERIFY2(knob->minimumWidth() >= target, qPrintable(QStringLiteral("type change shrank width to %1, target is %2")
			.arg(knob->minimumWidth()).arg(target)));
		QVERIFY2(knob->minimumHeight() >= target, qPrintable(QStringLiteral("type change shrank height to %1, target is %2")
			.arg(knob->minimumHeight()).arg(target)));

		knob->setEnabled(false);  // EnabledChange uses the same direct pixmap sizing path.
		QVERIFY2(knob->minimumWidth() >= target, qPrintable(QStringLiteral("disable shrank width to %1, target is %2")
			.arg(knob->minimumWidth()).arg(target)));
		QVERIFY2(knob->minimumHeight() >= target, qPrintable(QStringLiteral("disable shrank height to %1, target is %2")
			.arg(knob->minimumHeight()).arg(target)));
	}

	//! Target sizing searches the parent's layout; a null item the layout reports below its count must be stepped over.
	void accessibleTargetSkipsNullLayoutItems()
	{
		if (!qEnvironmentVariableIsSet("ZENE_TEST_ACCESSIBLE"))
		{
			// Accessible mode is fixed at startup, so run this slot in a fresh process that has it (see the slot above).
			QProcess child;
			QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
			environment.insert(QStringLiteral("ZENE_TEST_ACCESSIBLE"), QStringLiteral("1"));
			child.setProcessEnvironment(environment);
			child.setProcessChannelMode(QProcess::MergedChannels);
			child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("accessibleTargetSkipsNullLayoutItems")});
			QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
			QVERIFY2(child.waitForFinished(120000), "the accessible-mode child did not finish");
			QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
				qPrintable(QString::fromLocal8Bit(child.readAll())));
			return;
		}
		QVERIFY(tokens::accessibleMode());
		const int target = tokens::targetSize();

		QWidget parent;
		auto* layout = new GapLayout(&parent);
		layout->addItem(nullptr);  // index 0 reports no item; the knob sits at index 1
		auto* knob = new Knob(KnobType::Small17, &parent);
		layout->addWidget(knob);
		knob->ensurePolished();  // the Polish event runs the cue's target sizing, which searches the parent's layout
		QVERIFY2(knob->minimumWidth() >= target, qPrintable(QStringLiteral("width %1, target is %2")
			.arg(knob->minimumWidth()).arg(target)));
		QVERIFY2(knob->minimumHeight() >= target, qPrintable(QStringLiteral("height %1, target is %2")
			.arg(knob->minimumHeight()).arg(target)));
	}
};

QTEST_MAIN(KnobKeyboardTest)
#include "KnobKeyboardTest.moc"
