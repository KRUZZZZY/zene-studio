/*
 * XyPadPairTest.cpp - W1: an XY pad drag is one undo step, and a refused move is shown
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

/*! The pad over a real channel rack with two macros: a drag of several moves sets both macros and ONE control.undo puts
 *  both back (it used to be one step per axis per move). An open pad follows the rack's macro list as it really changes:
 *  a macro added through rack.macro_add (what Rack > Add macro... runs) makes a pad opened on an empty rack usable
 *  without reopening; a macro removed by another client is followed - the axes re-select by NAME, so a shifted
 *  macro-<n> id does not silently point at a different macro - and when the last macro goes the pad is disabled again
 *  and says so on the next move, with changed() fired so an open axis list drops its now-empty rows. */

#include <QtTest>

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QSignalSpy>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Mixer.h"
#include "XyPad.h"
#include "../../../plugins/Delay/DelayControlsDialog.h"

#define PLUGIN_NAME delay
#include "../../../plugins/Delay/DelayControlsDialog.cpp"
#undef PLUGIN_NAME
#include "moc_DelayControlsDialog.cpp"
#include "moc_EqFader.cpp"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

double macroValue(const QString& channel, const QString& macro)
{
	for (const QJsonValue& entry : run(QStringLiteral("rack.get_state"), {{QStringLiteral("channel"), channel}})
		.result.value(QStringLiteral("macros")).toArray())
	{
		if (entry.toObject().value(QStringLiteral("macro")).toString() == macro) { return entry.toObject().value(QStringLiteral("value")).toDouble(); }
	}
	return -1.0;
}

//! The sub-pixel centre of the dot along one axis, from the antialiasing coverage of every pixel
//! (the dot is the accent painted over the pad's control fill, so each pixel's distance from the
//! accent IS its coverage). Sub-pixel, because a whole-pixel box cannot see a one-pixel offset.
double dotCentreAlong(XyPadArea& area, Qt::Orientation axis)
{
	QPixmap shot(area.size());
	shot.fill(Qt::transparent);
	area.render(&shot);
	const QImage image = shot.toImage().convertToFormat(QImage::Format_RGB32);
	const QColor accent = tokens::palette().accent;
	const QColor base = tokens::palette().control;
	const QColor cross = tokens::palette().separator;
	const auto channel = [](const QColor& colour, int c) {
		return c == 0 ? colour.red() : (c == 1 ? colour.green() : colour.blue());
	};
	//! The pad paints a flat control fill, a flat separator cross and the accent dot on top, so a
	//! pixel that is still one of the two flat colours carries no dot and no weight at all.
	const auto isBackground = [channel, &base, &cross](const QColor& pixel) {
		return (std::abs(pixel.red() - base.red()) < 2 && std::abs(pixel.green() - base.green()) < 2
				   && std::abs(pixel.blue() - base.blue()) < 2)
			|| (std::abs(pixel.red() - cross.red()) < 2 && std::abs(pixel.green() - cross.green()) < 2
				&& std::abs(pixel.blue() - cross.blue()) < 2);
	};
	const auto coverage = [channel, &base](const QColor& pixel, const QColor& over) {
		double sum = 0.0;
		for (int c = 0; c < 3; ++c)
		{
			const double span = double(channel(over, c)) - double(channel(base, c));
			if (std::abs(span) < 1.0) { continue; }  // no contrast on this channel: it says nothing
			sum += std::clamp((double(channel(pixel, c)) - double(channel(base, c))) / span, 0.0, 1.0);
		}
		return sum / 3.0;
	};
	double weight = 0.0, moment = 0.0;
	for (int y = 0; y < image.height(); ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			const QColor pixel = image.pixelColor(x, y);
			if (isBackground(pixel)) { continue; }
			const double w = coverage(pixel, accent);
			if (w <= 0.0) { continue; }
			weight += w;
			moment += w * (axis == Qt::Horizontal ? x + 0.5 : y + 0.5);
		}
	}
	return weight > 0.0 ? moment / weight : -1.0;
}

