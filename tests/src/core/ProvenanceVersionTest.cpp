/*
 * ProvenanceVersionTest.cpp - the <z:provenance> version and its two-section files (SPEC-ARCH-4 1.9):
 *                             the append-only <z:provenance> section, read
 *                             back from the FILE after a fresh load.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include "Engine.h"
#include "ProvenanceSection.h"
#include "Song.h"

using namespace lmms;

namespace
{

QDomDocument parseFile(const QString& path)
{
	QDomDocument document;
	QFile file(path);
	if (file.open(QIODevice::ReadOnly)) { document.setContent(&file); }
	return document;
}

QDomElement provenanceElement(const QDomDocument& document)
{
	const QDomElement song = document.documentElement().firstChildElement(QStringLiteral("song"));
	return song.firstChildElement(QStringLiteral("z:provenance"));
}

QList<QDomElement> changeElements(const QDomElement& provenance)
{
	QList<QDomElement> changes;
	for (QDomNode node = provenance.firstChild(); !node.isNull(); node = node.nextSibling())
	{
		if (node.isElement() && node.nodeName() == QLatin1String("z:change"))
		{
			changes << node.toElement();
		}
	}
	return changes;
}

//! The command ids of every <z:change> under EVERY <z:provenance> child of the song, in document order.
QStringList allChanges(const QDomDocument& document)
{
	QStringList commands;
	const QDomElement song = document.documentElement().firstChildElement(QStringLiteral("song"));
	for (QDomElement section = song.firstChildElement(QStringLiteral("z:provenance")); !section.isNull();
		section = section.nextSiblingElement(QStringLiteral("z:provenance")))
	{
		for (const QDomElement& change : changeElements(section)) { commands << change.attribute(QStringLiteral("command")); }
	}
	return commands;
}

provenance::Change changeFromElement(const QDomElement& element)
{
	provenance::Change change;
	change.seq = element.attribute(QStringLiteral("seq")).toULongLong();
	change.at = element.attribute(QStringLiteral("at"));
	change.actor = element.attribute(QStringLiteral("actor"));
	change.instance = element.attribute(QStringLiteral("instance"));
	change.command = element.attribute(QStringLiteral("command"));
	change.target = element.attribute(QStringLiteral("target"));
	change.before = element.attribute(QStringLiteral("before"));
	change.after = element.attribute(QStringLiteral("after"));
	change.nested = element.attribute(QStringLiteral("nested"));
	return change;
}

QList<provenance::Change> changesFrom(const QDomElement& provenance)
{
	QList<provenance::Change> changes;
	for (const QDomElement& element : changeElements(provenance)) { changes << changeFromElement(element); }
	return changes;
}

} // namespace

class ProvenanceVersionTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;
	QString m_pristine;

	//! Loads \a path and requires the load was not refused.
	void loadProject(const QString& path)
	{
		Song* song = Engine::getSong();
		song->loadProject(path);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
	}

	QList<provenance::Change> heldChanges()
	{
		QList<provenance::Change> changes;
		provenance::Section& section = provenance::Section::instance();
		for (int i = 0; i < section.size(); ++i) { changes << section.at(i); }
		return changes;
	}

private slots:
	void initTestCase()
	{
		QVERIFY2(m_dir.isValid(), "no temporary directory for the round-trip files");
		Engine::init(true);
		m_pristine = m_dir.filePath(QStringLiteral("pristine.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(m_pristine, false));
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! THE VERSION: a section whose entries carry `nested` (an aggregate's children) is written as v="2",
	//! and only then. Older builds refuse a v="2" section as a whole and keep it verbatim, so no build drops
	//! the ids by rewriting it. A v="2" section round-trips through a save and a fresh load unchanged.
	void aSectionWithNestedIdsIsVersionTwoAndRoundTrips()
	{
		loadProject(m_pristine);
		provenance::Section::instance().clear();
		provenance::recordChange(QStringLiteral("range.consolidate"), QJsonObject(), QJsonObject{{QStringLiteral("n"), 2}},
			QStringList{QStringLiteral("clip.consolidate"), QStringLiteral("clip.consolidate")});
		const QString first = m_dir.filePath(QStringLiteral("nested-first.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(first, false));
		const QDomElement written = provenanceElement(parseFile(first));
		QCOMPARE(written.attribute(QStringLiteral("v")), QStringLiteral("2"));
		QCOMPARE(changeElements(written).first().attribute(QStringLiteral("nested")),
			QStringLiteral("clip.consolidate clip.consolidate"));

		loadProject(first);
		QCOMPARE(heldChanges().last().nested, QStringLiteral("clip.consolidate clip.consolidate"));
		const QString second = m_dir.filePath(QStringLiteral("nested-second.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(second, false));
		const QDomElement resaved = provenanceElement(parseFile(second));
		QCOMPARE(resaved.attribute(QStringLiteral("v")), QStringLiteral("2"));
		QVERIFY2(changesFrom(resaved) == changesFrom(written), "the v2 section changed on a fresh load and resave");
	}

	//! THE OLD SHAPE: a section without nested ids stays v="1", and a v="1" entry that carries `nested` is
	//! not a shape this writer produces, so it is refused rather than claimed.
	void aSectionWithoutNestedIdsStaysVersionOneAndRefusesNestedInVersionOne()
	{
		loadProject(m_pristine);
		provenance::Section::instance().clear();
		provenance::recordChange(QStringLiteral("track.add"), QJsonObject(), QJsonObject{{QStringLiteral("track"), QStringLiteral("trk-1")}});
		const QString plain = m_dir.filePath(QStringLiteral("plain.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(plain, false));
		const QDomElement written = provenanceElement(parseFile(plain));
		QCOMPARE(written.attribute(QStringLiteral("v")), QStringLiteral("1"));
		QVERIFY(!changeElements(written).first().hasAttribute(QStringLiteral("nested")));

		QDomDocument document;
		document.setContent(QStringLiteral(
			"<z:provenance v=\"1\" seq=\"1\"><z:change seq=\"1\" at=\"2026-01-01T00:00:00Z\" actor=\"human\""
			" instance=\"0123abcd-456\" command=\"range.consolidate\" nested=\"clip.consolidate\"/></z:provenance>"));
		const int heldBefore = heldChanges().size();
		QVERIFY2(!provenance::Section::instance().load(document.documentElement()),
			"a v=\"1\" entry carrying nested ids must be refused, not claimed");
		QCOMPARE(heldChanges().size(), heldBefore);
		provenance::Section::instance().clear();
	}

	//! A literal v="2" section with a nested entry is claimed, and its ids are kept.
	void aVersionTwoLiteralWithNestedIdsIsAdopted()
	{
		QDomDocument document;
		document.setContent(QStringLiteral(
			"<z:provenance v=\"2\" seq=\"1\"><z:change seq=\"1\" at=\"2026-01-01T00:00:00Z\" actor=\"human\""
			" instance=\"0123abcd-456\" command=\"range.consolidate\" nested=\"clip.consolidate clip.consolidate\"/></z:provenance>"));
		provenance::Section::instance().clear();
		QVERIFY2(provenance::Section::instance().load(document.documentElement()), "a v=\"2\" section must be adopted");
		QCOMPARE(heldChanges().last().nested, QStringLiteral("clip.consolidate clip.consolidate"));
		provenance::Section::instance().clear();
	}

	//! TWO SECTIONS IN ONE FILE: a 0.4 build keeps a v="2" section unclaimed and then writes its own v="1"
	//! section into the same file, so the file holds two <z:provenance> elements, in either order. The load adopts
	//! the first and keeps the second verbatim as unclaimed; the save re-emits it. Nothing is lost, and a reload
	//! finds the same entries again.
	void twoProvenanceSectionsInOneFileLoseNoEntry()
	{
		// Each element comes from a real save: a plain change (v="1") and a nested one (v="2").
		auto sectionOf = [this](const QString& name, bool nested) {
			loadProject(m_pristine);
			provenance::Section::instance().clear();
			if (nested) {
				provenance::recordChange(QStringLiteral("range.consolidate"), QJsonObject(), QJsonObject{{QStringLiteral("n"), 2}},
					QStringList{QStringLiteral("clip.consolidate"), QStringLiteral("clip.consolidate")});
			} else {
				provenance::recordChange(QStringLiteral("track.add"), QJsonObject(), QJsonObject{{QStringLiteral("track"), QStringLiteral("trk-1")}});
			}
			const QString path = m_dir.filePath(name);
			if (!Engine::getSong()->saveProjectFile(path, false)) { return QDomElement(); }
			return provenanceElement(parseFile(path));
		};
		const QDomElement v1 = sectionOf(QStringLiteral("two-plain.mmp"), false);
		const QDomElement v2 = sectionOf(QStringLiteral("two-nested.mmp"), true);
		QVERIFY2(!v1.isNull() && !v2.isNull(), "the fixture sections were not written");
		QCOMPARE(v1.attribute(QStringLiteral("v")), QStringLiteral("1"));
		QCOMPARE(v2.attribute(QStringLiteral("v")), QStringLiteral("2"));

		for (const bool v2First : {true, false})
		{
			QDomDocument document = parseFile(m_pristine);
			QDomElement song = document.documentElement().firstChildElement(QStringLiteral("song"));
			song.appendChild((v2First ? v2 : v1).cloneNode(true));
			song.appendChild((v2First ? v1 : v2).cloneNode(true));
			const QString both = m_dir.filePath(v2First ? QStringLiteral("two-v2-first.mmp") : QStringLiteral("two-v1-first.mmp"));
			QFile out(both);
			QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
			QTextStream(&out) << document.toString(2);
			out.close();

			loadProject(both);
			const QString resaved = m_dir.filePath(QStringLiteral("two-resaved.mmp"));
			QVERIFY(Engine::getSong()->saveProjectFile(resaved, false));
			const QStringList kept = allChanges(parseFile(resaved));
			QCOMPARE(kept.size(), 2);
			QVERIFY2(kept.contains(QStringLiteral("track.add")) && kept.contains(QStringLiteral("range.consolidate")),
				qPrintable(kept.join(QLatin1Char(' '))));

			loadProject(resaved);  // and once more: a reload of the resaved file keeps the same entries
			const QString again = m_dir.filePath(QStringLiteral("two-again.mmp"));
			QVERIFY(Engine::getSong()->saveProjectFile(again, false));
			QCOMPARE(allChanges(parseFile(again)).size(), 2);
		}
	}

};

QTEST_GUILESS_MAIN(ProvenanceVersionTest)
#include "ProvenanceVersionTest.moc"
