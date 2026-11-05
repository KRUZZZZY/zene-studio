/*
 * TrackGripContrastTest.cpp - the track movement grip is palette-driven and meets 3:1
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

/*! 0.5.0 Q050-442: the track movement handle, measured where a musician sees it.
 *
 *  The grip used to be a fixed 6 x 25 bitmap scaled down from a 24 px glyph, so the dots reached the
 *  row at a fraction of their colour's opacity and were outside every contrast check. The test renders
 *  the REAL row (a TrackView built by the real GuiApplication) and runs the in-project contrast helper
 *  over the grip's own pixels: every pixel that differs from the visible row surface behind it must
 *  meet WCAG 1.4.11's non-text minimum, 3:1 (a11y::meetsComponentContrast), in the standard dark
 *  palette, in the high-contrast palette, and after the foreground role changes.
 *
 *  The palette is latched when the interface starts (Appearance.h, DesignTokens.cpp), so the colours a
 *  start draws are tested by re-running this executable as a child that loads the config the previous
 *  start wrote - the real next-start path, not a test-only recolour. The GUI writes that config through
 *  the same registered commands an agent runs: Customise > Theme > Palette (window.appearance) and
 *  Customise > Theme > Colours... > (text) Change... (theme.set_token).
 *
 *  The handle's contrast is the palette role's contrast, so the test also holds the role itself to the
 *  surface it is measured on: a role that cannot reach 3:1 on the row would fail here rather than pass on
 *  an accident of compositing.
 */

#include <QtTest>

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QMenu>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QTemporaryDir>

#include "Appearance.h"
#include "CommandForm.h"
#include "CommandListDialog.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackGrip.h"
#include "TrackOperationsWidget.h"
#include "TrackView.h"
#include "WcagContrast.h"
#include "WorkspaceMenus.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! The environment a child sets to say it re-runs one slot after a restart.
constexpr char ChildVariable[] = "ZENE_TEST_GRIP_CHILD";
//! The config file the previous start saved; it is copied into the child's home before GuiApplication.
constexpr char ConfigVariable[] = "ZENE_TEST_GRIP_CONFIG";
//! What the child expects the next start to draw: "dark", "high_contrast", or a #rrggbb colour.
constexpr char ExpectedVariable[] = "ZENE_TEST_GRIP_EXPECT";

TrackView* trackViewOf(const Track* track)
{
	for (TrackView* view : getGUI()->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return view; }
	}
	return nullptr;
}

//! The row surface behind the grip: the most common colour in the grip's rect. The handle's dots are a
//! minority of the area, so the surface is what the row paints there - the colour a person actually
//! sees the handle on, whatever palette or stylesheet rule produced it.
QColor surfaceUnder(const QImage& row, const QRect& grip)
{
	QHash<QRgb, int> counts;
	int best = 0;
	QRgb surface = 0;
	for (int y = grip.top(); y <= grip.bottom(); ++y)
	{
		for (int x = grip.left(); x <= grip.right(); ++x)
		{
			const QRgb pixel = row.pixel(x, y);
			const int count = ++counts[pixel];
			if (count > best)
			{
				best = count;
				surface = pixel;
			}
		}
	}
	return QColor::fromRgba(surface);
}

struct ContrastReading
{
	QColor surface;
	qreal worst = 21.0;    //!< the lowest ratio any inked pixel has against the surface
	QPoint worstAt;
	int inked = 0;         //!< pixels that differ from the surface
	int roleColoured = 0;  //!< of those, pixels exactly the palette's text colour
};

/*! The GUI contrast check: read the grip's rendered area of the row and compare EVERY inked pixel with
 *  the surface it is drawn on. A handle made of translucent dots shows up here as a worst ratio under
 *  3:1 even when the palette's role colour itself would pass. */
