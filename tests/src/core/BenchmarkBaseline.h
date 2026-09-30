/*
 * BenchmarkBaseline.h - the opt-in same-machine regression gate every benchmark here shares
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

#ifndef LMMS_TESTS_BENCHMARK_BASELINE_H
#define LMMS_TESTS_BENCHMARK_BASELINE_H

#include <QtTest>

#include <QFile>
#include <QMap>
#include <QTextStream>

namespace benchtest
{

/*! A timing only compares on the machine that took it, so a benchmark's regression gate is
 *  OPT-IN: ZENE_BENCH_WRITE=<file> writes "key<TAB>value" lines (a baseline for this machine);
 *  ZENE_BENCH_BASELINE=<file> fails when any key is more than @a tolerance slower than it was.
 *  With neither set, nothing is compared. A macro-free function: QVERIFY needs a void return. */
inline void compareOrWriteBaseline(const QMap<QString, double>& measured, double tolerance = 0.2)
{
	const QString write = qEnvironmentVariable("ZENE_BENCH_WRITE");
	if (!write.isEmpty())
	{
		QFile file(write);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
		QTextStream out(&file);
		for (auto it = measured.begin(); it != measured.end(); ++it) { out << it.key() << '\t' << it.value() << '\n'; }
		return;
	}
	const QString baseline = qEnvironmentVariable("ZENE_BENCH_BASELINE");
	if (baseline.isEmpty()) { return; }
	QFile file(baseline);
	QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(baseline));
	QTextStream in(&file);
	while (!in.atEnd())
	{
		const QStringList fields = in.readLine().split(QLatin1Char('\t'));
		if (fields.size() != 2 || !measured.contains(fields[0])) { continue; }
		const double reference = fields[1].toDouble();
		QVERIFY2(measured[fields[0]] <= reference * (1.0 + tolerance), qPrintable(QStringLiteral(
			"%1: %2 against a %3 baseline - more than %4% slower").arg(fields[0]).arg(measured[fields[0]])
			.arg(reference).arg(tolerance * 100.0)));
	}
}

} // namespace benchtest

#endif // LMMS_TESTS_BENCHMARK_BASELINE_H
