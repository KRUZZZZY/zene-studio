/*
 * DesignTokensTest.cpp - the 0.5.0 design system holds to WCAG AA and paints resolution-independently
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

#include <QApplication>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QtTest>

#include "DesignTokens.h"
#include "ShellIcons.h"
#include "WcagContrast.h"

using namespace lmms::gui;

//! The design tokens are the theme (docs/adr/ADR-0001-UI-TOOLKIT.md): if they pass, every
//! new surface painted from them passes. ThemeContrastTest still holds the inherited
//! style.css files; this holds what replaces them.
class DesignTokensTest : public QObject
{
	Q_OBJECT

private slots:
	void everyDeclaredPairMeetsItsContrast()
	{
		const auto pairs = tokens::contrastPairs();
		QVERIFY(pairs.size() >= 15);
		for (const tokens::ContrastPair& pair : pairs)
		{
			const double ratio = lmms::a11y::contrastRatio(pair.foreground, pair.background);
			QVERIFY2(ratio >= pair.minimum, qPrintable(QStringLiteral("%1: %2:1, needs %3:1")
				.arg(QLatin1String(pair.name)).arg(ratio, 0, 'f', 2).arg(pair.minimum)));
		}
	}

	//! 0.5.0 L14 (WF-130): the high-contrast palette meets every declared pair too, and its text is
	//! 7:1 (WCAG AAA) on every surface.
	void theHighContrastPaletteIsAAA()
	{
		const tokens::Palette hc = tokens::paletteNamed(QStringLiteral("high_contrast"));
		for (const tokens::ContrastPair& pair : tokens::contrastPairs(hc))
		{
			const double ratio = lmms::a11y::contrastRatio(pair.foreground, pair.background);
			QVERIFY2(ratio >= pair.minimum, qPrintable(QStringLiteral("%1: %2:1, needs %3:1")
				.arg(QLatin1String(pair.name)).arg(ratio, 0, 'f', 2).arg(pair.minimum)));
		}
		for (const QColor& surface : {hc.base, hc.panel, hc.raised, hc.control, hc.controlHover, hc.selected})
		{
			QVERIFY(lmms::a11y::contrastRatio(hc.text, surface) >= 7.0);
		}
	}

	void textOnEverySurfaceIsAA()
	{
		// Not only the pairs someone remembered to list: primary text on every surface step.
		const tokens::Palette& p = tokens::palette();
		for (const QColor& surface : {p.base, p.panel, p.raised, p.control, p.controlHover, p.selected})
		{
			QVERIFY(lmms::a11y::meetsTextContrast(p.text, surface));
			QVERIFY(lmms::a11y::meetsComponentContrast(p.focusRing, surface));
		}
	}

	void theGeneratedStylesheetIsAA()
	{
		// The same rule ThemeContrastTest applies to style.css: a rule declaring both a text
		// colour and a background, as hex, is measured against 4.5:1.
		const QString css = tokens::applicationStyleSheet();
		QVERIFY(!css.isEmpty());
		static const QRegularExpression rule(QStringLiteral("([^{}]+)\\{([^}]*)\\}"));
		static const QRegularExpression fg(QStringLiteral("(?:^|[;\\s])color:\\s*(#[0-9a-fA-F]{6})"));
		static const QRegularExpression bg(QStringLiteral("background(?:-color)?:\\s*(#[0-9a-fA-F]{6})"));
		int measured = 0;
		for (auto it = rule.globalMatch(css); it.hasNext();)
		{
			const auto m = it.next();
			const auto f = fg.match(m.captured(2));
			const auto b = bg.match(m.captured(2));
			if (!f.hasMatch() || !b.hasMatch() || m.captured(1).contains(QStringLiteral(":disabled"))) { continue; }
			++measured;
			const double ratio = lmms::a11y::contrastRatio(QColor(f.captured(1)), QColor(b.captured(1)));
			QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("%1: %2:1").arg(m.captured(1).trimmed()).arg(ratio)));
		}
		QVERIFY2(measured >= 4, "the generated stylesheet should declare its text/background pairs");
	}

	void velocityColoursRiseInLightness()
	{
		double previous = -1.0;
		for (int v = 0; v <= 200; v += 10)
		{
			const double l = lmms::a11y::relativeLuminance(tokens::velocityColor(v));
			QVERIFY2(l > previous, qPrintable(QStringLiteral("velocity %1").arg(v)));
			previous = l;
		}
		// Out-of-range input clamps instead of extrapolating.
		QCOMPARE(tokens::velocityColor(-50), tokens::velocityColor(0));
		QCOMPARE(tokens::velocityColor(500), tokens::velocityColor(200));
	}

	void trackColoursAreDistinctAndWrap()
	{
		QSet<QRgb> seen;
		for (int i = 0; i < tokens::trackColorCount(); ++i) { seen.insert(tokens::trackColor(i).rgb()); }
		QCOMPARE(seen.size(), tokens::trackColorCount());
		QCOMPARE(tokens::trackColor(tokens::trackColorCount()), tokens::trackColor(0));
		QCOMPARE(tokens::trackColor(-1), tokens::trackColor(tokens::trackColorCount() - 1));
	}

	void theTypeScaleIsOrdered()
	{
		int previous = 0;
		for (auto role : {tokens::TextRole::Caption, tokens::TextRole::Small, tokens::TextRole::Body,
			tokens::TextRole::Title, tokens::TextRole::Display})
		{
			const int px = tokens::font(role).pixelSize();
			QVERIFY(px > previous);
			previous = px;
		}
	}

	void everyIconIsAnSvgThatPaints()
	{
		const QStringList ids = shellicons::ids();
		QVERIFY(ids.size() >= 25);
		for (const QString& id : ids)
		{
			QVERIFY2(!shellicons::svg(id).isEmpty(), qPrintable(id));
			const QImage image = shellicons::icon(id).pixmap(QSize(24, 24), 1.0).toImage();
			bool inked = false;
			for (int y = 0; y < image.height() && !inked; ++y)
			{
				for (int x = 0; x < image.width() && !inked; ++x) { inked = qAlpha(image.pixel(x, y)) > 0; }
			}
			QVERIFY2(inked, qPrintable(id + QStringLiteral(" paints nothing")));
		}
		QVERIFY(shellicons::svg(QStringLiteral("no-such-icon")).isEmpty());
	}

	void everyInheritedIconNameHasAModernIcon()
	{
		// The legacy map is the guarantee that no old bitmap icon is drawn: each entry must name
		// an icon the binary actually carries.
		const QStringList names = shellicons::legacyNames();
		QVERIFY(names.size() >= 150);
		for (const QString& name : names)
		{
			QVERIFY2(!shellicons::legacyPixmap(name, 16, 16).isNull(), qPrintable(name));
		}
		QCOMPARE(shellicons::lucideForLegacy(QStringLiteral("tripleoscillator/logo")), QStringLiteral("audio-waveform"));
		QVERIFY(shellicons::lucideForLegacy(QStringLiteral("lcd_19green")).isEmpty());  // artwork, not an icon
	}

	void theThemeGlyphsUseTheTokenColours()
	{
		// The few SVGs the stylesheet references by url() cannot take a token at run time, so
		// their fixed colours are held to the tokens here.
		const auto colourIn = [](const QString& file) {
			QFile f(QStringLiteral(":/zene/") + file);
			return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
		};
		for (const char* muted : {"chevron-down.svg", "chevron-up.svg", "chevron-right.svg", "chevron-left.svg",
			"gear.svg", "speaker.svg", "headphones.svg"})
		{
			QVERIFY2(colourIn(QString::fromLatin1(muted)).contains(tokens::palette().textMuted.name()), muted);
		}
		for (const char* onAccent : {"check.svg", "speaker-off.svg", "headphones-on.svg"})
		{
			QVERIFY2(colourIn(QString::fromLatin1(onAccent)).contains(tokens::palette().textOnAccent.name()), onAccent);
		}
	}

	void theStylesheetTemplateResolvesEveryToken()
	{
		const QString templ = tokens::styleSheetTemplate();
		QVERIFY2(templ.size() > 5000, "the compiled-in zene.qss is missing");
		QStringList unknown;
		const QString resolved = tokens::resolveStyleSheet(templ, &unknown);
		QVERIFY2(unknown.isEmpty(), qPrintable(unknown.join(QStringLiteral(", "))));
		QVERIFY(!resolved.contains(QLatin1Char('@')));
	}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	void iconsPaintAtTheScreensScale()
	{
		// HiDPI: a 2x request is a 2x bitmap painted from the path, not a 1x one stretched.
		const QIcon icon = shellicons::icon(QStringLiteral("play"));
		const QPixmap one = icon.pixmap(QSize(16, 16), 1.0);
		const QPixmap two = icon.pixmap(QSize(16, 16), 2.0);
		QCOMPARE(one.size(), QSize(16, 16));
		QCOMPARE(two.size(), QSize(32, 32));
		QCOMPARE(two.devicePixelRatio(), 2.0);
		// Checked paints in the accent; the image differs from the unchecked one.
		const QImage off = icon.pixmap(QSize(16, 16), 1.0, QIcon::Normal, QIcon::Off).toImage();
		const QImage on = icon.pixmap(QSize(16, 16), 1.0, QIcon::Normal, QIcon::On).toImage();
		QVERIFY(off != on);
	}
#endif
};

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
		&& qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
	QApplication app(argc, argv);
	DesignTokensTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "DesignTokensTest.moc"
