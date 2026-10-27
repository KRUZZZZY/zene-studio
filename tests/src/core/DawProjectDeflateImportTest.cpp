/*
 * DawProjectDeflateImportTest.cpp - the DAWproject container reads DEFLATE and reports every deviation.
 *
 * Queue unit Q050-001 (docs/050-features/Q050-001.md). Two claims, each proven on containers built
 * byte by byte in this file (so the checks are independent of the writer under test):
 *
 *  1. A STORE container and a DEFLATE container holding the same entries import to the SAME project
 *     state, with the decompression bounded (a per-entry and a total limit, enforced on the bytes
 *     actually produced).
 *  2. A corrupt, oversized, unsupported, CRC-failing or truncated entry and a missing referenced file
 *     are each LISTED in the reply (`deviations`), nothing crashes, and the readable content still
 *     imports; when project.xml itself is unusable the refusal lists every deviation too.
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

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <vector>

#include "ControlRegistry.h"
#include "DawProjectInterchange.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::interchange;

namespace
{

//! One entry of a hand-built ZIP: what the directory says and what the body really is.
struct Spec
{
	QString name;
	quint16 method = 0;
	QByteArray body;           //!< the bytes stored after the local header
	quint32 crc = 0;           //!< what the directory declares
	quint32 uncompressed = 0;  //!< what the directory declares
	quint16 flags = 0;
	bool dataDescriptor = false;   //!< sizes and CRC zero in the local header, a descriptor after the body
	bool brokenLocalHeader = false; //!< the signature of the local header is wrong
};

//! The CRC-32 of \a bytes, computed bit by bit here so it does not share code with the reader.
quint32 crcOf(const QByteArray& bytes)
{
	quint32 crc = 0xFFFFFFFFu;
	for (char byte : bytes)
	{
		crc ^= static_cast<unsigned char>(byte);
		for (int bit = 0; bit < 8; bit++) { crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1); }
	}
	return crc ^ 0xFFFFFFFFu;
}

//! A raw RFC 1951 stream of \a data: qCompress' zlib frame without its 2-byte header and 4-byte trailer.
QByteArray rawDeflate(const QByteArray& data)
{
	const QByteArray framed = qCompress(data, 9);
	return framed.mid(6, framed.size() - 10);
}

Spec stored(const QString& name, const QByteArray& data)
{
	Spec spec;
	spec.name = name;
	spec.body = data;
	spec.crc = crcOf(data);
	spec.uncompressed = static_cast<quint32>(data.size());
	return spec;
}

Spec deflated(const QString& name, const QByteArray& data)
{
	Spec spec = stored(name, data);
	spec.method = 8;
	spec.body = rawDeflate(data);
	return spec;
}

void u16(QByteArray& out, quint16 value)
{
	out.append(static_cast<char>(value & 0xFF));
	out.append(static_cast<char>(value >> 8));
}

void u32(QByteArray& out, quint32 value)
{
	for (int shift : {0, 8, 16, 24}) { out.append(static_cast<char>((value >> shift) & 0xFF)); }
}

QByteArray buildZip(const std::vector<Spec>& specs, int declaredCountOverride = -1)
{
	QByteArray file;
	std::vector<quint32> offsets;
	for (const Spec& spec : specs)
	{
		offsets.push_back(static_cast<quint32>(file.size()));
		const QByteArray name = spec.name.toUtf8();
		const quint16 flags = spec.flags | (spec.dataDescriptor ? 0x0008 : 0);
		u32(file, spec.brokenLocalHeader ? 0xDEADBEEFu : 0x04034B50u);
		u16(file, 20);
		u16(file, flags);
		u16(file, spec.method);
		u16(file, 0);
		u16(file, 0x21);
		u32(file, spec.dataDescriptor ? 0 : spec.crc);
		u32(file, spec.dataDescriptor ? 0 : static_cast<quint32>(spec.body.size()));
		u32(file, spec.dataDescriptor ? 0 : spec.uncompressed);
		u16(file, static_cast<quint16>(name.size()));
		u16(file, 0);
		file.append(name);
		file.append(spec.body);
		if (spec.dataDescriptor)
		{
			u32(file, 0x08074B50u);
			u32(file, spec.crc);
			u32(file, static_cast<quint32>(spec.body.size()));
			u32(file, spec.uncompressed);
		}
	}
	const quint32 centralOffset = static_cast<quint32>(file.size());
	for (std::size_t index = 0; index < specs.size(); index++)
	{
		const Spec& spec = specs[index];
		const QByteArray name = spec.name.toUtf8();
		u32(file, 0x02014B50u);
		u16(file, 20);
		u16(file, 20);
		u16(file, spec.flags | (spec.dataDescriptor ? 0x0008 : 0));
		u16(file, spec.method);
		u16(file, 0);
		u16(file, 0x21);
		u32(file, spec.crc);
		u32(file, static_cast<quint32>(spec.body.size()));
		u32(file, spec.uncompressed);
		u16(file, static_cast<quint16>(name.size()));
		u16(file, 0);
		u16(file, 0);
		u16(file, 0);
		u16(file, 0);
		u32(file, 0);
		u32(file, offsets[index]);
		file.append(name);
	}
	const quint32 centralSize = static_cast<quint32>(file.size()) - centralOffset;
	const quint16 declared = static_cast<quint16>(declaredCountOverride >= 0
		? declaredCountOverride : static_cast<int>(specs.size()));
	u32(file, 0x06054B50u);
	u16(file, 0);
	u16(file, 0);
	u16(file, declared);
	u16(file, declared);
	u32(file, centralSize);
	u32(file, centralOffset);
	u16(file, 0);
	return file;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

//! A model small enough to read, with every entity the importer carries.
DawProjectModel sampleModel()
{
	DawProjectModel model;
	model.applicationName = QStringLiteral("Foreign DAW");
	model.applicationVersion = QStringLiteral("9.9");
	model.tempo = 133.0;
	DawProjectMixerChannel master;
	master.id = QStringLiteral("mixer0");
	master.name = QStringLiteral("Master");
	master.role = QStringLiteral("master");
	master.index = 0;
	model.mixerChannels.append(master);
	DawProjectTrack track;
	track.id = QStringLiteral("track0");
	track.name = QStringLiteral("Bass");
	track.contentType = QStringLiteral("notes");
	track.typeName = QStringLiteral("instrument");
	track.channelId = QStringLiteral("strip1");
	track.destinationChannelId = QStringLiteral("mixer0");
	track.mixerChannelIndex = 0;
	DawProjectClip clip;
	clip.duration = 4.0;
	clip.name = QStringLiteral("Intro");
	for (int index = 0; index < 3; index++)
	{
		DawProjectNote note;
		note.time = index * 0.5;
		note.key = 60 + index;
		clip.notes.append(note);
	}
	track.clips.append(clip);
	model.tracks.append(track);
	DawProjectPoint tempo;
	tempo.value = 133.0;
	model.tempoPoints.append(tempo);
	return model;
}

inline ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

QStringList kindsOf(const QJsonArray& deviations)
{
	QStringList kinds;
	for (const QJsonValue& value : deviations)
	{
		kinds << QStringLiteral("%1|%2").arg(value.toObject().value(QStringLiteral("kind")).toString(),
			value.toObject().value(QStringLiteral("entry")).toString());
	}
	kinds.sort();
	return kinds;
}

} // namespace

class DawProjectDeflateImportTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_directory;
	QByteArray m_projectXml;
	QByteArray m_metaData = QByteArray("<MetaData><Title>Deflated song</Title></MetaData>");

	QString file(const QString& name) { return m_directory.filePath(name); }

	QString containerOf(const QString& name, const std::vector<Spec>& specs, int declaredCount = -1)
	{
		const QString path = file(name);
		const bool wrote = writeFile(path, buildZip(specs, declaredCount));
		Q_ASSERT(wrote);
		Q_UNUSED(wrote)
		return path;
	}

	//! The importer's view of the session, as the digest of the model the engine now describes.
	static QString sessionDigest()
	{
		return dawProjectModelDigest(dawProjectModelFromSong(Engine::getSong(), nullptr));
	}

	//! project.xml with `<File>` references added: one present, one missing, one external.
	QByteArray projectXmlWithReferences() const
	{
		QByteArray xml = m_projectXml;
		const QByteArray resources = "<Resources>"
			"<Audio><File path=\"audio/garbage.wav\"/></Audio>"
			"<Audio><File path=\"audio/missing.wav\"/></Audio>"
			"<Audio><File path=\"https://example.invalid/loop.wav\" external=\"true\"/></Audio>"
			"</Resources></Project>";
		xml.replace(QByteArray("</Project>"), resources);
		return xml;
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_directory.isValid());
		const QString seed = file(QStringLiteral("seed.dawproject"));
		QString error;
		QVERIFY2(writeDawProject(seed, sampleModel(), nullptr, &error), qPrintable(error));
		std::vector<DawProjectEntry> entries;
		QVERIFY2(dawProjectZipRead(seed, &entries, &error), qPrintable(error));
		for (const DawProjectEntry& entry : entries)
		{
			if (entry.first == QLatin1String("project.xml")) { m_projectXml = entry.second; }
		}
		QVERIFY(m_projectXml.contains("</Project>"));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		ControlRegistry::instance()->clearTransactions();
	}

	//! Acceptance 1: STORE and DEFLATE fixtures of the same entries give the same project state.
	void storeAndDeflateImportToTheSameProjectState()
	{
		const QString storePath = containerOf(QStringLiteral("store.dawproject"),
			{stored(QStringLiteral("metadata.xml"), m_metaData), stored(QStringLiteral("project.xml"), m_projectXml)});
		const QString deflatePath = containerOf(QStringLiteral("deflate.dawproject"),
			{deflated(QStringLiteral("metadata.xml"), m_metaData), deflated(QStringLiteral("project.xml"), m_projectXml)});
		// The deflate fixture really is smaller than what it holds, or this proves nothing.
		QVERIFY(QFile(deflatePath).size() < QFile(storePath).size());

		const ControlResult viaStore = run(QStringLiteral("dawproject.import"), {{QStringLiteral("path"), storePath}});
		QVERIFY2(viaStore.ok, qPrintable(viaStore.errorMessage));
		const QString storeState = sessionDigest();
		QCOMPARE(viaStore.result.value(QStringLiteral("deflated_entry_count")).toInt(), 0);

		Engine::getSong()->clearProject();
		const ControlResult viaDeflate = run(QStringLiteral("dawproject.import"), {{QStringLiteral("path"), deflatePath}});
		QVERIFY2(viaDeflate.ok, qPrintable(viaDeflate.errorMessage));
		QCOMPARE(viaDeflate.result.value(QStringLiteral("deflated_entry_count")).toInt(), 2);
		QCOMPARE(sessionDigest(), storeState);
		QVERIFY(storeState.contains(QStringLiteral("Bass")));

		for (const ControlResult& result : {viaStore, viaDeflate})
		{
			QCOMPARE(result.result.value(QStringLiteral("deviation_count")).toInt(), 0);
			QVERIFY(result.result.value(QStringLiteral("deviations")).toArray().isEmpty());
			QCOMPARE(result.result.value(QStringLiteral("track_count")).toInt(), 1);
			QCOMPARE(result.result.value(QStringLiteral("note_count")).toInt(), 3);
			QCOMPARE(result.result.value(QStringLiteral("tempo")).toDouble(), 133.0);
		}
		QCOMPARE(viaStore.result.value(QStringLiteral("model_digest")).toString(),
			viaDeflate.result.value(QStringLiteral("model_digest")).toString());

		// dawproject.read agrees on the whole model, and on the metadata title read out of a deflated entry.
		const ControlResult readStore = run(QStringLiteral("dawproject.read"), {{QStringLiteral("path"), storePath}});
		const ControlResult readDeflate = run(QStringLiteral("dawproject.read"), {{QStringLiteral("path"), deflatePath}});
		QVERIFY2(readStore.ok && readDeflate.ok, qPrintable(readDeflate.errorMessage));
		QCOMPARE(readDeflate.result.value(QStringLiteral("model")), readStore.result.value(QStringLiteral("model")));
		QCOMPARE(readDeflate.result.value(QStringLiteral("title")).toString(), QStringLiteral("Deflated song"));
	}

	//! The streaming writers' shape: sizes zero in the local header, a data descriptor after the body.
	void deflatedEntriesWithDataDescriptorsRead()
	{
		Spec project = deflated(QStringLiteral("project.xml"), m_projectXml);
		project.dataDescriptor = true;
		const QString path = containerOf(QStringLiteral("descriptor.dawproject"), {project});
		const ControlResult result = run(QStringLiteral("dawproject.import"), {{QStringLiteral("path"), path}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("note_count")).toInt(), 3);
		QCOMPARE(result.result.value(QStringLiteral("deviation_count")).toInt(), 0);
	}

	//! Acceptance 2: every deviation is listed, the rest imports, nothing is silently dropped.
	void everyDeviationIsListedAndTheReadableContentStillImports()
	{
		const QByteArray audio("RIFF....WAVE-data-of-a-sample");
		Spec garbage = deflated(QStringLiteral("audio/garbage.wav"), audio);
		garbage.body = QByteArray(40, '\xFF');  // 0xFF opens a reserved block type: not a DEFLATE stream
		Spec cut = deflated(QStringLiteral("audio/cut.wav"), QByteArray(100000, 'x') + QByteArray::number(1234567));
		cut.body = cut.body.left(cut.body.size() / 2);
		Spec flipped = deflated(QStringLiteral("audio/flipped.wav"), audio);
		flipped.crc ^= 0x1;
		Spec badMetaData = stored(QStringLiteral("metadata.xml"), m_metaData);
		badMetaData.crc ^= 0xFF;
		Spec bzip = stored(QStringLiteral("audio/bz.wav"), audio);
		bzip.method = 12;
		Spec encrypted = stored(QStringLiteral("audio/enc.wav"), audio);
		encrypted.flags = 0x0001;
		Spec huge = deflated(QStringLiteral("audio/huge.wav"), audio);
		huge.uncompressed = 600u * 1024u * 1024u;  // over the 512 MiB per-entry bound
		Spec orphan = stored(QStringLiteral("audio/nohdr.wav"), audio);
		orphan.brokenLocalHeader = true;

		const QString path = containerOf(QStringLiteral("many-deviations.dawproject"), {
			deflated(QStringLiteral("project.xml"), projectXmlWithReferences()), badMetaData,
			garbage, cut, flipped, bzip, encrypted, huge, orphan,
			deflated(QStringLiteral("audio/fine.wav"), audio)});

		// The clean import first, so "the same state" is measured rather than assumed.
		const QString clean = containerOf(QStringLiteral("clean.dawproject"),
			{deflated(QStringLiteral("project.xml"), m_projectXml)});
		QVERIFY(run(QStringLiteral("dawproject.import"), {{QStringLiteral("path"), clean}}).ok);
		const QString cleanState = sessionDigest();
		Engine::getSong()->clearProject();

		const ControlResult result = run(QStringLiteral("dawproject.import"), {{QStringLiteral("path"), path}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(sessionDigest(), cleanState);

		const QStringList expected = {
			QStringLiteral("corrupt_entry|audio/cut.wav"),
			QStringLiteral("corrupt_entry|audio/garbage.wav"),
			QStringLiteral("corrupt_entry|audio/nohdr.wav"),
			QStringLiteral("crc_mismatch|audio/flipped.wav"),
			QStringLiteral("crc_mismatch|metadata.xml"),
			QStringLiteral("missing_referenced_file|audio/missing.wav"),
			QStringLiteral("oversized_entry|audio/huge.wav"),
			QStringLiteral("unsupported_feature|audio/enc.wav"),
			QStringLiteral("unsupported_method|audio/bz.wav"),
		};
		const QJsonArray deviations = result.result.value(QStringLiteral("deviations")).toArray();
		QCOMPARE(kindsOf(deviations), expected);
		QCOMPARE(result.result.value(QStringLiteral("deviation_count")).toInt(), expected.size());
		QCOMPARE(result.result.value(QStringLiteral("entry_count")).toInt(), 10);
		QCOMPARE(result.result.value(QStringLiteral("referenced_file_count")).toInt(), 2);  // external is not counted
		for (const QJsonValue& value : deviations)
		{
			QVERIFY(!value.toObject().value(QStringLiteral("detail")).toString().isEmpty());
		}
		// The same list through dawproject.read, which does not touch the session.
		const ControlResult read = run(QStringLiteral("dawproject.read"), {{QStringLiteral("path"), path}});
		QVERIFY2(read.ok, qPrintable(read.errorMessage));
		QCOMPARE(kindsOf(read.result.value(QStringLiteral("deviations")).toArray()), expected);
		// An entry that could not be read is still named among the entries the importer did not use.
		QStringList unused;
		for (const QJsonValue& value : read.result.value(QStringLiteral("unused_entries")).toArray()) { unused << value.toString(); }
		QVERIFY(unused.contains(QStringLiteral("audio/cut.wav")));
		QVERIFY(!read.result.value(QStringLiteral("has_metadata")).toBool());  // its CRC failed: not trusted
	}

	//! A damaged project.xml leaves nothing to import: a typed refusal that still lists every deviation.
	void anUnusableProjectEntryRefusesAndListsEverything()
	{
		Spec project = deflated(QStringLiteral("project.xml"), m_projectXml);
		project.body = project.body.left(project.body.size() / 2);
		Spec bzip = stored(QStringLiteral("audio/bz.wav"), QByteArray("abc"));
		bzip.method = 12;
		const QString path = containerOf(QStringLiteral("no-project.dawproject"), {project, bzip});
		const int tracksBefore = Engine::getSong()->tracks().size();

		const ControlResult result = run(QStringLiteral("dawproject.import"), {{QStringLiteral("path"), path}});
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(result.errorMessage.contains(QStringLiteral("project.xml")), qPrintable(result.errorMessage));
		QVERIFY2(result.errorMessage.contains(QStringLiteral("truncated")), qPrintable(result.errorMessage));
		QVERIFY2(result.errorMessage.contains(QStringLiteral("audio/bz.wav")), qPrintable(result.errorMessage));
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore);  // a refusal writes nothing

		const QString noEntry = containerOf(QStringLiteral("no-entry.dawproject"), {bzip});
		const ControlResult absent = run(QStringLiteral("dawproject.import"), {{QStringLiteral("path"), noEntry}});
		QVERIFY(!absent.ok);
		QVERIFY2(absent.errorMessage.contains(QStringLiteral("no 'project.xml' entry")), qPrintable(absent.errorMessage));
		QVERIFY2(absent.errorMessage.contains(QStringLiteral("audio/bz.wav")), qPrintable(absent.errorMessage));
	}

	//! The bound holds on the bytes produced: honest bombs, lying bombs and a many-entry total.
	void decompressionIsBoundedPerEntryAndInTotal()
	{
		DawProjectZipLimits limits;
		limits.maxEntryBytes = 1024 * 1024;
		limits.maxTotalBytes = 1024 * 1024;

		// An honest bomb: 16 MiB of zeros in a few kilobytes, declared as such. Refused on its declaration;
		// nothing is inflated (inflatedBytes stays 0).
		const QByteArray zeros(16 * 1024 * 1024, '\0');
		Spec bomb = deflated(QStringLiteral("bomb.bin"), zeros);
		QVERIFY2(bomb.body.size() < 64 * 1024, "the fixture is not actually a high-ratio bomb");
		// A lying bomb: really 4 MiB, declared 1000 bytes. Decoding stops at the declared size.
		Spec liar = deflated(QStringLiteral("liar.bin"), QByteArray(4 * 1024 * 1024, '\0'));
		liar.uncompressed = 1000;
		// Three entries of 400 KB: the first two fit the 1 MiB total, the third would pass it.
		const QByteArray block(400 * 1024, 'q');
		const QString path = containerOf(QStringLiteral("bounded.dawproject"), {
			bomb, liar, deflated(QStringLiteral("a.bin"), block), deflated(QStringLiteral("b.bin"), block),
			deflated(QStringLiteral("c.bin"), block)});

		std::vector<DawProjectEntry> entries;
		DawProjectZipReport report;
		QString error;
		QVERIFY2(dawProjectZipReadTolerant(path, &entries, &report, &error, limits), qPrintable(error));
		QCOMPARE(entries.size(), static_cast<std::size_t>(2));
		QCOMPARE(entries[0].first, QStringLiteral("a.bin"));
		QCOMPARE(entries[1].second, block);
		QCOMPARE(report.inflatedBytes, static_cast<qint64>(2 * block.size()));
		QVERIFY(report.inflatedBytes <= limits.maxTotalBytes);
		QStringList kinds;
		for (const DawProjectDeviation& deviation : report.deviations)
		{
			kinds << deviation.kind + QLatin1Char('|') + deviation.entry;
		}
		kinds.sort();
		QCOMPARE(kinds, (QStringList{QStringLiteral("corrupt_entry|liar.bin"),
			QStringLiteral("oversized_entry|bomb.bin"), QStringLiteral("total_oversized|c.bin")}));
		for (const DawProjectDeviation& deviation : report.deviations)
		{
			if (deviation.entry == QLatin1String("liar.bin"))
			{
				QVERIFY2(deviation.detail.contains(QStringLiteral("inflates past")), qPrintable(deviation.detail));
			}
		}
	}

	//! The total bound counts every byte decoded, not only the bytes of entries that turn out good: entries that
	//! fail their CRC, or break mid-stream, cannot be inflated past it in aggregate.
	void failingEntriesStillCountAgainstTheTotalBound()
	{
		DawProjectZipLimits limits;
		limits.maxEntryBytes = 1024 * 1024;
		limits.maxTotalBytes = 1024 * 1024;
		const QByteArray block(400 * 1024, 'q');  // 409600 bytes each; two fit the total, a third cannot

		// Eight entries that all FAIL their CRC after inflating in full.
		std::vector<Spec> badCrc;
		for (int index = 0; index < 8; index++)
		{
			Spec spec = deflated(QStringLiteral("crc%1.bin").arg(index), block);
			spec.crc ^= 0x5;
			badCrc.push_back(spec);
		}
		std::vector<DawProjectEntry> entries;
		DawProjectZipReport report;
		QString error;
		QVERIFY2(dawProjectZipReadTolerant(containerOf(QStringLiteral("bad-crc.dawproject"), badCrc), &entries,
			&report, &error, limits), qPrintable(error));
		QVERIFY(entries.empty());
		QCOMPARE(report.inflatedBytes, static_cast<qint64>(2 * block.size()));  // two inflated, the rest refused unread
		QVERIFY(report.inflatedBytes <= limits.maxTotalBytes);
		int crc = 0;
		int total = 0;
		for (const DawProjectDeviation& deviation : report.deviations)
		{
			crc += deviation.kind == QLatin1String("crc_mismatch");
			total += deviation.kind == QLatin1String("total_oversized");
		}
		QCOMPARE(crc, 2);
		QCOMPARE(total, 6);

		// Eight entries whose streams are cut off mid-way: the bytes they did produce are counted too.
		std::vector<Spec> cut;
		for (int index = 0; index < 8; index++)
		{
			Spec spec = deflated(QStringLiteral("cut%1.bin").arg(index), block);
			spec.body = spec.body.left(spec.body.size() * 3 / 4);
			cut.push_back(spec);
		}
		QVERIFY2(dawProjectZipReadTolerant(containerOf(QStringLiteral("cut.dawproject"), cut), &entries, &report,
			&error, limits), qPrintable(error));
		QVERIFY(entries.empty());
		QVERIFY(report.inflatedBytes <= limits.maxTotalBytes);
		QVERIFY(report.inflatedBytes > 0);
		int corrupt = 0;
		total = 0;
		for (const DawProjectDeviation& deviation : report.deviations)
		{
			corrupt += deviation.kind == QLatin1String("corrupt_entry");
			total += deviation.kind == QLatin1String("total_oversized");
		}
		QVERIFY2(total > 0, "truncated entries never exhausted the total bound");
		QCOMPARE(corrupt + total, 8);  // every entry is accounted for, none silently dropped
	}

	//! `retain` drops the bytes of the other entries but still checks them.
	void retainedSubsetStillChecksTheOtherEntries()
	{
		Spec damaged = stored(QStringLiteral("other.bin"), QByteArray("0123456789"));
		damaged.crc ^= 1;
		const QString path = containerOf(QStringLiteral("retain.dawproject"),
			{stored(QStringLiteral("keep.bin"), QByteArray("kept")), damaged,
			deflated(QStringLiteral("drop.bin"), QByteArray(5000, 'd'))});
		std::vector<DawProjectEntry> entries;
		DawProjectZipReport report;
		QString error;
		QVERIFY2(dawProjectZipReadTolerant(path, &entries, &report, &error, DawProjectZipLimits(),
			QStringList{QStringLiteral("keep.bin")}), qPrintable(error));
		QCOMPARE(entries.size(), static_cast<std::size_t>(1));
		QCOMPARE(report.entriesRead, 2);  // keep.bin and drop.bin decoded; other.bin failed its CRC
		QCOMPARE(report.entriesDeflated, 1);
		QCOMPARE(report.deviations.size(), static_cast<std::size_t>(1));
		QCOMPARE(report.deviations[0].kind, QStringLiteral("crc_mismatch"));
		QCOMPARE(report.entryNames.size(), 3);
	}

	//! A directory that ends early reports it and still returns the entries it did reach; a non-ZIP is refused.
	void aTruncatedDirectoryIsListedAndANonZipIsRefused()
	{
		const QString path = containerOf(QStringLiteral("short-directory.dawproject"),
			{stored(QStringLiteral("one.bin"), QByteArray("1")), stored(QStringLiteral("two.bin"), QByteArray("2"))}, 3);
		std::vector<DawProjectEntry> entries;
		DawProjectZipReport report;
		QString error;
		QVERIFY2(dawProjectZipReadTolerant(path, &entries, &report, &error), qPrintable(error));
		QCOMPARE(entries.size(), static_cast<std::size_t>(2));
		QCOMPARE(report.deviations.size(), static_cast<std::size_t>(1));
		QCOMPARE(report.deviations[0].kind, QStringLiteral("truncated_directory"));

		const QString notZip = file(QStringLiteral("not-a-zip.dawproject"));
		QVERIFY(writeFile(notZip, QByteArray(200, 'x')));
		QVERIFY(!dawProjectZipReadTolerant(notZip, &entries, &report, &error));
		QVERIFY2(error.contains(QStringLiteral("not a ZIP")), qPrintable(error));
		const ControlResult refused = run(QStringLiteral("dawproject.import"), {{QStringLiteral("path"), notZip}});
		QVERIFY(!refused.ok);
	}

	//! The strict reader (the .mmpz v2 container's) keeps its STORE-only contract and refuses by name.
	void theStrictReaderStaysStoreOnly()
	{
		const QString storePath = containerOf(QStringLiteral("strict-store.dawproject"),
			{stored(QStringLiteral("project.xml"), m_projectXml)});
		const QString deflatePath = containerOf(QStringLiteral("strict-deflate.dawproject"),
			{deflated(QStringLiteral("project.xml"), m_projectXml)});
		std::vector<DawProjectEntry> entries;
		QString error;
		QVERIFY2(dawProjectZipRead(storePath, &entries, &error), qPrintable(error));
		QCOMPARE(entries.size(), static_cast<std::size_t>(1));
		QVERIFY(!dawProjectZipRead(deflatePath, &entries, &error));
		QVERIFY2(error.contains(QStringLiteral("DEFLATE")) && error.contains(QStringLiteral("project.xml")),
			qPrintable(error));
	}

	//! A read that gives back nothing is refused AS A READ by both readers, so an
	//! import or a container recovery names the file it could not read. The same
	//! file read without the fault is a valid import. The fault is ZENE_TEST_CONTAINER_READ_FAULT.
	void aFailedReadIsNamedAsARead()
	{
		const QString path = containerOf(QStringLiteral("read-fault.dawproject"),
			{stored(QStringLiteral("project.xml"), m_projectXml)});
		std::vector<DawProjectEntry> entries;
		DawProjectZipReport report;
		QString error;
		QVERIFY2(dawProjectZipReadTolerant(path, &entries, &report, &error), qPrintable(error));

		qputenv("ZENE_TEST_CONTAINER_READ_FAULT", "1");
		const bool tolerant = dawProjectZipReadTolerant(path, &entries, &report, &error);
		const bool strict = dawProjectZipRead(path, &entries, &error);
		qunsetenv("ZENE_TEST_CONTAINER_READ_FAULT");
		QVERIFY(!tolerant);
		QVERIFY2(error.contains(QStringLiteral("cannot read")), qPrintable(error));
		QVERIFY(!strict);
		QVERIFY2(error.contains(QStringLiteral("cannot read")), qPrintable(error));
	}
};

QTEST_GUILESS_MAIN(DawProjectDeflateImportTest)
#include "DawProjectDeflateImportTest.moc"