ContrastReading measureGrip(const QImage& row, const QRect& grip, const QColor& role)
{
	ContrastReading reading;
	const QRect area = grip.intersected(row.rect());
	if (area.isEmpty()) { return reading; }
	reading.surface = surfaceUnder(row, area);
	for (int y = area.top(); y <= area.bottom(); ++y)
	{
		for (int x = area.left(); x <= area.right(); ++x)
		{
			const QColor pixel = row.pixelColor(x, y);
			if (pixel == reading.surface) { continue; }
			++reading.inked;
			if (pixel == role) { ++reading.roleColoured; }
			const qreal ratio = a11y::contrastRatio(pixel, reading.surface);
			if (ratio < reading.worst)
			{
				reading.worst = ratio;
				reading.worstAt = QPoint(x, y);
			}
		}
	}
	return reading;
}

QString describe(const ContrastReading& reading)
{
	return QStringLiteral("surface %1, worst %2:1 at %3,%4, %5 inked, %6 exactly the role colour")
		.arg(reading.surface.name()).arg(reading.worst, 0, 'f', 2).arg(reading.worstAt.x()).arg(reading.worstAt.y())
		.arg(reading.inked).arg(reading.roleColoured);
}

//! The grip's rect inside its row's rendered image.
QRect gripIn(TrackView* view, TrackGrip* grip)
{
	return QRect(grip->mapTo(view, QPoint(0, 0)), grip->size());
}

} // namespace

class TrackGripContrastTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		if (qEnvironmentVariableIsSet(ChildVariable))
		{
			// This process is the restart: install the config the previous start saved BEFORE
			// GuiApplication (and with it tokens::palette()) is made.
			const QString saved = qEnvironmentVariable(ConfigVariable);
			QVERIFY2(!saved.isEmpty() && QFile::copy(saved, m_home.filePath(QStringLiteral("zene.xml"))),
				qPrintable(QStringLiteral("could not install the saved config %1").arg(saved)));
		}
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Criterion 1, standard palette: the grip is visible on an ordinary track row.
	void theGripIsLegibleInTheStandardPalette()
	{
		// The dark palette is what a fresh start draws, so this slot needs no restart.
		QCOMPARE(tokens::palette().text, tokens::paletteNamed(QStringLiteral("dark")).text);
		verifyGrip(tokens::paletteNamed(QStringLiteral("dark")).text);
	}

	//! Criterion 1, high-contrast palette: chosen where a person chooses it (Customise > Theme >
	//! Palette), and drawn by the next start.
	void theGripIsLegibleInTheHighContrastPalette()
	{
		if (!qEnvironmentVariableIsSet(ChildVariable))
		{
			choosePaletteThroughTheThemeMenu(QStringLiteral("high_contrast"));
			QCOMPARE(appearance::options().palette, QStringLiteral("high_contrast"));
			// The one command both sides run says the running interface still draws the old colours.
			const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("window.appearance"));
			QVERIFY2(state.ok, qPrintable(state.errorMessage));
			QVERIFY(state.result.value(QStringLiteral("restart_pending")).toBool());
			runChild(QStringLiteral("high_contrast"));
			// The running interface keeps its palette; the next start drew the high-contrast one.
			QCOMPARE(tokens::palette().text, tokens::paletteNamed(QStringLiteral("dark")).text);
			QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok, "one undo step restores the palette");
			QCOMPARE(appearance::options().palette, QStringLiteral("dark"));
			return;
		}
		QCOMPARE(appearance::options().palette, QStringLiteral("high_contrast"));
		QCOMPARE(tokens::palette().text, tokens::paletteNamed(QStringLiteral("high_contrast")).text);
		QVERIFY(tokens::palette().base == QColor(Qt::black));
		verifyGrip(tokens::paletteNamed(QStringLiteral("high_contrast")).text);
	}

	//! Criterion 2: a changed foreground role reaches the grip. Made where a person makes it - the
	//! Theme Colours list's Change... form - which runs the same theme.set_token an agent runs.
	void aChangedForegroundRoleReachesTheGrip()
	{
		if (!qEnvironmentVariableIsSet(ChildVariable))
		{
			const QColor wanted(QStringLiteral("#ffcc00"));
			changeTextRoleThroughTheList(wanted);
			// Both sides read the one store: the agent's theme.get agrees with the list's write.
			const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("theme.get"));
			QVERIFY2(state.ok, qPrintable(state.errorMessage));
			QCOMPARE(state.result.value(QStringLiteral("overrides")).toObject().value(QStringLiteral("text")).toString(),
				wanted.name());
			QVERIFY(state.result.value(QStringLiteral("restart_pending")).toBool());
			runChild(wanted.name());
			QVERIFY2(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok, "one undo step drops the colour");
			QVERIFY(tokens::themeOverrides().isEmpty());
			return;
		}
		const QColor expected(qEnvironmentVariable(ExpectedVariable));
		QCOMPARE(tokens::palette().text, expected);
		verifyGrip(expected);
	}

	//! The handle still shows while it is grabbed (the inherited artwork had a second bitmap for that);
	//! the palette's role is unchanged, only the dots grow.
	void theGrabbedGripIsStillDrawnInThePalette()
	{
		TrackView* view = trackView();
		QVERIFY(view != nullptr);
		TrackGrip* grip = view->getTrackOperationsWidget()->getTrackGrip();
		QVERIFY(grip != nullptr);
		grip->show();
		QCoreApplication::processEvents();
		const QColor role = tokens::palette().text;

		const QImage released = renderRow(view);
		const ContrastReading before = measureGrip(released, gripIn(view, grip), role);
		QVERIFY2(before.inked > 0, "the grip paints no handle at all");
		QVERIFY2(a11y::meetsComponentContrast(role, before.surface), qPrintable(describe(before)));

		QTest::mousePress(grip, Qt::LeftButton, Qt::NoModifier, QPoint(grip->width() / 2, grip->height() / 2));
		QCoreApplication::processEvents();
		QCOMPARE(grip->cursor().shape(), Qt::ClosedHandCursor);
		const QImage grabbed = renderRow(view);
		const ContrastReading during = measureGrip(grabbed, gripIn(view, grip), role);
		QVERIFY2(during.inked > before.inked, qPrintable(QStringLiteral("grabbed %1 vs released %2 inked pixels")
			.arg(during.inked).arg(before.inked)));
		QVERIFY2(a11y::meetsComponentContrast(role, during.surface), qPrintable(describe(during)));
		QVERIFY2(during.worst >= 3.0, qPrintable(describe(during)));
		QVERIFY2(during.roleColoured >= before.roleColoured,
			qPrintable(QStringLiteral("the grabbed handle lost the palette colour: %1 vs %2")
				.arg(during.roleColoured).arg(before.roleColoured)));

		QTest::mouseRelease(grip, Qt::LeftButton, Qt::NoModifier, QPoint(grip->width() / 2, grip->height() / 2));
		QCoreApplication::processEvents();
		QCOMPARE(grip->cursor().shape(), Qt::OpenHandCursor);
		const QImage again = renderRow(view);
		QCOMPARE(again, released);
	}

