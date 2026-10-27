/*
 * VcaStripLifetimeTest.cpp - BUGS_FOUND 11.12: a removed VCA group's strip never paints freed models
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

/*! BUGS_FOUND 11.12, found by the gate run's ControlVcaCommands failing once and reproduced at
 *  ~1 in 12: vca.remove frees the group's fader, mute and solo models, the mixer rebuilds its VCA
 *  strips only on its 500 ms sync, and a paint in between read the freed fader model (SIGSEGV in
 *  Fader::calculateKnobPosYFromModel, fault address 0x6c - ModelView's QPointer had gone null).
 *  Here the window is made deterministic: the group is removed and the mixer is repainted AT
 *  ONCE, before any sync can run. Needs the real application (a Fader cannot be built without
 *  it - the reason VcaStripViewTest skips its strip slot). */

#include <QtTest>
#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>

#include "ControlRegistry.h"
#include "Engine.h"
#include "FadeButton.h"
#include "Fader.h"
#include "FloatModelEditorBase.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MixerView.h"
#include "LocklessRingBuffer.h"
#include "SampleFrame.h"
#include "VcaStripView.h"
#include "ColorChooser.h"
#include "FontHelper.h"
#include "../../../plugins/Vectorscope/VecControls.h"

// Reuse the product implementation in this preconfigured GUI test target. Its MOC data is not
// needed for the direct double-click call below, so keep VectorView's meta-object macros out.
#pragma push_macro("Q_OBJECT")
#pragma push_macro("slots")
#undef Q_OBJECT
#define Q_OBJECT
#undef slots
#define slots
#include "../../../plugins/Vectorscope/VectorView.cpp"
#pragma pop_macro("slots")
#pragma pop_macro("Q_OBJECT")

class TestVectorView : public lmms::gui::VectorView
{
public:
	using lmms::gui::VectorView::VectorView;
	void triggerDoubleClick()
	{
		QMouseEvent event{QEvent::MouseButtonDblClick, QPointF{}, QPointF{}, QPointF{},
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
		mouseDoubleClickEvent(&event);
	}
};

using namespace lmms;
using namespace lmms::gui;

//! ColorChooser has no Q_OBJECT, so findChildren<ColorChooser*> matched any QColorDialog (and Qt 6.12 refuses it):
//! list the dialogs and keep only real ColorChoosers.
static QList<ColorChooser*> colorChoosers(const QWidget& parent)
{
	QList<ColorChooser*> found;
	for (auto* dialog : parent.findChildren<QColorDialog*>())
	{
		if (auto* chooser = dynamic_cast<ColorChooser*>(dialog)) { found.append(chooser); }
	}
	return found;
}

class VcaStripLifetimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aRemovedGroupsStripIsNeverPaintedAgain()
	{
		MixerView* mixer = getGUI()->mixerView();
		getGUI()->mainWindow()->show();
		// The mixer is a shell panel since 0.5.0 (docs/UI-SHELL.md): bring it on screen the way
		// the interface does, not by showing a sub-window it no longer has.
		getGUI()->mainWindow()->revealPanel(QStringLiteral("mixer"));
		const ControlResult created = ControlRegistry::instance()->invoke(QStringLiteral("vca.create"),
			{{QStringLiteral("name"), QStringLiteral("Drums")}});
		QVERIFY2(created.ok, qPrintable(created.errorMessage));
		mixer->syncWithMixer();
		QCOMPARE(mixer->findChildren<VcaStripView*>().size(), 1);
		QVERIFY(mixer->findChildren<VcaStripView*>().first()->isVisible());

