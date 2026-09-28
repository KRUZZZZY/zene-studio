/*
 * ThemeContrastTest.cpp - the shipped themes' text is held to WCAG AA contrast
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

//! WcagContrastTest proves the helper's arithmetic; nothing applied it to the
//! themes the product ships. This reads each theme's style.css and holds every
//! rule that declares BOTH a text `color` and a `background(-color)` as hex to
//! the AA text minimum, 4.5:1 (include/WcagContrast.h). `selection-background-color`
//! is not the widget background and is not read as one.
//!
//! Scope, stated rather than implied: only pairs declared in the SAME rule are
//! measured (a colour inherited from another selector is not resolved), and
//! `:disabled` rules are skipped - WCAG 1.4.3 exempts inactive components.
//!
//! It is a ratchet. The themes are inherited upstream files, and recolouring
//! them is a visible design change, so today's failures are listed below with
//! their measured ratio instead of being fixed here. A NEW failure fails the
//! test; so does a listed pair that now passes (take it off the list).

#include <QtTest>

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include "WcagContrast.h"

namespace
{

struct Pair
{
	QString selector;
	QColor foreground;
	QColor background;
};

//! "<theme>|<selector as written, whitespace collapsed>". Measured 2026-09-28.
const QSet<QString>& knownFailures()
{
	static const QSet<QString> known{
		// 3.58:1 - #c9c9c9 on #5b6571.
		QStringLiteral("classic|lmms--gui--TrackLabelButton"),
		// 4.49:1 - #e0e0e0 on #5b6571.
		QStringLiteral("classic|lmms--gui--MixerChannelView"),
	};
	return known;
}

QVector<Pair> declaredPairs(const QString& css)
{
	QString text = css;
	text.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"),
		QRegularExpression::DotMatchesEverythingOption));
	static const QRegularExpression rule(QStringLiteral("([^{}]+)\\{([^{}]*)\\}"));
	static const QRegularExpression fg(QStringLiteral("(?<![-\\w])color\\s*:\\s*(#[0-9a-fA-F]{3,8})\\b"));
	static const QRegularExpression bg(QStringLiteral("(?<![-\\w])background(?:-color)?\\s*:\\s*(#[0-9a-fA-F]{3,8})\\b"));

	QVector<Pair> pairs;
	auto it = rule.globalMatch(text);
	while (it.hasNext())
	{
		const auto match = it.next();
		const QString body = match.captured(2);
		const auto f = fg.match(body);
		const auto b = bg.match(body);
		if (!f.hasMatch() || !b.hasMatch()) { continue; }
		const QString selector = match.captured(1).simplified();
		if (selector.contains(QStringLiteral(":disabled"))) { continue; }
		pairs.push_back({selector, QColor(f.captured(1)), QColor(b.captured(1))});
	}
	return pairs;
}

} // namespace

class ThemeContrastTest : public QObject
{
	Q_OBJECT

private slots:
	void theShippedThemesMeetAaTextContrast_data()
	{
		QTest::addColumn<QString>("theme");
		QTest::newRow("default") << QStringLiteral("default");
		QTest::newRow("classic") << QStringLiteral("classic");
	}

	void theShippedThemesMeetAaTextContrast()
	{
		QFETCH(QString, theme);
		QFile file(QStringLiteral(ZENE_THEMES_DIR "/%1/style.css").arg(theme));
		QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
		const QVector<Pair> pairs = declaredPairs(QString::fromUtf8(file.readAll()));
		QVERIFY2(!pairs.isEmpty(), "no rule declares a text colour and a background - the parser is broken");

		QStringList newFailures;
		QStringList nowPassing;
		for (const Pair& pair : pairs)
		{
			const qreal ratio = lmms::a11y::contrastRatio(pair.foreground, pair.background);
			const QString key = theme + QLatin1Char('|') + pair.selector;
			const bool passes = ratio >= 4.5;
			std::printf("CONTRAST %s %.2f %s\n", qPrintable(theme), ratio, qPrintable(pair.selector));
			if (!passes && !knownFailures().contains(key))
			{
				newFailures << QStringLiteral("%1 (%2:1)").arg(key).arg(ratio, 0, 'f', 2);
			}
			if (passes && knownFailures().contains(key)) { nowPassing << key; }
		}
		QVERIFY2(newFailures.isEmpty(), qPrintable(QStringLiteral("below 4.5:1: ") + newFailures.join(QStringLiteral("; "))));
		QVERIFY2(nowPassing.isEmpty(), qPrintable(QStringLiteral("now passes - remove from knownFailures(): ")
			+ nowPassing.join(QStringLiteral("; "))));
	}
};

QTEST_GUILESS_MAIN(ThemeContrastTest)
#include "ThemeContrastTest.moc"
