/*
 * ProjectSectionTest.cpp - a registered ProjectSection is saved, loaded and cleared with the project
 *
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

/*! A project-level element that lives outside Song registers itself once (ProjectSection::registerSection,
 *  idempotent); Song then writes it only when it holds something, reads it back by its element name and
 *  empties it whenever the project is cleared. Driven here with a section of the test's own through the real
 *  Song save/load, so a project without the element loads into an empty section (reset-on-absence). */

#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>

#include "Engine.h"
#include "ProjectSection.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! A section holding one integer, written as <coverage-test-section value="..."/> only while it is non-zero.
class CounterSection : public ProjectSection
{
public:
	int value = 0;
	int clears = 0;

	QString elementName() const override { return QStringLiteral("coverage-test-section"); }
	bool shouldPersist() const override { return value != 0; }
	void save(QDomDocument& doc, QDomElement& parent) const override
	{
		QDomElement element = doc.createElement(elementName());
		element.setAttribute(QStringLiteral("value"), value);
		parent.appendChild(element);
	}
	void load(const QDomElement& element) override { value = element.attribute(QStringLiteral("value")).toInt(); }
	void clear() override
	{
		value = 0;
		++clears;
	}
};

bool fileHasElement(const QString& path, const QString& name)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) && file.readAll().contains(('<' + name).toUtf8());
}

} // namespace

class ProjectSectionTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void registeringIsIdempotentAndListed()
	{
		CounterSection section;
		const auto before = ProjectSection::sections().size();
		ProjectSection::registerSection(&section);
		ProjectSection::registerSection(&section);
		QCOMPARE(ProjectSection::sections().size(), before + 1);
		const auto& all = ProjectSection::sections();
		QVERIFY(std::find(all.begin(), all.end(), &section) != all.end());

		section.value = 7;
		ProjectSection::clearAll();
		QCOMPARE(section.value, 0);
		QCOMPARE(section.clears, 1);
		// The registry holds the pointer for the life of the process: take it out again so the later
		// destruction of this stack object leaves nothing dangling for the other cases.
		auto& mutableAll = const_cast<std::vector<ProjectSection*>&>(ProjectSection::sections());
		mutableAll.erase(std::remove(mutableAll.begin(), mutableAll.end(), &section), mutableAll.end());
	}

	void aSectionIsSavedLoadedAndClearedWithTheProject()
	{
		static CounterSection* section = [] {
			auto* made = new CounterSection();
			ProjectSection::registerSection(made);
			return made;
		}();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		Song* song = Engine::getSong();

		// Empty: the element is not written at all (a project that never used it re-saves byte for byte).
		section->value = 0;
		const QString empty = QDir(dir.path()).filePath(QStringLiteral("empty.mmp"));
		QVERIFY(song->saveProjectFile(empty));
		QVERIFY(!fileHasElement(empty, QStringLiteral("coverage-test-section")));

		// Holding something: written, and read back into a cleared section by its element name.
		section->value = 42;
		const QString full = QDir(dir.path()).filePath(QStringLiteral("full.mmp"));
		QVERIFY(song->saveProjectFile(full));
		QVERIFY(fileHasElement(full, QStringLiteral("coverage-test-section")));
		section->value = 0;
		song->loadProject(full);
		QCOMPARE(section->value, 42);

		// Reset on absence: loading the project saved without the element empties the section.
		song->loadProject(empty);
		QCOMPARE(section->value, 0);

		// Clearing the project empties it too.
		section->value = 9;
		const int clearsBefore = section->clears;
		song->clearProject();
		QCOMPARE(section->value, 0);
		QVERIFY(section->clears > clearsBefore);
	}
};

QTEST_GUILESS_MAIN(ProjectSectionTest)
#include "ProjectSectionTest.moc"
