/*
 * RenderSourceGuardTest.cpp - a render queued onto its own source project is refused and the project is kept
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
 */

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "ControlRegistry.h"
#include "Engine.h"
#include "RenderQueue.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QByteArray contents(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString fakeOutputPath(int argc, char** argv)
{
	for (int i = 2; i + 1 < argc; ++i)
	{
		if (QString::fromLocal8Bit(argv[i]) == QLatin1String("-o")) { return QString::fromLocal8Bit(argv[i + 1]); }
	}
	return {};
}

int fakeRender(int argc, char** argv)
{
	QFile output(fakeOutputPath(argc, argv));
	if (!output.open(QIODevice::WriteOnly)) { return 5; }
	return output.write("rendered output") == 15 ? 0 : 5;
}

#ifndef _WIN32
// Only the POSIX cases below queue a render; leaving this outside the guard makes it an
// unused function in the Windows translation unit, which the hosted mingw64 build rejects.
int queueRender(const QString& source, const QString& destination)
{
	RenderJob job;
	job.kind = RenderJob::Kind::Render;
	job.source = source;
	job.out = destination;
	RenderQueue& queue = RenderQueue::instance();
	const int id = queue.add(job);
	if (id > 0) { queue.start(); }
	return id;
}
#endif

// The hosted mingw64 build compiles every test with -Werror=unused-function. A definition
// whose every use sits inside a guard that excludes Windows is therefore a build failure on
// that host, while a Linux compiler is happy with it (run 43569206372 died on queueRender).
// The scan below reads the test sources and names every such definition, so the defect is
// visible on every platform.
struct WindowsGuardOffender
{
	QString file;
	int line = 0;
	QString name;
};

struct GuardFrame
{
	bool isWindowsGuard = false;
	bool excludesWindows = false;
};

struct TopLevelDefinition
{
	int line = 0;
	QString name;
};

bool guardExcludesWindows(const QString& line)
{
	static const QRegularExpression guard(QStringLiteral(
		R"(^\s*#\s*(?:ifndef\s+_WIN32\b|ifndef\s+LMMS_BUILD_WIN32\b|if\s+!\s*defined\s*\(\s*(?:_WIN32|LMMS_BUILD_WIN32|Q_OS_WIN)\s*\)))"));
	return guard.match(line).hasMatch();
}

void applyGuardDirective(const QString& kind, const QString& line, QVector<GuardFrame>& stack)
{
	if (kind == QLatin1String("endif"))
	{
		if (!stack.isEmpty()) { stack.removeLast(); }
		return;
	}
	if (kind == QLatin1String("else") || kind == QLatin1String("elif"))
	{
		if (!stack.isEmpty() && stack.last().isWindowsGuard)
		{
			// #else of a Windows exclusion is the Windows branch; #elif is not exclusion.
			stack.last().excludesWindows = kind == QLatin1String("else")
				&& !stack.last().excludesWindows;
		}
		return;
	}
	const bool windowsGuard = guardExcludesWindows(line);
	stack.append(GuardFrame{windowsGuard, windowsGuard});
}

QVector<bool> windowsExcludedLines(const QStringList& lines)
{
	static const QRegularExpression directive(QStringLiteral(
		R"(^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b)"));
	QVector<bool> excluded;
	excluded.reserve(lines.size());
	QVector<GuardFrame> stack;
	for (const QString& line : lines)
	{
		excluded.append(std::any_of(stack.cbegin(), stack.cend(),
			[](const GuardFrame& frame) { return frame.excludesWindows; }));
		const QRegularExpressionMatch match = directive.match(line);
		if (match.hasMatch()) { applyGuardDirective(match.captured(1), line, stack); }
	}
	return excluded;
}

QVector<TopLevelDefinition> topLevelFunctionDefinitions(const QStringList& lines)
{
	static const QRegularExpression definition(QStringLiteral(
		R"(^(?:static\s+)?[A-Za-z_][\w:<>,\s*&]*?\s+([A-Za-z_]\w*)\s*\([^;]*$)"));
	QVector<TopLevelDefinition> definitions;
	for (int i = 0; i < lines.size(); ++i)
	{
		const QRegularExpressionMatch match = definition.match(lines.at(i));
		if (!match.hasMatch()) { continue; }
		bool opens = false;
		for (int j = i + 1; j < lines.size() && j <= i + 4; ++j)
		{
			opens = opens || lines.at(j).trimmed() == QLatin1String("{");
		}
		if (opens) { definitions.append(TopLevelDefinition{i, match.captured(1)}); }
	}
	return definitions;
}

