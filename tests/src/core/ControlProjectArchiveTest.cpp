/*
 * ControlProjectArchiveTest.cpp - the project.missing_assets /
 *                                 project.hash_assets / project.relink group's
 *                                 SURFACE and its measured effect (feature row
 *                                 38, SPEC A11-A16, release contract 3.1).
 *
 * The ENGINE half is include/ControlProjectAssets.h (the READ TU
 * ControlProjectAssets.cpp and the WRITE TU ControlProjectAssetsRelink.cpp);
 * this file holds the surface to account, in process: the three registered ids
 * with their schemas and mutating flags, the three contract rows, and the
 * measured effect.
 *
 * The NEGATIVE CONTROL is anIntactProjectReportsNoMissingAssets(): a project
 * whose every reference is on disk reports an EMPTY missing list. A detector
 * that reported everything, or that counted a reference it never resolved,
 * fails there and not in the positive test. Its twin,
 * eachUnresolvableReferenceIsReportedWithItsStoredValue(), is the positive half.
 *
 * The relink half proves the inverse for real: relink, read the file back, then
 * control.undo and compare the whole document with the bytes that were there
 * before (not "the attribute looks right again").
 *
 * The registered proof is THIS file, as the ctest ControlProjectArchiveTest
 * (tests/CMakeLists.txt): it drives the three ids through
 * ControlRegistry::invoke, the same entry the socket calls. A committed
 * --control-socket transcript is NOT provided by this lane - the contract's
 * third item allows either - and that absence is stated in
 * docs/KNOWN-LIMITATIONS.md rather than implied away.
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

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "ControlProjectAssets.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

namespace
{

//! The three ids of the group, in the order the registration lists them.
QStringList archiveIds()
{
	return {
		QStringLiteral("project.missing_assets"),
		QStringLiteral("project.hash_assets"),
		QStringLiteral("project.relink"),
	};
}

//! readBytes() is revtest::readBytes (ReversibilityTestSupport.h): one
//! definition, not a second copy that could drift from it.

bool writeBytes(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	return file.write(bytes) == bytes.size();
}

QString sha256Of(const QString& path)
{
	return QString::fromLatin1(QCryptographicHash::hash(readBytes(path),
		QCryptographicHash::Sha256).toHex());
}

//! A file that is a file: 44 bytes of RIFF header, then silence. The verbs
//! under test hash what is there; they do not decode it.
QByteArray fakeWave(int payloadBytes)
{
	return QByteArray("RIFF").append(4, '\0').append("WAVEfmt ").append(16, '\0')
		.append("data").append(payloadBytes, '\1');
}

/*! The fixture project. Five references, one of them inline:
 *    sampleclip          -> <dir>/clip.wav      (absolute)
 *    audiofileprocessor  -> <dir>/sample.wav    (absolute)
 *    sampleclip          -> gone.wav            (legacy relative, project dir)
 *    sf2player           -> <dir>/sound.sf2     (absolute)
 *    session clip slot   -> <dir>/slot.wav      (absolute)
 *  plus one element whose media is inline (src="" with sampledata), which the
 *  scan counts rather than lists. */
QByteArray fixtureDocument(const QString& dir)
{
	const QString quoted = QDir::fromNativeSeparators(dir);
	QString xml = QStringLiteral(
		"<?xml version=\"1.0\"?>\n"
		"<zene-project creator=\"Zene Studio\" version=\"1.0\" type=\"song\" creatorversion=\"0.3.0\">\n"
		"  <head bpm=\"120\" timesig_numerator=\"4\" timesig_denominator=\"4\"/>\n"
		"  <song>\n"
		"    <trackcontainer type=\"song\">\n"
		"      <track name=\"Audio\" type=\"0\">\n"
		"        <instrumenttrack><instrument name=\"audiofileprocessor\">\n"
		"          <audiofileprocessor src=\"%1/sample.wav\" amp=\"100\"/></instrument>\n"
		"          <sampleclip pos=\"0\" len=\"192\" src=\"%1/clip.wav\"/>\n"
		"          <sampleclip pos=\"192\" len=\"192\" src=\"gone.wav\"/>\n"
		"        </instrumenttrack>\n"
		"        <instrumenttrack instrument=\"1\"><instrument name=\"sf2player\">\n"
		"          <sf2player src=\"%1/sound.sf2\" bank=\"0\" patch=\"0\"/></instrument>\n"
		"        </instrumenttrack>\n"
		"        <instrumenttrack instrument=\"2\"><instrument name=\"audiofileprocessor\">\n"
		"          <audiofileprocessor src=\"\" sampledata=\"AAAA\" amp=\"100\"/></instrument>\n"
		"        </instrumenttrack>\n"
		"      </track>\n"
		"    </trackcontainer>\n"
		"    <session><clips>\n"
		"      <clip track=\"0\" scene=\"0\" type=\"1\" src=\"%1/slot.wav\"/>\n"
		"    </clips></session>\n"
		"  </song>\n"
		"</zene-project>\n").arg(quoted);
	return xml.toUtf8();
}

