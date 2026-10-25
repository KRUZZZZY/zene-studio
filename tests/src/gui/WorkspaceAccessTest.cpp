/*
 * WorkspaceAccessTest.cpp - the 0.5.0 W3 L13 / L14 interface commands
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
/*! 0.5.0 W3 L13 / L14, the interface side: the light palette holds every declared pair at AA (WF-118);
 *  a theme colour override is one undo step, names the pairs it breaks and round-trips through a file,
 *  and the Theme Colours list runs it (WF-117); accessible mode scales the stylesheet's metrics and is a
 *  window.appearance field that the old density values still set (WF-134, 0.5.0 themes 17/23); Help > Ask shows app.ask's answers (WF-142), What's New the shipped notes
 *  (WF-141), the large clock the play position (WF-101), the tuner listens only while open (WF-147); the
 *  accessibility walk names nameless controls from what they carry (WF-129) and reports controls with no role or
 *  no value (theme 17); the cross-hair is a preference with
 *  one undo step and follows it on the arranger (WF-109); the window verbs refuse, typed, without a main window. */

#include <QtTest>

#include <cmath>
#include <limits>

#include <QJsonArray>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QToolButton>

#include "AccessibilityAudit.h"
#include "Appearance.h"
#include "ArrangerAids.h"
#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Knob.h"
#include "UiConfigStore.h"
#include "WcagContrast.h"
#include "WorkspaceWindows.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class WorkspaceAccessTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("HOME", m_home.path().toLocal8Bit());
		qputenv("ZENE_UI_SCALE_FILE", m_home.filePath(QStringLiteral("scale")).toLocal8Bit());
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theLightPaletteIsAA()
	{
		QVERIFY(appearance::paletteNames().contains(QStringLiteral("light")));
		const tokens::Palette light = tokens::paletteNamed(QStringLiteral("light"));
		for (const tokens::ContrastPair& pair : tokens::contrastPairs(light))
		{
			const double ratio = a11y::contrastRatio(pair.foreground, pair.background);
			QVERIFY2(ratio >= pair.minimum, qPrintable(QStringLiteral("%1: %2:1, needs %3:1")
				.arg(QLatin1String(pair.name)).arg(ratio, 0, 'f', 2).arg(pair.minimum)));
		}
		// Light means light: the panel is lighter than its text, the reverse of the dark palette.
		QVERIFY(a11y::relativeLuminance(light.panel) > a11y::relativeLuminance(light.text));
		QVERIFY(tokens::paletteNamed(QStringLiteral("dark")).panel != light.panel);
	}

	void aColourOverrideIsOneUndoStep()
	{
		const ControlResult set = run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("accent")},
			{QStringLiteral("color"), QStringLiteral("#ff8800")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(tokens::themeOverrides().value(QStringLiteral("accent")).toString(), QStringLiteral("#ff8800"));
		bool found = false;
		for (const QJsonValue& entry : set.result.value(QStringLiteral("colors")).toArray())
		{
			const QJsonObject role = entry.toObject();
			if (role.value(QStringLiteral("name")).toString() != QLatin1String("accent")) { continue; }
			found = true;
			QCOMPARE(role.value(QStringLiteral("color")).toString(), QStringLiteral("#ff8800"));
			QVERIFY(role.value(QStringLiteral("overridden")).toBool());
		}
		QVERIFY(found);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	void aBadColourIsRefused()
	{
		const ControlResult bad = run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("accent")},
			{QStringLiteral("color"), QStringLiteral("not-a-colour")}});
		QVERIFY(!bad.ok);
		QCOMPARE(bad.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(!run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("noSuchRole")},
			{QStringLiteral("color"), QStringLiteral("#000000")}}).ok);
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	//! 0.5.0 theme 20: a colour that makes text unreadable is refused, naming the pairs, unless the user insists - then it
	//! is kept and still named. Nothing is written by the refusal.
	void anUnreadableThemeIsRefusedUnlessAllowed()
	{
		const QString panel = tokens::paletteNamed(appearance::options().palette).panel.name();
		const QJsonObject args{{QStringLiteral("name"), QStringLiteral("text")}, {QStringLiteral("color"), panel}};
		const ControlResult refused = run(QStringLiteral("theme.set_token"), args);
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("text on panel")), qPrintable(refused.errorMessage));
		QVERIFY(refused.errorMessage.contains(QStringLiteral("allow_low_contrast")));
		QVERIFY(tokens::themeOverrides().isEmpty());

		QJsonObject insist = args;
		insist.insert(QStringLiteral("allow_low_contrast"), true);
		const ControlResult set = run(QStringLiteral("theme.set_token"), insist);
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QVERIFY(!set.result.value(QStringLiteral("contrast_failures")).toArray().isEmpty());
		QVERIFY(!set.result.value(QStringLiteral("new_contrast_failures")).toArray().isEmpty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("theme.get")).result.value(QStringLiteral("contrast_failures")).toArray().isEmpty());
	}

	//! A pair that already fails does not block an unrelated change, and an import is held to the same check.
	void onlyANewFailureBlocks()
	{
		const QString panel = tokens::paletteNamed(appearance::options().palette).panel.name();
		QVERIFY(run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("text")}, {QStringLiteral("color"), panel},
			{QStringLiteral("allow_low_contrast"), true}}).ok);
		const ControlResult unrelated = run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("playhead")},
			{QStringLiteral("color"), QStringLiteral("#ffffff")}});
		QVERIFY2(unrelated.ok, qPrintable(unrelated.errorMessage));
		QVERIFY(unrelated.result.value(QStringLiteral("new_contrast_failures")).toArray().isEmpty());
		QVERIFY(!run(QStringLiteral("theme.import"), {{QStringLiteral("colors"), QJsonObject{{QStringLiteral("focusRing"), panel}}}}).ok);
		for (int step = 0; step < 2; ++step) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	//! Every theme reply says when it takes effect, and whether the running interface still draws something else.
	void everyThemeReplySaysWhenItApplies()
	{
		for (const ControlResult& reply : {run(QStringLiteral("theme.get")), run(QStringLiteral("window.appearance"), QJsonObject{})})
		{
			QVERIFY2(reply.ok, qPrintable(reply.errorMessage));
			QCOMPARE(reply.result.value(QStringLiteral("applies")).toString(), QStringLiteral("next_start"));
			QVERIFY(reply.result.value(QStringLiteral("restart_required")).toBool());
			QVERIFY(!reply.result.value(QStringLiteral("applies_note")).toString().isEmpty());
			QVERIFY(reply.result.contains(QStringLiteral("restart_pending")));
		}
		QVERIFY(!run(QStringLiteral("theme.get")).result.value(QStringLiteral("restart_pending")).toBool());
		const ControlResult set = run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("playhead")},
			{QStringLiteral("color"), QStringLiteral("#ffffff")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QVERIFY(set.result.value(QStringLiteral("restart_pending")).toBool());  // drawn from the next start only
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

	void aThemeRoundTripsThroughAFile()
	{
		QVERIFY(run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("panel")},
			{QStringLiteral("color"), QStringLiteral("#202830")}}).ok);
		const QString file = m_home.filePath(QStringLiteral("mine.theme.json"));
		const ControlResult exported = run(QStringLiteral("theme.export"), {{QStringLiteral("file"), file}});
		QVERIFY2(exported.ok, qPrintable(exported.errorMessage));
		QCOMPARE(exported.result.value(QStringLiteral("colors")).toInt(), static_cast<int>(tokens::tokenNames().size()));
		QVERIFY(run(QStringLiteral("theme.reset")).ok);
		QVERIFY(tokens::themeOverrides().isEmpty());
		const ControlResult imported = run(QStringLiteral("theme.import"), {{QStringLiteral("file"), file}, {QStringLiteral("replace"), true}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QCOMPARE(tokens::themeOverrides().value(QStringLiteral("panel")).toString(), QStringLiteral("#202830"));
		QCOMPARE(tokens::themeOverrides().size(), tokens::tokenNames().size());
		QVERIFY(!run(QStringLiteral("theme.import"), {{QStringLiteral("file"), m_home.filePath(QStringLiteral("missing.json"))}}).ok);
		for (int step = 0; step < 3; ++step) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	void aMalformedThemeFileCannotEraseOverrides()
	{
		const ControlResult set = run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), QStringLiteral("accent")},
			{QStringLiteral("color"), QStringLiteral("#ff8800")}, {QStringLiteral("allow_low_contrast"), true}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		const QString path = m_home.filePath(QStringLiteral("missing-colors.theme.json"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		const QByteArray malformed = R"({"format":"zene-theme"})";
		QCOMPARE(file.write(malformed), static_cast<qint64>(malformed.size()));
		file.close();

		const ControlResult imported = run(QStringLiteral("theme.import"), {{QStringLiteral("file"), path},
			{QStringLiteral("replace"), true}});
		QVERIFY(!imported.ok);
		QCOMPARE(imported.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(tokens::themeOverrides().value(QStringLiteral("accent")).toString(), QStringLiteral("#ff8800"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	void theColourListRunsTheCommands()
	{
		CommandListDialog dialog(CommandLists::themeColors());
		QCOMPARE(dialog.rowCount(), static_cast<int>(tokens::tokenNames().size()));
		// #123456 is unreadable on most roles: allowed explicitly (theme 20's contrast check would refuse it).
		QVERIFY(run(QStringLiteral("theme.set_token"), {{QStringLiteral("name"), dialog.row(0).value(QStringLiteral("name"))},
			{QStringLiteral("color"), QStringLiteral("#123456")}, {QStringLiteral("allow_low_contrast"), true}}).ok);
		QCOMPARE(static_cast<int>(tokens::themeOverrides().size()), 1);
		dialog.refresh();
		const ControlResult cleared = dialog.runAction(0, 1);  // "Use the Palette's"
		QVERIFY2(cleared.ok, qPrintable(cleared.errorMessage));
		QVERIFY(tokens::themeOverrides().isEmpty());
	}

	void accessibleModeScalesTheStylesheet()
	{
		const QString css = QStringLiteral("QPushButton { padding: 5px 10px; min-height: 18px; color: #ffffff; border-width: 1px; }");
		QCOMPARE(tokens::scaleMetrics(css, 1.0), css);
		const QString touch = tokens::scaleMetrics(css, 2.0);
		QVERIFY(touch.contains(QStringLiteral("padding: 10px 20px;")));
		QVERIFY(touch.contains(QStringLiteral("min-height: 36px;")));
		QVERIFY(touch.contains(QStringLiteral("border-width: 1px;")));
		QVERIFY(appearance::accessibleTargetFactor > 1.0);
		// 0.5.0 Q050-141: the touch minimum - 44 x 44 logical pixels with 8 between adjacent targets.
		QCOMPARE(static_cast<int>(std::lround(tokens::size::target * appearance::accessibleTargetFactor)), 44);
		QVERIFY(tokens::targetSize() >= tokens::size::target);
		// Accessible mode's own rules thicken every focusable border and ring the controls the base rules leave bare.
		QStringList unknown;
		const QString rules = tokens::resolveStyleSheet(tokens::accessibleModeRules(), &unknown);
		QVERIFY2(unknown.isEmpty(), qPrintable(unknown.join(QStringLiteral(", "))));
		QVERIFY(rules.contains(QStringLiteral("border-width: 3px")));
		QVERIFY(rules.contains(QStringLiteral("QCheckBox::indicator:focus")));
	}

	void accessibleModeIsAnAppearanceField()
	{
		QVERIFY(!appearance::options().accessible);
		const QString chosen = appearance::options().palette;
		const ControlResult set = run(QStringLiteral("window.appearance"), {{QStringLiteral("accessible"), true}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QVERIFY(appearance::options().accessible);
		QVERIFY(set.result.value(QStringLiteral("accessible")).toBool());
		// Accessible mode draws with the high-contrast palette whatever the chosen one is, and keeps the choice.
		QCOMPARE(appearance::options().palette, chosen);
		QCOMPARE(appearance::effectivePalette(appearance::options()), QStringLiteral("high_contrast"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!appearance::options().accessible);
		QCOMPARE(appearance::effectivePalette(appearance::options()), chosen);
	}

	//! A non-finite zoom is not a zoom: std::clamp leaves NaN untouched (every comparison with it is
	//! false), so it must be refused before it is stored or the interface is sized from garbage.
	void aNonFiniteAppearanceScaleIsRefused()
	{
		const double before = appearance::options().scale;
		appearance::Options bad = appearance::options();
		bad.scale = std::nan("");
		appearance::setOptions(bad);
		const double after = appearance::options().scale;
		QVERIFY(std::isfinite(after));
		QVERIFY(after >= 0.5 && after <= 3.0);
		// An infinity is out of range in the other direction, and is refused the same way.
		bad.scale = std::numeric_limits<double>::infinity();
		appearance::setOptions(bad);
		QVERIFY(std::isfinite(appearance::options().scale));
		QCOMPARE(appearance::options().scale, before);
	}

	//! The W3 density values still work: comfortable and touch turn accessible mode on, standard turns it off,
	//! and the reply still says a density.
	void theOldDensityValuesSetAccessibleMode()
	{
		const ControlResult set = run(QStringLiteral("window.appearance"), {{QStringLiteral("density"), QStringLiteral("comfortable")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QVERIFY(appearance::options().accessible);
		QCOMPARE(set.result.value(QStringLiteral("density")).toString(), QStringLiteral("comfortable"));
		QVERIFY(!run(QStringLiteral("window.appearance"), {{QStringLiteral("density"), QStringLiteral("cramped")}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!appearance::options().accessible);
		QVERIFY(run(QStringLiteral("window.appearance"), {{QStringLiteral("density"), QStringLiteral("touch")}}).ok);
		QVERIFY(appearance::options().accessible);
		const ControlResult off = run(QStringLiteral("window.appearance"), {{QStringLiteral("density"), QStringLiteral("standard")}});
		QVERIFY(!appearance::options().accessible);
		QCOMPARE(off.result.value(QStringLiteral("density")).toString(), QStringLiteral("standard"));
		// An explicit 'accessible' wins over a density sent beside it.
		QVERIFY(run(QStringLiteral("window.appearance"), {{QStringLiteral("density"), QStringLiteral("touch")},
			{QStringLiteral("accessible"), false}}).ok);
		QVERIFY(!appearance::options().accessible);
		// A config written before accessible mode reads its density the same way.
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("density"), QStringLiteral("touch"));
		QVERIFY(appearance::options().accessible);
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("density"), QStringLiteral("standard"));
	}

	//! 0.5.0 L14 (Q050-235): the workspace is an appearance field of its own - selectable and readable through
	//! window.appearance, one undo step back, stored in user configuration, and never implied by a density value.
	void theWorkspaceIsAnAppearanceFieldOfItsOwn()
	{
		QCOMPARE(appearance::options().workspace, QStringLiteral("standard"));  // an absent setting is the default
		const ControlResult set = run(QStringLiteral("window.appearance"), {{QStringLiteral("workspace"), QStringLiteral("touch")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("workspace")).toString(), QStringLiteral("touch"));
		QCOMPARE(appearance::options().workspace, QStringLiteral("touch"));
		QCOMPARE(ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("workspace")), QStringLiteral("touch"));
		QVERIFY(set.result.value(QStringLiteral("restart_pending")).toBool());  // drawn from the next start
		QVERIFY(!set.result.value(QStringLiteral("accessible")).toBool());  // a workspace, not accessible mode
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(appearance::options().workspace, QStringLiteral("standard"));
		QCOMPARE(ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("workspace")), QStringLiteral("standard"));
		// A value outside the list is refused typed, and an unreadable stored value falls back to the default.
		QCOMPARE(run(QStringLiteral("window.appearance"), {{QStringLiteral("workspace"), QStringLiteral("tablet")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("workspace"), QStringLiteral("sideways"));
		QCOMPARE(appearance::options().workspace, QStringLiteral("standard"));
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("workspace"), QStringLiteral("standard"));
	}

	//! One global zoom is the sizing model: the old text-size field asks for the zoom, and reads back as 1.
	void theTextSizeFieldSetsTheOneZoom()
	{
		const ControlResult set = run(QStringLiteral("window.appearance"), {{QStringLiteral("font_scale"), 1.25}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(appearance::options().scale, 1.25);
		QCOMPARE(set.result.value(QStringLiteral("font_scale")).toDouble(), 1.0);
		// 'scale' wins when both are given.
		QVERIFY(run(QStringLiteral("window.appearance"), {{QStringLiteral("scale"), 1.5}, {QStringLiteral("font_scale"), 2.0}}).ok);
		QCOMPARE(appearance::options().scale, 1.5);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(appearance::options().scale, 1.0);
	}

	void askShowsTheAnswers()
	{
		AssistantDialog dialog;
		QVERIFY(dialog.ask(QStringLiteral("how do I add a send to a return channel")) > 0);
		QVERIFY(!dialog.answersText().isEmpty());
		QCOMPARE(dialog.ask(QStringLiteral("a")), 0);
	}

	void whatsNewShowsTheNotes()
	{
		const ControlResult notes = run(QStringLiteral("app.whats_new"));
		QVERIFY2(notes.ok, qPrintable(notes.errorMessage));
		WhatsNewDialog dialog(notes.result);
		// This version's notes, as window.whats_new promises - whichever version the build says it is.
		const QString title = notes.result.value(QStringLiteral("notes")).toObject().value(QStringLiteral("title")).toString();
		QVERIFY(!title.isEmpty());
		QVERIFY2(dialog.notesText().contains(title), qPrintable(dialog.notesText().left(200)));
	}

	void theClockShowsThePosition()
	{
		LargeClock clock;
		QVERIFY2(clock.refresh().startsWith(QStringLiteral("1 . 1 . 000")), qPrintable(clock.refresh()));
	}

	void theTunerWindowListensWhileOpen()
	{
		{
			TunerWindow tuner;
			QCOMPARE(run(QStringLiteral("tuner.get_state")).result.value(QStringLiteral("source")).toString(), QStringLiteral("input"));
			QCOMPARE(tuner.refresh(), QStringLiteral("-"));
		}
		QCOMPARE(run(QStringLiteral("tuner.get_state")).result.value(QStringLiteral("source")).toString(), QStringLiteral("off"));
	}

	void namelessControlsAreNamedFromWhatTheyCarry()
	{
		QWidget root;
		auto* play = new QToolButton(&root);
		play->setToolTip(QStringLiteral("<b>Play</b> from the cursor"));
		auto* save = new QPushButton(QStringLiteral("&Save"), &root);
		auto* bare = new QToolButton(&root);
		bare->setObjectName(QStringLiteral("bareButton"));
		new QLabel(QStringLiteral("not a control"), &root);
		root.show();
		const QJsonObject looked = auditAccessibility(&root, false);
		QCOMPARE(looked.value(QStringLiteral("checked")).toInt(), 3);
		QCOMPARE(looked.value(QStringLiteral("implied")).toInt(), 2);
		QCOMPARE(looked.value(QStringLiteral("unnamed")).toArray().first().toObject().value(QStringLiteral("object")).toString(),
			QStringLiteral("bareButton"));
		QVERIFY(play->accessibleName().isEmpty());
		const QJsonObject filled = auditAccessibility(&root, true);
		QCOMPARE(filled.value(QStringLiteral("filled")).toInt(), 2);
		QCOMPARE(play->accessibleName(), QStringLiteral("Play from the cursor"));
		QCOMPARE(save->accessibleName(), QStringLiteral("Save"));
		QVERIFY(bare->accessibleName().isEmpty());
	}

	//! Theme 17: the walk checks what the accessibility bridge is told - a control announced as a bare client has
	//! no role, and a control that shows a value must give it - and counts the controls Tab cannot reach.
	void theWalkReportsRolesAndValues()
	{
		//! A parameter control with no description of its own: a plain widget the bridge calls a client.
		struct BareDial : public QWidget, public IntModelView
		{
			explicit BareDial(QWidget* parent) : QWidget(parent), IntModelView(new IntModel(3, 0, 9, nullptr, QStringLiteral("Bare"), true), this)
			{
				setFocusPolicy(Qt::TabFocus);
			}
		};
		FloatModel cutoff(440.f, 20.f, 20000.f, 1.f, nullptr, QStringLiteral("Cutoff"));
		QWidget root;
		auto* knob = new Knob(KnobType::Bright26, &root);
		knob->setModel(&cutoff);
		new QSlider(Qt::Horizontal, &root);
		auto* bare = new BareDial(&root);
		bare->setObjectName(QStringLiteral("bareDial"));
		auto* flat = new QToolButton(&root);
		flat->setToolTip(QStringLiteral("Flat"));
		flat->setFocusPolicy(Qt::NoFocus);
		root.show();
		const QJsonObject looked = auditAccessibility(&root, false);
		QCOMPARE(looked.value(QStringLiteral("checked")).toInt(), 4);
		QCOMPARE(looked.value(QStringLiteral("roleless_count")).toInt(), 1);
		const QJsonObject roleless = looked.value(QStringLiteral("roleless")).toArray().first().toObject();
		QCOMPARE(roleless.value(QStringLiteral("object")).toString(), QStringLiteral("bareDial"));
		QCOMPARE(roleless.value(QStringLiteral("role")).toString(), QStringLiteral("Client"));
		QCOMPARE(looked.value(QStringLiteral("valueless_count")).toInt(), 1);  // the knob and the slider say theirs
		QCOMPARE(looked.value(QStringLiteral("valueless")).toArray().first().toObject().value(QStringLiteral("object")).toString(),
			QStringLiteral("bareDial"));
		QCOMPARE(looked.value(QStringLiteral("unreachable_count")).toInt(), 1);  // the flat button
	}

	void theCrosshairIsAPreference()
	{
		QWidget arranger;
		arranger.resize(400, 300);
		arranger.show();
		installArrangerAids(&arranger);
		auto* overlay = arranger.findChild<QWidget*>(QStringLiteral("arrangerCrosshair"));
		QVERIFY(overlay != nullptr);
		const ControlResult on = run(QStringLiteral("view.crosshair"), {{QStringLiteral("enabled"), true}});
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		QVERIFY(crosshairShown());
		QVERIFY(overlay->isVisible());
		QCOMPARE(overlay->size(), arranger.size());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!crosshairShown());
		QVERIFY(!overlay->isVisible());
	}

	//! window.layout_save trims the name (src/gui/ControlCommandsLayouts.cpp saveLayout), so a layout
	//! saved as " Studio " is stored as "Studio"; the lookup must find it under the name it was saved with.
	void aLayoutIsFoundUnderTheNameItWasSavedWith()
	{
		QVERIFY(gui::uistore::write(QStringLiteral("shell.layouts"),
			{{QStringLiteral("layouts"), QJsonObject{{QStringLiteral("Studio"), QJsonObject{{QStringLiteral("view"),
				QStringLiteral("arranger")}}}}}},
			{}, {}, {}).ok);
		const ControlResult removed = run(QStringLiteral("window.layout_remove"),
			{{QStringLiteral("name"), QStringLiteral(" Studio ")}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("name")).toString(), QStringLiteral("Studio"));
		QVERIFY(gui::uistore::read(QStringLiteral("shell.layouts"))
			.value(QStringLiteral("layouts")).toObject().isEmpty());
	}

	void theWindowVerbsNeedAMainWindow()
	{
		for (const char* id : {"window.stay_on_top", "window.tidy", "window.large_clock", "window.ask", "window.whats_new",
			"window.focus_panel", "window.tuner", "window.accessibility_report"})
		{
			const ControlResult refused = run(QLatin1String(id));
			QVERIFY2(!refused.ok, id);
			QCOMPARE(refused.errorKind, ControlErrorKind::Requires);
		}
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(WorkspaceAccessTest)
#include "WorkspaceAccessTest.moc"
