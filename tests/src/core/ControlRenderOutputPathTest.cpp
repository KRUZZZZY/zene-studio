/*
 * ControlRenderOutputPathTest.cpp - render.render output path validation
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"

using namespace lmms;

namespace
{

QString repoRoot()
{
	return QString::fromUtf8(ZENE_SOURCE_DIR);
}

QString renderArgsValidationSource()
{
	const QString path = repoRoot() + QStringLiteral("/src/core/ControlCommandsProject.cpp");
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { return {}; }
	const QByteArray source = file.readAll();
	const int begin = source.indexOf("QString parseRenderArgs(");
	const int end = source.indexOf("The render RANGE", begin);
	if (begin < 0 || end <= begin) { return {}; }
	return QString::fromUtf8(source.mid(begin, end - begin));
}

QString nativeAbsoluteOutputPath()
{
#ifdef Q_OS_WIN
	return QStringLiteral("C:\\Music\\mix.wav");
#else
	return QDir::tempPath() + QStringLiteral("/zene-render-output-path.wav");
#endif
}

} // namespace

class ControlRenderOutputPathTest : public QObject
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

	void nativeAbsolutePathPassesValidation()
	{
		const QString out = nativeAbsoluteOutputPath();
		QVERIFY(QFileInfo(out).isAbsolute());
		QVERIFY(Engine::getSong()->isEmpty());

		const ControlResult result = ControlRegistry::instance()->invoke(
			QStringLiteral("render.render"), {{QStringLiteral("out"), out}});
		QVERIFY(!result.ok);
		QVERIFY2(result.errorMessage.contains(QStringLiteral("session is empty")),
			qPrintable(result.errorMessage));
	}

	void validationUsesHostNativeAbsolutePathRules()
	{
		const QString body = renderArgsValidationSource();
		QVERIFY2(!body.isEmpty(), "could not locate parseRenderArgs source");
		QVERIFY2(body.contains(QStringLiteral("QFileInfo(*out).isAbsolute()")),
			"render output validation must use QFileInfo's host-native absolute-path rules");
	}
};

QTEST_GUILESS_MAIN(ControlRenderOutputPathTest)
#include "ControlRenderOutputPathTest.moc"