/*! Writes the fixture project and (optionally) every file it references.
 *  Returns the project's path. */
QString writeFixture(const QString& dir, const QString& projectName, bool intact)
{
	QStringList names{QStringLiteral("sample.wav"), QStringLiteral("clip.wav"),
		QStringLiteral("sound.sf2"), QStringLiteral("slot.wav")};
	if (intact) { names.append(QStringLiteral("gone.wav")); }
	for (const QString& name : names) { writeBytes(dir + QLatin1Char('/') + name, fakeWave(32)); }
	const QString project = dir + QLatin1Char('/') + projectName;
	writeBytes(project, fixtureDocument(dir));
	return project;
}

//! The reference whose stored value is \a raw.
QJsonObject referenceWithRaw(const QJsonArray& references, const QString& raw)
{
	for (const QJsonValue& value : references)
	{
		if (value.toObject().value(QStringLiteral("raw")).toString() == raw) { return value.toObject(); }
	}
	return QJsonObject();
}


} // namespace

class ControlProjectArchiveTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}
	//! The group is on the wire: three ids, each with a schema, and the
	//! mutating flag that decides whether a transaction is recorded for it.
	void theThreeVerbsAreRegistered()
	{
		const ControlRegistry* registry = ControlRegistry::instance();
		for (const QString& id : archiveIds())
		{
			const ControlCommand* cmd = registry->command(id);
			QVERIFY2(cmd != nullptr, qPrintable(QStringLiteral("not registered: %1").arg(id)));
			QVERIFY2(!cmd->argsSchema.isEmpty(),
				qPrintable(QStringLiteral("%1 has no args schema").arg(id)));
			QVERIFY2(!cmd->resultSchema.isEmpty(),
				qPrintable(QStringLiteral("%1 has no result schema").arg(id)));
			QCOMPARE(cmd->group, QStringLiteral("project"));
			QVERIFY2(!cmd->description.isEmpty(),
				qPrintable(QStringLiteral("%1 has no description").arg(id)));
		}
		QCOMPARE(registry->command(QStringLiteral("project.missing_assets"))->mutating, false);
		QCOMPARE(registry->command(QStringLiteral("project.hash_assets"))->mutating, false);
		QCOMPARE(registry->command(QStringLiteral("project.relink"))->mutating, true);

		// The args contracts an agent reads before it calls: the two inspectors
		// take one path, the writer takes the reference and the file it found.
		const QJsonObject inspectors = registry->command(
			QStringLiteral("project.missing_assets"))->argsSchema;
		QCOMPARE(inspectors.value(QStringLiteral("required")).toArray(),
			QJsonArray{QStringLiteral("project")});
		const QJsonObject relink = registry->command(
			QStringLiteral("project.relink"))->argsSchema;
		QCOMPARE(relink.value(QStringLiteral("required")).toArray(),
			(QJsonArray{QStringLiteral("project"), QStringLiteral("from"), QStringLiteral("to")}));
	}
	//! SPEC A16: one row per command, in the table, with the reason and the
	//! mechanism filled in. relink is the true_inverse one, and its inverse is
	//! automatic - no manual fallback is needed, which the empty fallback says.
	void theContractRowsClassifyTheGroup()
	{
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		for (const QString& id : archiveIds())
		{
			const control::ReversibilityEntry* row = table.lookup(id);
			QVERIFY2(row != nullptr, qPrintable(QStringLiteral("no contract row for %1").arg(id)));
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
		}
		QCOMPARE(table.lookup(QStringLiteral("project.missing_assets"))->cls,
			control::ReversibilityClass::NotMutating);
		QCOMPARE(table.lookup(QStringLiteral("project.hash_assets"))->cls,
			control::ReversibilityClass::NotMutating);

		const control::ReversibilityEntry* relink = table.lookup(QStringLiteral("project.relink"));
		QCOMPARE(relink->cls, control::ReversibilityClass::TrueInverse);
		QCOMPARE(relink->reversible, true);
		QVERIFY2(relink->fallback.isEmpty(),
			"the relink's inverse is automatic (a recorded action checkpoint), so its row must "
			"not name a manual fallback");
	}
	//! THE NEGATIVE CONTROL. Every reference is on disk, so the missing list is
	//! EMPTY and the counts say so; the one inline element is counted separately
	//! and is not a reference.
	void anIntactProjectReportsNoMissingAssets()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = writeFixture(dir.path(), QStringLiteral("intact.mmp"), true);
		QVERIFY(QFileInfo::exists(project));

		const ControlResult scanned = run(QStringLiteral("project.missing_assets"),
			QJsonObject{{QStringLiteral("project"), project}});
		QVERIFY2(scanned.ok, qPrintable(scanned.errorMessage));

		QCOMPARE(scanned.result.value(QStringLiteral("missing")).toArray().size(), 0);
		QCOMPARE(scanned.result.value(QStringLiteral("missing_count")).toInt(), 0);
		QCOMPARE(scanned.result.value(QStringLiteral("reference_count")).toInt(), 5);
		QCOMPARE(scanned.result.value(QStringLiteral("present_count")).toInt(), 5);
		QCOMPARE(scanned.result.value(QStringLiteral("unresolved_count")).toInt(), 0);
		QCOMPARE(scanned.result.value(QStringLiteral("embedded_count")).toInt(), 1);
		QVERIFY(!scanned.result.value(QStringLiteral("digest")).toString().isEmpty());

		// The same control on the hash verb: five references, five hashes, and
		// every one of them equals the file's own sha256.
		const ControlResult hashed = run(QStringLiteral("project.hash_assets"),
			QJsonObject{{QStringLiteral("project"), project}});
		QVERIFY2(hashed.ok, qPrintable(hashed.errorMessage));
		QCOMPARE(hashed.result.value(QStringLiteral("hashed_count")).toInt(), 5);
		QCOMPARE(hashed.result.value(QStringLiteral("unhashed_present")).toInt(), 0);
		for (const QJsonValue& value : hashed.result.value(QStringLiteral("references")).toArray())
		{
			const QJsonObject ref = value.toObject();
			const QString path = ref.value(QStringLiteral("path")).toString();
			QCOMPARE(ref.value(QStringLiteral("sha256")).toString(), sha256Of(path));
		}
	}
	//! The positive half: the four files that are gone are named by the value
	//! the project STORES (so the caller can pass it straight back to relink),
	//! including the legacy relative one, which resolves against the project's
	//! own directory.
	void eachUnresolvableReferenceIsReportedWithItsStoredValue()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = writeFixture(dir.path(), QStringLiteral("broken.mmp"), false);

		const ControlResult scanned = run(QStringLiteral("project.missing_assets"),
			QJsonObject{{QStringLiteral("project"), project}});
		QVERIFY2(scanned.ok, qPrintable(scanned.errorMessage));

		QCOMPARE(scanned.result.value(QStringLiteral("reference_count")).toInt(), 5);
		QCOMPARE(scanned.result.value(QStringLiteral("missing_count")).toInt(), 1);
		QCOMPARE(scanned.result.value(QStringLiteral("missing")).toArray().size(), 1);

		const QJsonArray references = scanned.result.value(QStringLiteral("references")).toArray();
		QCOMPARE(references.size(), 5);
		const QJsonObject gone = referenceWithRaw(scanned.result.value(QStringLiteral("missing"))
			.toArray(), QStringLiteral("gone.wav"));
		QVERIFY2(!gone.isEmpty(), "the legacy relative reference was not reported");
		QCOMPARE(gone.value(QStringLiteral("tag")).toString(), QStringLiteral("sampleclip"));
		QCOMPARE(gone.value(QStringLiteral("resolved_via")).toString(), QStringLiteral("project-dir"));
		QCOMPARE(gone.value(QStringLiteral("exists")).toBool(), false);
		QCOMPARE(gone.value(QStringLiteral("path")).toString(),
			QDir::cleanPath(dir.path() + QStringLiteral("/gone.wav")));
		QVERIFY2(!gone.value(QStringLiteral("error")).toString().isEmpty(),
			"a missing reference must say why it is unusable");

		// The present ones are still reported, with their type and path: an agent
		// sees the whole reference set, not only the failures.
		QCOMPARE(referenceWithRaw(references, QStringLiteral("gone.wav"))
			.value(QStringLiteral("missing")).toBool(), true);
		QCOMPARE(referenceWithRaw(references, QStringLiteral("gone.wav"))
			.value(QStringLiteral("exists")).toBool(), false);
	}
	//! Hashing is the identity step: the digest is stable, and relinking
	//! changes it, because the digest covers the reference set and its hashes.
	void hashingIsStableAndChangesWithTheReferenceSet()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = writeFixture(dir.path(), QStringLiteral("digest.mmp"), true);

		const ControlResult first = run(QStringLiteral("project.hash_assets"),
			QJsonObject{{QStringLiteral("project"), project}});
		const ControlResult second = run(QStringLiteral("project.hash_assets"),
			QJsonObject{{QStringLiteral("project"), project}});
		QVERIFY(first.ok && second.ok);
		const QString digest = first.result.value(QStringLiteral("digest")).toString();
		QCOMPARE(second.result.value(QStringLiteral("digest")).toString(), digest);

		// A different file at the same reference changes the digest.
		const QString other = dir.path() + QStringLiteral("/other.wav");
		writeBytes(other, fakeWave(64));
		QVERIFY(run(QStringLiteral("project.relink"), QJsonObject{
			{QStringLiteral("project"), project},
			{QStringLiteral("from"), dir.path() + QStringLiteral("/clip.wav")},
			{QStringLiteral("to"), other}}).ok);
		const ControlResult after = run(QStringLiteral("project.hash_assets"),
			QJsonObject{{QStringLiteral("project"), project}});
		QVERIFY(after.ok);
		QVERIFY2(after.result.value(QStringLiteral("digest")).toString() != digest,
			"the digest did not move with the reference set");
	}
	//! The relink's inverse, for real: the file is rewritten (the reference
	//! resolves again, and a rescan says so), then ONE control.undo puts back the
	//! bytes that were there before - compared whole, not attribute by attribute.
	void relinkRepointsTheReferenceAndUndoRestoresTheFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = writeFixture(dir.path(), QStringLiteral("relink.mmp"), false);

		// The sample was found somewhere else: same name, another directory.
		const QString foundDir = dir.path() + QStringLiteral("/found");
		const QString found = foundDir + QStringLiteral("/gone.wav");
		QVERIFY(writeBytes(found, fakeWave(48)));
		const QString expectedHash = sha256Of(found);

		const QByteArray before = readBytes(project);
		QVERIFY(!before.isEmpty());

		const ControlResult relinked = run(QStringLiteral("project.relink"), QJsonObject{
			{QStringLiteral("project"), project},
			{QStringLiteral("from"), QStringLiteral("gone.wav")},
			{QStringLiteral("to"), found},
			{QStringLiteral("expect_sha256"), expectedHash}});
		QVERIFY2(relinked.ok, qPrintable(relinked.errorMessage));
		QCOMPARE(relinked.result.value(QStringLiteral("replaced")).toInt(), 1);
		QCOMPARE(relinked.result.value(QStringLiteral("before")).toArray().size(), 1);
		QCOMPARE(relinked.result.value(QStringLiteral("before")).toArray().first().toString(),
			QStringLiteral("gone.wav"));
		QCOMPARE(relinked.result.value(QStringLiteral("dry_run")).toBool(), false);

		// Measured: the reference resolves again, and the file on disk is not the
		// one that was there before.
		const ControlResult rescan = run(QStringLiteral("project.missing_assets"),
			QJsonObject{{QStringLiteral("project"), project}});
		QVERIFY(rescan.ok);
		QCOMPARE(rescan.result.value(QStringLiteral("missing_count")).toInt(), 0);
		QVERIFY(readBytes(project) != before);

		// ... and the recorded transaction is the relink's, reversible.
		QCOMPARE(stateOf(QStringLiteral("project.relink")).value(QStringLiteral("class")).toString(),
			QStringLiteral("true_inverse"));
		QCOMPARE(stateOf(QStringLiteral("project.relink")).value(QStringLiteral("reversible")).toBool(),
			true);

		REV_UNDO_OR_FAIL();
		QCOMPARE(readBytes(project), before);
	}
	//! A relink that cannot identify the media must refuse and write NOTHING.
	void relinkRefusesAMismatchedHash()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = writeFixture(dir.path(), QStringLiteral("mismatch.mmp"), false);
		const QByteArray before = readBytes(project);

		const ControlResult wrong = run(QStringLiteral("project.relink"), QJsonObject{
			{QStringLiteral("project"), project},
			{QStringLiteral("from"), QStringLiteral("gone.wav")},
			{QStringLiteral("to"), dir.path() + QStringLiteral("/sample.wav")},
			{QStringLiteral("expect_sha256"), QString(64, QLatin1Char('0'))}});
		QCOMPARE(wrong.ok, false);
		QCOMPARE(wrong.errorKind, ControlErrorKind::Refused);
		QCOMPARE(readBytes(project), before);

		// A reference the project does not carry is a typed not_found, and it
		// writes nothing either.
		const ControlResult absent = run(QStringLiteral("project.relink"), QJsonObject{
			{QStringLiteral("project"), project},
			{QStringLiteral("from"), QStringLiteral("not-referenced.wav")},
			{QStringLiteral("to"), dir.path() + QStringLiteral("/sample.wav")}});
		QCOMPARE(absent.ok, false);
		QCOMPARE(absent.errorKind, ControlErrorKind::NotFound);
		QCOMPARE(readBytes(project), before);
	}
	//! A dry run says what would change and leaves the file alone.
	void dryRunWritesNothing()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString project = writeFixture(dir.path(), QStringLiteral("dryrun.mmp"), true);
		const QByteArray before = readBytes(project);

		const ControlResult preview = run(QStringLiteral("project.relink"), QJsonObject{
			{QStringLiteral("project"), project},
			{QStringLiteral("from"), dir.path() + QStringLiteral("/clip.wav")},
			{QStringLiteral("to"), dir.path() + QStringLiteral("/sample.wav")},
			{QStringLiteral("dry_run"), true}});
		QVERIFY2(preview.ok, qPrintable(preview.errorMessage));
		QCOMPARE(preview.result.value(QStringLiteral("replaced")).toInt(), 1);
		QCOMPARE(preview.result.value(QStringLiteral("dry_run")).toBool(), true);
		QCOMPARE(readBytes(project), before);
	}
	//! An .mmpz project is rewritten AS .mmpz: the bytes it leaves in still
	//! inflate, and the reference they carry moved. (Writing the plain XML back
	//! into a .mmpz is the defect this proves absent.)
	void relinkKeepsTheCompressedFormat()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString plain = writeFixture(dir.path(), QStringLiteral("plain.mmp"), false);
		const QString packed = dir.path() + QStringLiteral("/packed.mmpz");
		QVERIFY(writeBytes(packed, qCompress(readBytes(plain))));
		const ControlResult relinked = run(QStringLiteral("project.relink"), QJsonObject{
			{QStringLiteral("project"), packed},
			{QStringLiteral("from"), QStringLiteral("gone.wav")},
			{QStringLiteral("to"), dir.path() + QStringLiteral("/sample.wav")}});
		QVERIFY2(relinked.ok, qPrintable(relinked.errorMessage));
		// Measured: the file still inflates, and what it says moved.
		const QString inflated = QString::fromUtf8(qUncompress(readBytes(packed)));
		QVERIFY(!inflated.isEmpty());
		QVERIFY(inflated.contains(QStringLiteral("sample.wav")));
		QVERIFY(!inflated.contains(QStringLiteral("gone.wav")));
	}

	//! Junk arguments are typed refusals, never a crash: this is the shape the
	//! agent-surface sweep and a confused client both need.
	void junkArgumentsAreTypedRefusals()
	{
		const ControlResult noProject = run(QStringLiteral("project.missing_assets"),
			QJsonObject{{QStringLiteral("project"), QString()}});
		QCOMPARE(noProject.ok, false);
		QCOMPARE(noProject.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult notAProject = run(QStringLiteral("project.missing_assets"),
			QJsonObject{{QStringLiteral("project"), QStringLiteral("/nonexistent-project.mmp")}});
		QCOMPARE(notAProject.ok, false);
		QCOMPARE(notAProject.errorKind, ControlErrorKind::NotFound);

		const ControlResult noFrom = run(QStringLiteral("project.relink"), QJsonObject{
			{QStringLiteral("project"), QStringLiteral("/nonexistent-project.mmp")},
			{QStringLiteral("from"), QString()},
			{QStringLiteral("to"), QStringLiteral("/nonexistent.wav")}});
		QCOMPARE(noFrom.ok, false);
		QCOMPARE(noFrom.errorKind, ControlErrorKind::InvalidArgs);

		// ...and the SAME junk arguments with dry_run: true, where nothing is read at all -
		// the fact that decided the check's order: a preview flag must not change it.
		const ControlResult noFromDry = run(QStringLiteral("project.relink"), QJsonObject{
			{QStringLiteral("project"), QStringLiteral("/nonexistent-project.mmp")},
			{QStringLiteral("from"), QString()}, {QStringLiteral("to"), QStringLiteral("/nonexistent.wav")},
			{QStringLiteral("dry_run"), true}});
		QCOMPARE(noFromDry.errorKind, ControlErrorKind::InvalidArgs);
	}
};

QTEST_MAIN(ControlProjectArchiveTest)
#include "ControlProjectArchiveTest.moc"