private:
	//! Choose @a palette where a person chooses it: Project tools > Customise > Theme > Palette.
	//! Every entry declares window.appearance, the command the agent surface runs.
	void choosePaletteThroughTheThemeMenu(const QString& palette)
	{
		QMenu customise;
		populateCustomiseExtras(&customise);
		QMenu* theme = nullptr;
		for (QMenu* menu : customise.findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (menu->title() == QStringLiteral("Theme")) { theme = menu; }
		}
		QVERIFY2(theme != nullptr, "Customise > Theme is missing");
		QMenu* paletteMenu = nullptr;
		for (QMenu* menu : theme->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (menu->title() == QStringLiteral("Palette")) { paletteMenu = menu; }
		}
		QVERIFY2(paletteMenu != nullptr, "Theme > Palette is missing");
		QAction* wanted = nullptr;
		for (QAction* action : paletteMenu->actions())
		{
			QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("window.appearance"));
			if (action->text() == palette) { wanted = action; }
		}
		QVERIFY2(wanted != nullptr, qPrintable(QStringLiteral("Theme > Palette has no %1 entry").arg(palette)));
		wanted->trigger();
	}

	//! The row of the Theme Colours list that names the `text` role, or -1.
	static int textRow(const CommandListDialog& dialog)
	{
		for (int i = 0; i < dialog.rowCount(); ++i)
		{
			if (dialog.row(i).value(QStringLiteral("name")).toString() == QLatin1String("text")) { return i; }
		}
		return -1;
	}

	//! The visible command form @a waitFor opened, or null: a form action replies Busy and shows one.
	QPointer<CommandForm> awaitForm()
	{
		for (int turn = 0; turn < 300; ++turn)
		{
			for (QWidget* widget : QApplication::topLevelWidgets())
			{
				auto* candidate = qobject_cast<CommandForm*>(widget);
				if (candidate != nullptr && candidate->isVisible()) { return candidate; }
			}
			QTest::qWait(10);
		}
		return nullptr;
	}

	//! The form's Run button, or null; the label is matched without its mnemonic.
	static QPushButton* runButton(CommandForm* form)
	{
		for (QPushButton* button : form->findChildren<QPushButton*>())
		{
			if (button->text().remove(QLatin1Char('&')) == QStringLiteral("Run")) { return button; }
		}
		return nullptr;
	}

	//! The visible Theme Colours list (the shared CommandListDialog), or null.
	static QPointer<CommandListDialog> awaitThemeColoursList()
	{
		for (int turn = 0; turn < 100; ++turn)
		{
			for (QWidget* widget : QApplication::topLevelWidgets())
			{
				auto* candidate = qobject_cast<CommandListDialog*>(widget);
				if (candidate != nullptr && candidate->isVisible()) { return candidate; }
			}
			QTest::qWait(10);
		}
		return nullptr;
	}

	//! Change the `text` role the way a person does: Customise > Theme > Colours..., the text row's
	//! Change... form, a colour typed in, Run. Every step is the registered command (SPEC A11), so the
	//! menu, the list and the form cannot drift from what an agent runs.
	void changeTextRoleThroughTheList(const QColor& colour)
	{
		QMenu customise;
		populateCustomiseExtras(&customise);
		QMenu* theme = nullptr;
		for (QMenu* menu : customise.findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (menu->title() == QStringLiteral("Theme")) { theme = menu; }
		}
		QVERIFY2(theme != nullptr, "Customise > Theme is missing");
		QAction* colours = nullptr;
		for (QAction* action : theme->actions())
		{
			if (action->text() == QStringLiteral("Colours...")) { colours = action; }
		}
		QVERIFY2(colours != nullptr, "Theme > Colours... is missing");
		QCOMPARE(colours->property("controlCommand").toString(), QStringLiteral("theme.get"));
		colours->trigger();
		const QPointer<CommandListDialog> dialog = awaitThemeColoursList();
		QVERIFY2(dialog != nullptr, "Customise > Theme > Colours... opened no colour list");
		const int row = textRow(*dialog);
		QVERIFY2(row >= 0, "the Theme Colours list has no text row");
		const ControlResult asked = dialog->runAction(row, 0);  // "Change..."
		QCOMPARE(asked.errorKind, ControlErrorKind::Busy);      // the form is open
		const QPointer<CommandForm> form = awaitForm();
		QVERIFY2(form != nullptr, "Change... opened no command form");
		QVERIFY2(form->setField(QStringLiteral("color"), colour.name()), "the form has no color field");
		QPushButton* run = runButton(form);
		QVERIFY(run != nullptr);
		QTest::mouseClick(run, Qt::LeftButton);
		// A refusal keeps the form open with the reason; a success closes it and writes the role.
		QVERIFY2(!form || form->result().ok, form ? qPrintable(form->errorText()) : "the form closed without a result");
		QCOMPARE(tokens::themeOverrides().value(QStringLiteral("text")).toString(), colour.name());
	}

	//! The real track view of a freshly created sample track, or null when the editor has none.
	TrackView* trackView()
	{
		if (m_track == nullptr)
		{
			m_track = Track::create(Track::Type::Sample, Engine::getSong());
			// The view is built on the next turn (TrackContainerView::createTrackView is queued).
			for (int turn = 0; turn < 100 && trackViewOf(m_track) == nullptr; ++turn) { QTest::qWait(10); }
		}
		TrackView* view = trackViewOf(m_track);
		if (view == nullptr) { return nullptr; }
		view->show();
		QCoreApplication::processEvents();
		return view;
	}

	//! The whole row as a musician sees it, the grip included.
	QImage renderRow(TrackView* view)
	{
		QCoreApplication::processEvents();
		return view->grab().toImage().convertToFormat(QImage::Format_ARGB32);
	}

	//! The GUI contrast check the acceptance criterion asks for: render the real grip in the running
	//! palette and hold every pixel that differs from the row surface to WCAG 1.4.11's 3:1.
	void verifyGrip(const QColor& role)
	{
		TrackView* view = trackView();
		QVERIFY(view != nullptr);
		TrackGrip* grip = view->getTrackOperationsWidget()->getTrackGrip();
		QVERIFY(grip != nullptr && grip->width() > 0 && grip->height() > 0);
		grip->show();
		QCoreApplication::processEvents();
		const QImage row = renderRow(view);
		const ContrastReading reading = measureGrip(row, gripIn(view, grip), role);
		qInfo().noquote() << QStringLiteral("GRIP CONTRAST %1: %2").arg(role.name(), describe(reading));
		QVERIFY2(reading.inked > 0, qPrintable(QStringLiteral("the grip paints no handle: %1").arg(describe(reading))));
		QVERIFY2(reading.worst >= 3.0, qPrintable(describe(reading)));
		QVERIFY2(a11y::meetsComponentContrast(role, reading.surface), qPrintable(describe(reading)));
		// The handle is the role's own colour, not a fixed pixmap that happens to be near it: the dots
		// are drawn at full opacity from tokens::palette().text.
		QVERIFY2(reading.roleColoured >= 20,
			qPrintable(QStringLiteral("only %1 pixels are exactly %2: %3")
				.arg(reading.roleColoured).arg(role.name(), describe(reading))));
		// The handle keeps the inherited 10 px column, so the row's buttons stay where they were.
		QCOMPARE(grip->width(), 10);
	}

	//! Re-run this executable as the next start with the saved config; the child enters the same slot
	//! and makes the assertions there.
	void runChild(const QString& expected)
	{
		ConfigManager::inst()->saveConfigFile();  // the config the next start reads is the one written now
		const QString config = m_home.filePath(QStringLiteral("zene.xml"));
		QVERIFY2(QFile::exists(config), qPrintable(config));

		QProcess child;
		QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
		environment.insert(QString::fromLatin1(ChildVariable), QStringLiteral("1"));
		environment.insert(QString::fromLatin1(ConfigVariable), config);
		environment.insert(QString::fromLatin1(ExpectedVariable), expected);
		environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		child.setProcessEnvironment(environment);
		child.setProcessChannelMode(QProcess::MergedChannels);
		const QString slot = QLatin1String(QTest::currentTestFunction());
		child.start(QCoreApplication::applicationFilePath(), {slot});
		QVERIFY2(child.waitForStarted(), qPrintable(child.errorString()));
		QVERIFY2(child.waitForFinished(180000), "the restarted instance did not finish");
		const QByteArray output = child.readAll();
		// The child's measurement belongs in this run's log too: the numbers a reader checks are the
		// ones the restarted instance measured, and its own log is not kept anywhere.
		for (const QByteArray& line : output.split('\n'))
		{
			const QByteArray trimmed = line.trimmed();
			if (trimmed.startsWith("QINFO") || trimmed.startsWith("PASS") || trimmed.startsWith("FAIL")) { qInfo().noquote() << trimmed; }
		}
		QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
			qPrintable(QString::fromLocal8Bit(output)));
		// The child ran the one slot (plus its class's setup and teardown) and nothing failed.
		QVERIFY2(QString::fromLocal8Bit(output).contains(QStringLiteral("PASS   : TrackGripContrastTest::%1()").arg(slot)),
			qPrintable(QString::fromLocal8Bit(output)));
		QVERIFY2(QString::fromLocal8Bit(output).contains(QStringLiteral("Totals: 3 passed, 0 failed")),
			qPrintable(QString::fromLocal8Bit(output)));
	}

	QTemporaryDir m_home;
	Track* m_track = nullptr;
};

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
	QApplication app(argc, argv);
	TrackGripContrastTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "TrackGripContrastTest.moc"