		const ControlResult removed = ControlRegistry::instance()->invoke(QStringLiteral("vca.remove"),
			{{QStringLiteral("group"), created.result.value(QStringLiteral("group")).toString()}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		// No event has run: the sync has not rebuilt anything. This paint is the one that crashed
		// (grab() renders synchronously; repaint() skips a window offscreen has not exposed).
		for (VcaStripView* strip : mixer->findChildren<VcaStripView*>()) { strip->grab(); }
		mixer->grab();
		for (VcaStripView* strip : mixer->findChildren<VcaStripView*>()) { QVERIFY(!strip->isVisible()); }
		QTRY_VERIFY(mixer->findChildren<VcaStripView*>().isEmpty());
	}

	void aFaderWhoseModelDiesPaintsNothing()
	{
		auto* model = new FloatModel(1.f, 0.f, 2.f, 0.01f);
		Fader fader(model, QStringLiteral("Doomed"), nullptr);
		fader.grab();
		delete model;
		QVERIFY(fader.model() == nullptr);
		fader.grab();  // must not read through the null
	}

	void aLiveFaderStillTakesWheelInput()
	{
		FloatModel model(1.f, 0.f, 2.f, 0.01f);
		Fader fader(&model, QStringLiteral("Live"), nullptr);
		const QPointF at{20., 100.};
		QWheelEvent wheel{at, at, QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
		QCoreApplication::sendEvent(&fader, &wheel);
		QVERIFY(model.value() > 1.f);  // +1 dB on a live model: the guards must not swallow normal input
	}

	void aFaderWhoseModelDiesIgnoresMousePressAndDrag()
	{
		auto* model = new FloatModel(1.f, 0.f, 2.f, 0.01f);
		Fader fader(model, QStringLiteral("Doomed"), nullptr);
		fader.resize(40, 200);
		delete model;
		QVERIFY(fader.model() == nullptr);
		// Press and drag read the freed model through calculateKnobPosYFromModel and setVolumeByLocalPixelValue.
		const QPointF at{20., 100.};
		QMouseEvent press{QEvent::MouseButtonPress, at, at, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
		QCoreApplication::sendEvent(&fader, &press);
		QMouseEvent move{QEvent::MouseMove, at, at, at, Qt::NoButton, Qt::LeftButton, Qt::NoModifier};
		QCoreApplication::sendEvent(&fader, &move);
		QMouseEvent release{QEvent::MouseButtonRelease, at, at, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
		QCoreApplication::sendEvent(&fader, &release);
		QVERIFY(fader.model() == nullptr);
	}

	void aFaderWhoseModelDiesIgnoresWheel()
	{
		auto* model = new FloatModel(1.f, 0.f, 2.f, 0.01f);
		Fader fader(model, QStringLiteral("Doomed"), nullptr);
		delete model;
		QVERIFY(fader.model() == nullptr);
		const QPointF at{20., 100.};
		QWheelEvent wheel{at, at, QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false};
		QCoreApplication::sendEvent(&fader, &wheel);
		QVERIFY(fader.model() == nullptr);
	}

	void aFaderWhoseModelDiesIgnoresKeyboardAdjust()
	{
		// MixerView's arrow keys reach a fader through adjust(), not through keyPressEvent.
		auto* model = new FloatModel(1.f, 0.f, 2.f, 0.01f);
		Fader fader(model, QStringLiteral("Doomed"), nullptr);
		delete model;
		fader.adjust(Qt::NoModifier, Fader::AdjustmentDirection::Up);
		fader.adjust(Qt::NoModifier, Fader::AdjustmentDirection::Down);
		QVERIFY(fader.model() == nullptr);
	}

	void aFaderWhoseModelDiesOpensNoContextMenu()
	{
		// The context menu's Reset and Copy entries read the model, and exec() would block the test.
		auto* model = new FloatModel(1.f, 0.f, 2.f, 0.01f);
		Fader fader(model, QStringLiteral("Doomed"), nullptr);
		delete model;
		QContextMenuEvent menu{QContextMenuEvent::Mouse, QPoint{20, 100}, QPoint{20, 100}};
		QCoreApplication::sendEvent(&fader, &menu);
		QVERIFY(fader.model() == nullptr);
	}

	void aFaderWhoseModelDiesIgnoresDoubleClick()
	{
		// The double-click opens the "Set value" dialog; with no model there must be no dialog and no read.
		auto* model = new FloatModel(1.f, 0.f, 2.f, 0.01f);
		Fader fader(model, QStringLiteral("Doomed"), nullptr);
		delete model;
		const QPointF at{20., 100.};
		QMouseEvent dblClick{QEvent::MouseButtonDblClick, at, at, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
		QCoreApplication::sendEvent(&fader, &dblClick);
		QVERIFY(fader.model() == nullptr);
	}

	void aFadeButtonKeepsItsSuppliedParentWithoutALayout()
	{
		QWidget parent;
		FadeButton button(&parent);

		QCOMPARE(button.parentWidget(), &parent);
	}

	void repeatedDoubleClicksReleaseColorDialogs()
	{
		LocklessRingBuffer<SampleFrame> inputBuffer{8};
		TestVectorView view{nullptr, &inputBuffer};

		for (int i = 0; i < 3; ++i)
		{
			bool dialogOpened = false;
			QTimer::singleShot(0, [&view, &dialogOpened]()
			{
				const auto dialogs = colorChoosers(view);
				dialogOpened = !dialogs.isEmpty();
				if (dialogOpened) { dialogs.constFirst()->reject(); }
			});
			view.triggerDoubleClick();
			QVERIFY(dialogOpened);
			QVERIFY(colorChoosers(view).isEmpty());
		}
	}

	void aFloatModelEditorWhoseModelDiesPaintsNothing()
	{
		FloatModelEditorBase editor;
		auto* model = new FloatModel(1.f, 0.f, 2.f, 0.01f);
		editor.setModel(model);
		QVERIFY(editor.isEnabled());
		editor.grab();
		delete model;
		QVERIFY(editor.model() == nullptr);
		QVERIFY(!editor.isEnabled());
		editor.grab();  // must not read through the null
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(VcaStripLifetimeTest)
#include "VcaStripLifetimeTest.moc"
