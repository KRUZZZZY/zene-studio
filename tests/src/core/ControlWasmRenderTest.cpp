/*
 * ControlWasmRenderTest.cpp - the wasm.render_offline result JSON for invalid samples
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "WasmSandbox.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace lmms::wasm
{

namespace
{

const std::string kNaNOutputWat =
	"(module (memory (export \"memory\") 3) "
	"(global (export \"channels\") i32 (i32.const 1)) "
	"(func (export \"process\") (param i32 i32 i32 f32) (result i32) "
	"(f32.store (local.get 1) (f32.reinterpret_i32 (i32.const 2143289344))) "
	"(i32.const 0)))";

//! process() writes 42 then 7 into the output plane, grows memory by a page
//! between the two stores, and so only a host that re-reads the memory view
//! after the call sees 42 as the plane's first sample.
const std::string kGrowthWat =
	"(module (memory (export \"memory\") 1) "
	"(global (export \"channels\") i32 (i32.const 1)) "
	"(func (export \"process\") (param i32 i32 i32 f32) (result i32) "
	"(f32.store (local.get 1) (f32.const 42)) "
	"(drop (memory.grow (i32.const 1))) "
	"(f32.store (i32.add (local.get 1) (i32.const 4)) (f32.const 7)) "
	"(i32.const 0)))";

//! A growth request past the 4 GiB limit is refused (memory.grow answers -1);
//! the module then stores past the end of its memory and traps.
const std::string kInvalidGrowthWat =
	"(module (memory (export \"memory\") 1) "
	"(global (export \"channels\") i32 (i32.const 1)) "
	"(func (export \"process\") (param i32 i32 i32 f32) (result i32) "
	"(if (i32.eq (memory.grow (i32.const 65536)) (i32.const -1)) "
	"(then (f32.store (i32.const 2147483640) (f32.const 1)))) "
	"(i32.const 0)))";

bool writeModule(const std::string& wat, const QString& path, QString& error)
{
	std::vector<std::uint8_t> bytes;
	std::string assemblyError;
	if (!WasmSandbox::assembleWat(wat, bytes, assemblyError))
	{
		error = QString::fromStdString(assemblyError);
		return false;
	}

	QFile module(path);
	if (!module.open(QIODevice::WriteOnly))
	{
		error = module.errorString();
		return false;
	}
	const qint64 written = module.write(reinterpret_cast<const char*>(bytes.data()),
		static_cast<qint64>(bytes.size()));
	if (written != static_cast<qint64>(bytes.size()))
	{
		error = module.errorString();
		return false;
	}
	return true;
}

bool writeNaNModule(const QString& path, QString& error)
{
	return writeModule(kNaNOutputWat, path, error);
}

} // namespace

class ControlWasmRenderTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void renderOfflineReportsNonFiniteSamples()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString modulePath = directory.filePath(QStringLiteral("nan-output.wasm"));
		QString error;
		QVERIFY2(writeNaNModule(modulePath, error), qPrintable(error));

		const ControlResult rendered = ControlRegistry::instance()->invoke(
			QStringLiteral("wasm.render_offline"),
			{{QStringLiteral("module"), modulePath},
				{QStringLiteral("frames"), 4},
				{QStringLiteral("block_frames"), 4},
				{QStringLiteral("repeats"), 2}});
		QVERIFY2(rendered.ok, qPrintable(rendered.errorMessage));

		const QJsonObject response = QJsonDocument::fromJson(
			QJsonDocument(rendered.result).toJson(QJsonDocument::Compact)).object();
		const QJsonArray runs = response.value(QStringLiteral("runs")).toArray();
		QVERIFY(!runs.isEmpty());
		const QJsonObject run = runs.first().toObject();
		QVERIFY(run.value(QStringLiteral("ok")).toBool());
		QCOMPARE(run.value(QStringLiteral("non_finite_samples")).toInt(-1), 1);
	}

	void renderOfflineReadsTheOutputPlaneAfterTheModuleGrewItsMemory()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString modulePath = directory.filePath(QStringLiteral("grows.wasm"));
		QString error;
		QVERIFY2(writeModule(kGrowthWat, modulePath, error), qPrintable(error));

		const ControlResult rendered = ControlRegistry::instance()->invoke(
			QStringLiteral("wasm.render_offline"),
			{{QStringLiteral("module"), modulePath},
				{QStringLiteral("frames"), 2},
				{QStringLiteral("block_frames"), 2},
				{QStringLiteral("repeats"), 2}});
		QVERIFY2(rendered.ok, qPrintable(rendered.errorMessage));

		// The two samples are 42 and 7: peak 42 and rms sqrt((42^2 + 7^2) / 2),
		// in the inline control renders and in the pooled runs alike. A stale
		// view would read zeros and report peak 0.
		const QJsonObject response = QJsonDocument::fromJson(
			QJsonDocument(rendered.result).toJson(QJsonDocument::Compact)).object();
		const double expectedRms = std::sqrt((42.0 * 42.0 + 7.0 * 7.0) / 2.0);
		for (const QString& key : {QStringLiteral("control_runs"), QStringLiteral("runs")})
		{
			const QJsonArray renders = response.value(key).toArray();
			QVERIFY2(!renders.isEmpty(), qPrintable(key));
			for (const QJsonValue& value : renders)
			{
				const QJsonObject render = value.toObject();
				QVERIFY2(render.value(QStringLiteral("ok")).toBool(), qPrintable(key));
				QCOMPARE(render.value(QStringLiteral("peak")).toDouble(), 42.0);
				QVERIFY(std::fabs(render.value(QStringLiteral("rms")).toDouble() - expectedRms) < 1e-6);
			}
		}
	}

	void renderOfflineRefusesAModuleThatTrapsAfterAFailedGrowth()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString modulePath = directory.filePath(QStringLiteral("bad-growth.wasm"));
		QString error;
		QVERIFY2(writeModule(kInvalidGrowthWat, modulePath, error), qPrintable(error));

		const ControlResult rendered = ControlRegistry::instance()->invoke(
			QStringLiteral("wasm.render_offline"),
			{{QStringLiteral("module"), modulePath},
				{QStringLiteral("frames"), 2},
				{QStringLiteral("block_frames"), 2},
				{QStringLiteral("repeats"), 2}});
		QVERIFY2(!rendered.ok, "a render that trapped was reported as a result");
		QCOMPARE(rendered.errorKind, ControlErrorKind::Refused);
		QVERIFY2(rendered.errorMessage.contains(QStringLiteral("the render did not complete")),
			qPrintable(rendered.errorMessage));
	}

	void renderOfflineRefusesASampleRateThatIsNotAFiniteFloatRate()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString modulePath = directory.filePath(QStringLiteral("nan-output.wasm"));
		QString error;
		QVERIFY2(writeNaNModule(modulePath, error), qPrintable(error));

		// 1e300 and -1 are the finding's inputs; 0 and 1e-60 (which rounds to a
		// float zero) are the other ways a rate stops being a positive one.
		const std::vector<double> rates = {1e300, -1.0, 0.0, 1e-60};
		for (const double rate : rates)
		{
			const ControlResult rendered = ControlRegistry::instance()->invoke(
				QStringLiteral("wasm.render_offline"),
				{{QStringLiteral("module"), modulePath},
					{QStringLiteral("frames"), 4},
					{QStringLiteral("block_frames"), 4},
					{QStringLiteral("repeats"), 2},
					{QStringLiteral("sample_rate"), rate}});
			const QString label = QStringLiteral("sample_rate %1 was rendered").arg(rate);
			QVERIFY2(!rendered.ok, qPrintable(label));
			QCOMPARE(rendered.errorKind, ControlErrorKind::InvalidArgs);
		}
	}

	void renderOfflineCarriesAFiniteSampleRateToTheRender()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString modulePath = directory.filePath(QStringLiteral("nan-output.wasm"));
		QString error;
		QVERIFY2(writeNaNModule(modulePath, error), qPrintable(error));

		const ControlResult rendered = ControlRegistry::instance()->invoke(
			QStringLiteral("wasm.render_offline"),
			{{QStringLiteral("module"), modulePath},
				{QStringLiteral("frames"), 4},
				{QStringLiteral("block_frames"), 4},
				{QStringLiteral("repeats"), 2},
				{QStringLiteral("sample_rate"), 44100.0}});
		QVERIFY2(rendered.ok, qPrintable(rendered.errorMessage));

		const QJsonObject response = QJsonDocument::fromJson(
			QJsonDocument(rendered.result).toJson(QJsonDocument::Compact)).object();
		QCOMPARE(response.value(QStringLiteral("sample_rate")).toDouble(), 44100.0);
	}

	void processRefusesASampleRateThatIsNotAFiniteFloatRate()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString modulePath = directory.filePath(QStringLiteral("nan-output.wasm"));
		QString error;
		QVERIFY2(writeNaNModule(modulePath, error), qPrintable(error));

		const ControlResult loaded = ControlRegistry::instance()->invoke(
			QStringLiteral("wasm.load"), {{QStringLiteral("path"), modulePath}});
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));

		// The same rule as render_offline: 'sample_rate' reaches the module as
		// a float, so a rate outside the float range is refused, not run.
		const ControlResult processed = ControlRegistry::instance()->invoke(
			QStringLiteral("wasm.process"),
			{{QStringLiteral("frames"), 4}, {QStringLiteral("sample_rate"), 1e300}});
		QVERIFY2(!processed.ok, "wasm.process ran with sample_rate 1e300");
		QCOMPARE(processed.errorKind, ControlErrorKind::InvalidArgs);
	}
};

} // namespace lmms::wasm

QTEST_GUILESS_MAIN(lmms::wasm::ControlWasmRenderTest)

#include "ControlWasmRenderTest.moc"