bool usedOnlyInsideWindowsExcludedCode(const QString& name, int definitionLine,
	const QStringList& lines, const QVector<bool>& excluded)
{
	// A use is a call (or an address-of): plain mentions in comments must not make a
	// definition look used on Windows.
	const QRegularExpression use(QStringLiteral(R"((?:\b%1\s*\(|&\s*%1\b))")
		.arg(QRegularExpression::escape(name)));
	bool used = false;
	bool excludedOnly = true;
	for (int i = 0; i < lines.size(); ++i)
	{
		if (i == definitionLine || !use.match(lines.at(i)).hasMatch()) { continue; }
		used = true;
		excludedOnly = excludedOnly && excluded.at(i);
	}
	return used && excludedOnly;
}

QList<WindowsGuardOffender> scanTestFileForWindowsUnusedDefinitions(const QString& path)
{
	QList<WindowsGuardOffender> offenders;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		return {WindowsGuardOffender{path, 0, QStringLiteral("<unreadable>")}};
	}
	const QString text = QString::fromUtf8(file.readAll());
	if (!text.contains(QLatin1String("_WIN32")) && !text.contains(QLatin1String("LMMS_BUILD_WIN32"))
		&& !text.contains(QLatin1String("Q_OS_WIN")))
	{
		return offenders;
	}
	const QStringList lines = text.split(QLatin1Char('\n'));
	const QVector<bool> excluded = windowsExcludedLines(lines);
	for (const TopLevelDefinition& definition : topLevelFunctionDefinitions(lines))
	{
		if (definition.line >= excluded.size() || excluded.at(definition.line)) { continue; }
		if (usedOnlyInsideWindowsExcludedCode(definition.name, definition.line, lines, excluded))
		{
			offenders.append(WindowsGuardOffender{path, definition.line + 1, definition.name});
		}
	}
	return offenders;
}

QList<WindowsGuardOffender> windowsUnusedInternalDefinitions(const QString& directory)
{
	QList<WindowsGuardOffender> offenders;
	QDirIterator files(directory, {QStringLiteral("*.cpp")}, QDir::Files,
		QDirIterator::Subdirectories);
	while (files.hasNext()) { offenders.append(scanTestFileForWindowsUnusedDefinitions(files.next())); }
	return offenders;
}

} // namespace

class RenderSourceGuardTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	QString out(const QString& name) const { return QDir(m_dir.path()).filePath(name); }

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		qunsetenv("ZENE_FAKE_RENDER_SUCCESS");
		run(QStringLiteral("render.queue_cancel"), {{QStringLiteral("all"), true}});
		run(QStringLiteral("render.queue_clear"));
		Engine::getSong()->clearProject();
	}

	void renderOntoItsOwnSourceProjectIsRefusedAndKeepsTheProject()
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		track->setName(QStringLiteral("Own source"));
		track->createClip(TimePos(0));
		const QString project = out(QStringLiteral("own-source.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		const QByteArray original = contents(project);
		QVERIFY(!original.isEmpty());

		// The same file under another spelling, through a symbolic link or through a hard link, is still the source.
		QStringList targets{project, m_dir.path() + QStringLiteral("/./own-source.mmp")};
#ifndef _WIN32
		const QString linked = out(QStringLiteral("own-source-link.mmp"));
		QVERIFY(QFile(project).link(linked));
		targets << linked;
		const QString hard = out(QStringLiteral("own-source-hard.mmp"));
		QVERIFY(::link(QFile::encodeName(project).constData(), QFile::encodeName(hard).constData()) == 0);
		targets << hard;
#endif
		for (const QString& target : targets)
		{
			const ControlResult refused = run(QStringLiteral("render.queue_add"),
				{{QStringLiteral("project"), project}, {QStringLiteral("out"), target}, {QStringLiteral("start"), false}});
			QVERIFY2(!refused.ok, qPrintable(target));
			QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		}
		QVERIFY(RenderQueue::instance().jobs().empty());
		QCOMPARE(contents(project), original);

		// A different output file is still accepted: the refusal is for the source only.
		const ControlResult accepted = run(QStringLiteral("render.queue_add"),
			{{QStringLiteral("project"), project}, {QStringLiteral("out"), out(QStringLiteral("own-source.wav"))},
				{QStringLiteral("start"), false}});
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QCOMPARE(RenderQueue::instance().jobs().size(), std::size_t(1));
		QCOMPARE(contents(project), original);
	}

#if !defined(Q_OS_WIN) && !defined(_WIN32)
	void successfulRenderKeepsSymbolicLinkDestination()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString target = dir.filePath(QStringLiteral("target.wav"));
		const QString destination = dir.filePath(QStringLiteral("render.wav"));
		QFile previous(target);
		QVERIFY(previous.open(QIODevice::WriteOnly));
		QCOMPARE(previous.write("previous output"), qint64(15));
		previous.close();
		QVERIFY(QFile::link(target, destination));
		qputenv("ZENE_FAKE_RENDER_SUCCESS", "1");
		const int id = queueRender(dir.filePath(QStringLiteral("project.mmp")), destination);
		QVERIFY(id > 0);
		QTRY_VERIFY_WITH_TIMEOUT(RenderQueue::instance().job(id)->state == RenderJob::State::Done, 5000);
		QVERIFY(QFileInfo(destination).isSymLink());
		QCOMPARE(contents(target), QByteArray("rendered output"));
		RenderQueue::instance().clearFinished();
	}

	void renderToSymbolicLinkDirectoryDoesNotReplaceIt()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QTemporaryDir targetDirectory(dir.filePath(QStringLiteral("target-XXXXXX")));
		QVERIFY(targetDirectory.isValid());
		const QString destination = dir.filePath(QStringLiteral("render.wav"));
		QVERIFY(QFile::link(targetDirectory.path(), destination));
		qputenv("ZENE_FAKE_RENDER_SUCCESS", "1");
		const int id = queueRender(dir.filePath(QStringLiteral("project.mmp")), destination);
		QVERIFY(id > 0);
		QTRY_VERIFY_WITH_TIMEOUT(RenderQueue::instance().job(id)->state == RenderJob::State::Failed, 5000);
		QVERIFY(QFileInfo(destination).isSymLink());
		QVERIFY(QFileInfo(destination).isDir());
		RenderQueue::instance().clearFinished();
	}

	void successfulRenderSupportsLongDestinationNames()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString destination = dir.filePath(QString(240, QLatin1Char('r')) + QStringLiteral(".wav"));
		QFile previous(destination);
		QVERIFY(previous.open(QIODevice::WriteOnly));
		QCOMPARE(previous.write("previous output"), qint64(15));
		previous.close();
		qputenv("ZENE_FAKE_RENDER_SUCCESS", "1");
		const int id = queueRender(dir.filePath(QStringLiteral("project.mmp")), destination);
		QVERIFY(id > 0);
		QTRY_VERIFY_WITH_TIMEOUT(RenderQueue::instance().job(id)->state == RenderJob::State::Done, 5000);
		QCOMPARE(contents(destination), QByteArray("rendered output"));
		RenderQueue::instance().clearFinished();
	}
#endif

	// A helper used only by the POSIX-only cases must be compiled only there: the
	// hosted mingw64 build turns the unused-function warning into an error. Read the
	// test sources (a Linux compiler cannot observe the Windows preprocessor view) and
	// fail on every definition whose uses all sit inside a Windows-excluding guard.
	void windowsBuildHasNoUnusedTestHelpers()
	{
		const QList<WindowsGuardOffender> offenders = windowsUnusedInternalDefinitions(
			QDir{QStringLiteral(QT_TESTCASE_SOURCEDIR)}.filePath(QStringLiteral("src")));
		QStringList found;
		for (const WindowsGuardOffender& offender : offenders)
		{
			found << QStringLiteral("%1:%2: %3").arg(offender.file).arg(offender.line)
				.arg(offender.name);
		}
		QVERIFY2(offenders.isEmpty(), qPrintable(QStringLiteral(
			"these definitions are only used inside a guard that excludes Windows, so the "
			"mingw64 build reports them unused; move each inside its own guard:\n")
			+ found.join(QLatin1Char('\n'))));
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("render"))
	{
		QCoreApplication app(argc, argv);
		return fakeRender(argc, argv);
	}
	QCoreApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	RenderSourceGuardTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "RenderSourceGuardTest.moc"
