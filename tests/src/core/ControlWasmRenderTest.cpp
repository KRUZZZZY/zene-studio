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

bool writeNaNModule(const QString& path, QString& error)
{
	std::vector<std::uint8_t> bytes;
	std::string assemblyError;
	if (!WasmSandbox::assembleWat(kNaNOutputWat, bytes, assemblyError))
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
};

} // namespace lmms::wasm

QTEST_GUILESS_MAIN(lmms::wasm::ControlWasmRenderTest)

#include "ControlWasmRenderTest.moc"
