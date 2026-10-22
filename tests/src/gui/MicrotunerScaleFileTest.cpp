/*
 * MicrotunerScaleFileTest.cpp - the Microtuner dialog's own .scl file path
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! The Microtuner dialog's .scl file path: a file that does not describe a scale - an empty
 *  one, or one that names a scale and never declares how many degrees it has - is REFUSED
 *  before the Song's scale is replaced or the session table is fed. The refused file used
 *  to clear the form and hand applyScale() an empty interval list, which built the 1/1
 *  interval the apply code always starts with: a scale in which every key has the same
 *  pitch. A file that does describe a scale still loads. */

#include <QtTest>

#include <cstddef>

#include <QFile>
#include <QTemporaryDir>

#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MicrotunerConfig.h"
#include "Scale.h"
#include "SessionTuning.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! A .scl file, one line per entry.
bool writeScl(const QString& path, const QStringList& lines)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { return false; }
	for (const QString& line : lines) { file.write(line.toUtf8() + "\n"); }
	file.close();
	return true;
}

//! What the Song's first scale slot and the session table hold: the state a refused file
//! must not change.
struct ScaleState
{
	QString description;
	std::size_t intervals = 0;
	const Scale* fed = nullptr;

	bool operator==(const ScaleState& other) const
	{
		return description == other.description && intervals == other.intervals && fed == other.fed;
	}
};

ScaleState stateOfScaleZero()
{
	const auto scale = Engine::getSong()->getScale(0);
	ScaleState state;
	state.description = scale->getDescription();
	state.intervals = scale->getIntervals().size();
	state.fed = SessionTuning::instance()->scale().get();
	return state;
}

} // namespace

class MicrotunerScaleFileTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		getGUI()->mainWindow()->show();
		m_config = new MicrotunerConfig();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void anEmptyScaleFileIsRefused()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("empty.scl"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
		file.close();

		const ScaleState before = stateOfScaleZero();
		QString error;
		const bool loaded = m_config->loadScaleFile(path, &error);

		QVERIFY2(!loaded, "the empty .scl was accepted: it built a one-interval scale whose "
			"every key has the same pitch");
		QVERIFY2(!error.isEmpty(), "the refusal carried no reason");
		QCOMPARE(stateOfScaleZero(), before);
	}

	void aScaleFileWithoutADegreeCountIsRefused()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("name-only.scl"));
		QVERIFY(writeScl(path, {QStringLiteral("a scale with no degree count")}));

		const ScaleState before = stateOfScaleZero();
		QString error;
		const bool loaded = m_config->loadScaleFile(path, &error);

		QVERIFY2(!loaded, "the .scl without a degree count was accepted: it built a "
			"one-interval scale whose every key has the same pitch");
		QVERIFY2(!error.isEmpty(), "the refusal carried no reason");
		QCOMPARE(stateOfScaleZero(), before);
	}

	void aValidScaleFileStillLoads()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("ji3.scl"));
		QVERIFY(writeScl(path, {QStringLiteral("! a just scale"), QStringLiteral("JI three"),
			QStringLiteral("3"), QStringLiteral("100.0"), QStringLiteral("200.0"),
			QStringLiteral("300.0")}));

		QString error;
		QVERIFY2(m_config->loadScaleFile(path, &error),
			qPrintable(QStringLiteral("a valid .scl was refused: %1").arg(error)));

		const auto scale = Engine::getSong()->getScale(0);
		QCOMPARE(scale->getDescription(), QStringLiteral("JI three"));
		// the 1/1 the apply code always starts with, plus the file's three degrees
		QCOMPARE(scale->getIntervals().size(), std::size_t{4});
		QVERIFY(SessionTuning::instance()->scale() != nullptr);
		QCOMPARE(SessionTuning::instance()->scale()->getDescription(), QStringLiteral("JI three"));
	}

private:
	QTemporaryDir m_home;
	MicrotunerConfig* m_config = nullptr;
};

QTEST_MAIN(MicrotunerScaleFileTest)
#include "MicrotunerScaleFileTest.moc"