QLabel* statusOf(XyPadDialog& dialog)
{
	for (QLabel* label : dialog.findChildren<QLabel*>())
	{
		if (label->accessibleName() == QLatin1String("XY pad status")) { return label; }
	}
	return nullptr;
}

struct DelayXyPadFixture
{
	FloatModel xModel{0.2f, 0.f, 1.f, 0.001f};
	FloatModel yModel{0.3f, 0.f, 1.f, 0.001f};
	XyPad pad{nullptr, &xModel, &yModel};

	DelayXyPadFixture()
	{
		pad.resize(100, 100);
		pad.show();
		QCoreApplication::processEvents();
	}
};

} // namespace

class XyPadPairTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		while (Engine::mixer()->numChannels() <= 1) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void delayXyPadIgnoresRightDrag()
	{
		DelayXyPadFixture fixture;
		QTest::mousePress(&fixture.pad, Qt::RightButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseMove(&fixture.pad, QPoint(80, 70));
		QTest::mouseRelease(&fixture.pad, Qt::RightButton, Qt::NoModifier, QPoint(80, 70));
		QCOMPARE(fixture.xModel.value(), 0.2f);
		QCOMPARE(fixture.yModel.value(), 0.3f);
	}

	void delayXyPadIgnoresMiddleDrag()
	{
		DelayXyPadFixture fixture;
		QTest::mousePress(&fixture.pad, Qt::MiddleButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseMove(&fixture.pad, QPoint(80, 70));
		QTest::mouseRelease(&fixture.pad, Qt::MiddleButton, Qt::NoModifier, QPoint(80, 70));
		QCOMPARE(fixture.xModel.value(), 0.2f);
		QCOMPARE(fixture.yModel.value(), 0.3f);
	}

	void delayXyPadLeftDragUpdatesBothModels()
	{
		DelayXyPadFixture fixture;
		QTest::mousePress(&fixture.pad, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseMove(&fixture.pad, QPoint(80, 70));
		QCOMPARE(fixture.xModel.value(), 0.8f);
		QCOMPARE(fixture.yModel.value(), 0.7f);
		QTest::mouseRelease(&fixture.pad, Qt::LeftButton, Qt::NoModifier, QPoint(80, 70));
	}

	void delayXyPadIgnoresOtherButtonReleaseDuringLeftDrag()
	{
		DelayXyPadFixture fixture;
		QTest::mousePress(&fixture.pad, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mousePress(&fixture.pad, Qt::RightButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseRelease(&fixture.pad, Qt::RightButton, Qt::NoModifier, QPoint(10, 10));
		QTest::mouseMove(&fixture.pad, QPoint(80, 70));
		QCOMPARE(fixture.xModel.value(), 0.8f);
		QCOMPARE(fixture.yModel.value(), 0.7f);
		QTest::mouseRelease(&fixture.pad, Qt::LeftButton, Qt::NoModifier, QPoint(80, 70));
	}

	void aDragIsOneStepAndARefusalIsShown()
	{
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(1));
		for (const char* name : {"Cutoff", "Drive"})
		{
			QVERIFY(run(QStringLiteral("rack.macro_add"), {{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QLatin1String(name)}}).ok);
		}
		QVERIFY(run(QStringLiteral("rack.macro_set"), {{QStringLiteral("channel"), channel},
			{QStringLiteral("macro"), QStringLiteral("macro-0")}, {QStringLiteral("value"), 0.4}}).ok);
		const double x0 = macroValue(channel, QStringLiteral("macro-0"));
		const double y0 = macroValue(channel, QStringLiteral("macro-1"));
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 60000}}).ok);

		XyPadDialog dialog(channel);
		auto* area = dialog.findChild<XyPadArea*>();
		QLabel* status = statusOf(dialog);
		QVERIFY(area != nullptr && area->moved && status != nullptr);
		QCOMPARE(area->property("controlCommand").toString(), QStringLiteral("rack.macro_set_pair"));
		for (const QPointF point : {QPointF(0.2, 0.8), QPointF(0.25, 0.85), QPointF(0.3, 0.9)}) { area->moved(point); }
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-0")) - 0.3) < 1e-4);
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-1")) - 0.9) < 1e-4);
		QVERIFY(status->text().isEmpty());

		const ControlResult undo = run(QStringLiteral("control.undo"));  // ONE undo: the whole drag, both axes
		QVERIFY2(undo.ok, qPrintable(undo.errorMessage));
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-0")) - x0) < 1e-4);
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-1")) - y0) < 1e-4);
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), control::UndoCoalesceWindowMs}}).ok);

		// When both axes select one macro, an Up key must not leave the dot away from that macro's value.
		const auto axes = dialog.findChildren<QComboBox*>();
		QCOMPARE(axes.size(), 2);
		axes.at(1)->setCurrentIndex(0);
		QCOMPARE(dialog.macroId(false), QStringLiteral("macro-0"));
		QCOMPARE(dialog.macroId(true), QStringLiteral("macro-0"));
		QTest::keyClick(area, Qt::Key_Up);
		QVERIFY(std::abs(area->point().y() - macroValue(channel, QStringLiteral("macro-0"))) < 1e-4);
		axes.at(1)->setCurrentIndex(1);

		// Another client removes the Y macro while the pad is open: the pad follows the change - Y re-selects by
		// name onto the one macro left - so the next move drives that macro instead of a stale macro-<n> id.
		QVERIFY(run(QStringLiteral("rack.macro_remove"), {{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-1")}}).ok);
		QCOMPARE(axes.at(0)->currentText(), QStringLiteral("Cutoff"));
		QCOMPARE(axes.at(1)->currentText(), QStringLiteral("Cutoff"));
		area->moved(QPointF(0.7, 0.7));
		QVERIFY(status->text().isEmpty());
		QVERIFY(std::abs(macroValue(channel, QStringLiteral("macro-0")) - 0.7) < 1e-4);

		// When the LAST macro goes, the pad has nothing left to drive: the next move is refused whole, and says so.
		QVERIFY(run(QStringLiteral("rack.macro_remove"), {{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-0")}}).ok);
		QVERIFY(!area->isEnabled());
		QCOMPARE(axes.at(0)->count(), 0);
		QCOMPARE(axes.at(1)->count(), 0);
		area->moved(QPointF(0.2, 0.2));
		QVERIFY2(status->text().startsWith(QStringLiteral("Not set")), qPrintable(status->text()));
	}

	//! Cluster 5481: the dot is drawn at point * width() / height(), while the pointer maps a
	//! position over width() - 1 / height() - 1, so at a range endpoint the dot's centre sits a
	//! whole pixel outside the widget and half of the circle is clipped away. The dot at one end
	//! must be the mirror image of the dot at the other end about the widget's centre.
	void theDotIsNotClippedAtTheRangeEndpoints()
	{
		XyPadArea area;
		area.resize(160, 160);
		const auto centreAt = [&area](QPointF point, Qt::Orientation axis) {
			area.setPoint(point);
			return dotCentreAlong(area, axis);
		};
		// The two ends of X, at mid height: the same dot, mirrored about the middle of the widget.
		const double leftX = centreAt(QPointF(0.0, 0.5), Qt::Horizontal);
		const double rightX = centreAt(QPointF(1.0, 0.5), Qt::Horizontal);
		QVERIFY2(leftX > 0.0 && rightX > 0.0, "the pad painted no dot at an X endpoint");
		QVERIFY2(std::abs(leftX + rightX - (area.width() - 1)) < 0.6,
			qPrintable(QStringLiteral("the dot at X=0 is centred at %1 and the dot at X=1 at %2; mirrored about the "
				"widget they would sum to %3 - one end's dot is clipped")
				.arg(leftX, 0, 'f', 2).arg(rightX, 0, 'f', 2).arg(area.width() - 1)));
		// The two ends of Y, at mid width.
		const double bottomY = centreAt(QPointF(0.5, 0.0), Qt::Vertical);
		const double topY = centreAt(QPointF(0.5, 1.0), Qt::Vertical);
		QVERIFY2(bottomY > 0.0 && topY > 0.0, "the pad painted no dot at a Y endpoint");
		QVERIFY2(std::abs(bottomY + topY - (area.height() - 1)) < 0.6,
			qPrintable(QStringLiteral("the dot at Y=0 is centred at %1 and the dot at Y=1 at %2; mirrored about the "
				"widget they would sum to %3 - one end's dot is clipped")
				.arg(bottomY, 0, 'f', 2).arg(topY, 0, 'f', 2).arg(area.height() - 1)));
		// And a mid-range dot is where it always was (the fix must not move the interior).
		const double middleX = centreAt(QPointF(0.5, 0.5), Qt::Horizontal);
		const double middleY = centreAt(QPointF(0.5, 0.5), Qt::Vertical);
		QVERIFY(std::abs(middleX - area.width() / 2.0) < 0.6);
		QVERIFY(std::abs(middleY - area.height() / 2.0) < 0.6);
	}

	//! The pad's accessors tell a list or a reader where it stands, and refresh() follows an edit made elsewhere.
	void thePadFollowsTheMacrosItIsOverAndSaysWhereItStands()
	{
		while (Engine::mixer()->numChannels() <= 2) { Engine::mixer()->createChannel(); }
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(2));
		for (const char* name : {"Width", "Colour"})
		{
			QVERIFY(run(QStringLiteral("rack.macro_add"), {{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QLatin1String(name)}}).ok);
		}
		QVERIFY(run(QStringLiteral("rack.macro_set"), {{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-0")},
			{QStringLiteral("value"), 0.25}}).ok);
		XyPadDialog dialog(channel);
		QCOMPARE(dialog.channel(), channel);
		QCOMPARE(dialog.macroId(false), QStringLiteral("macro-0"));
		QCOMPARE(dialog.macroId(true), QStringLiteral("macro-1"));
		QCOMPARE(dialog.macroName(false), QStringLiteral("Width"));
		QCOMPARE(dialog.macroName(true), QStringLiteral("Colour"));
		XyPadArea* area = dialog.area();
		QVERIFY(area != nullptr);
		QCOMPARE(area->sizeHint(), QSize(220, 220));
		QVERIFY(std::abs(area->point().x() - 0.25) < 1e-4);  // opened at the macros' own values

		// An edit made elsewhere is shown once the pad is told to look again.
		QVERIFY(run(QStringLiteral("rack.macro_set"), {{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-1")},
			{QStringLiteral("value"), 0.75}}).ok);
		dialog.refresh();
		QVERIFY(std::abs(area->point().y() - 0.75) < 1e-4);
		QVERIFY(std::abs(area->point().x() - 0.25) < 1e-4);

		// The pad reads out both axes by name, and moving it reports the new place.
		area->setAxisNames(QStringLiteral("Width"), QStringLiteral("Colour"));
		QCOMPARE(area->accessibleValue(), QStringLiteral("X Width 25 %, Y Colour 75 %"));
		QPointF reported;
		area->moved = [&reported](QPointF point) { reported = point; };
		area->setPoint(QPointF(0.5, 0.5));
		QTest::keyClick(area, Qt::Key_Right);
		QVERIFY2(std::abs(reported.x() - 0.51) < 1e-6, "a key press did not report the new place");
	}
	//! A pad opened before the rack has any macros becomes usable once one is added, without reopening and without
	//! anyone telling the pad: Rack > Add macro... runs rack.macro_add, and the open pad follows that path.
	void thePadBecomesUsableWhenAMacroIsAddedAfterOpening()
	{
		while (Engine::mixer()->numChannels() <= 5) { Engine::mixer()->createChannel(); }
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(5));
		XyPadDialog dialog(channel);
		auto* area = dialog.area();
		const auto combos = dialog.findChildren<QComboBox*>();
		QCOMPARE(combos.size(), 2);
		QVERIFY(!area->isEnabled());
		QCOMPARE(combos.at(0)->count(), 0);
		QCOMPARE(combos.at(1)->count(), 0);

		QVERIFY(run(QStringLiteral("rack.macro_add"), {{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QStringLiteral("Tone")}}).ok);
		QVERIFY2(area->isEnabled(), "the pad stayed disabled after a macro was added through rack.macro_add");
		QCOMPARE(combos.at(0)->count(), 1);
		QCOMPARE(combos.at(1)->count(), 1);
		QCOMPARE(dialog.macroId(false), QStringLiteral("macro-0"));
		QCOMPARE(dialog.macroId(true), QStringLiteral("macro-0"));
	}

	//! Removing an EARLIER macro shifts the macro-<n> ids of every later one (RackMacros::removeMacro erases its
	//! vector entry): the axes must keep following the macros they had chosen - by name - not whichever macro keeps
	//! the old id.
	void thePadKeepsItsChosenMacrosWhenAnEarlierOneIsRemoved()
	{
		while (Engine::mixer()->numChannels() <= 6) { Engine::mixer()->createChannel(); }
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(6));
		for (const char* name : {"Alpha", "Beta", "Gamma"})
		{
			QVERIFY(run(QStringLiteral("rack.macro_add"), {{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QLatin1String(name)}}).ok);
		}
		XyPadDialog dialog(channel);
		const auto combos = dialog.findChildren<QComboBox*>();
		QCOMPARE(combos.size(), 2);
		combos.at(0)->setCurrentIndex(2);  // X drives Gamma (macro-2)
		combos.at(1)->setCurrentIndex(1);  // Y drives Beta (macro-1)
		QCOMPARE(dialog.macroId(false), QStringLiteral("macro-2"));
		QCOMPARE(dialog.macroName(true), QStringLiteral("Beta"));

		QVERIFY(run(QStringLiteral("rack.macro_remove"), {{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-0")}}).ok);
		QCOMPARE(dialog.macroName(false), QStringLiteral("Gamma"));  // still Gamma, now macro-1 - not Beta at macro-1
		QCOMPARE(dialog.macroId(false), QStringLiteral("macro-1"));
		QCOMPARE(dialog.macroName(true), QStringLiteral("Beta"));  // still Beta, now macro-0
		QCOMPARE(dialog.macroId(true), QStringLiteral("macro-0"));
	}

	//! When the LAST macro is removed while the pad is open, the pad goes back to disabled with empty axis lists and
	//! still emits changed(), so an open axis list reloads its now-empty rows instead of showing ghosts.
	void thePadSaysChangedWhenTheLastMacroIsRemoved()
	{
		while (Engine::mixer()->numChannels() <= 7) { Engine::mixer()->createChannel(); }
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(7));
		QVERIFY(run(QStringLiteral("rack.macro_add"), {{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QStringLiteral("Solo")}}).ok);
		XyPadDialog dialog(channel);
		auto* area = dialog.area();
		const auto combos = dialog.findChildren<QComboBox*>();
		QVERIFY(area->isEnabled());
		QCOMPARE(combos.at(0)->count(), 1);
		QSignalSpy changed(&dialog, &XyPadDialog::changed);
		QVERIFY(changed.isValid());

		QVERIFY(run(QStringLiteral("rack.macro_remove"), {{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), QStringLiteral("macro-0")}}).ok);
		QVERIFY(!area->isEnabled());
		QCOMPARE(combos.at(0)->count(), 0);
		QCOMPARE(combos.at(1)->count(), 0);
		QCOMPARE(changed.count(), 1);  // the open list is told to drop its rows
	}

};

QTEST_MAIN(XyPadPairTest)
#include "XyPadPairTest.moc"
