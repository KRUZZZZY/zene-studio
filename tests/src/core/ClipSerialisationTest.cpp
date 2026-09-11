/*
 * ClipSerialisationTest.cpp - Slice 1 of the clip-and-capture wave (task #611):
 *                             the clip's authored source window survives a save.
 *
 * Copyright (c) 2026 Zene Studio developers
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

// A window that does not survive a save is worthless (docs/CLIP-CAPTURE-DESIGN.md
// §§2.6, 4.1). Slice 1 therefore adds two additive attributes to <sampleclip> -
// `srcin`/`srcout`, in source frames - plus the `autoresize="0"` dependency §2.6
// names, and this file holds the round trip.
//
// I9 (additive serialisation) is the other half: a clip with no trim writes
// exactly the attribute set it wrote before this slice. That is asserted at the
// element level and at the project level.
//
// Note on byte comparison: a Clip serialises a <journallingObject id="..."/>
// child, and that id is per object, so two distinct clips can never be compared
// byte for byte (which is why the SlideNotesTest pattern, where a Note has no
// such child, cannot be copied verbatim). The comparisons below neutralise the
// id and then compare byte for byte, which is the strongest available form.

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QDomNode>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

#include "Engine.h"
#include "LmmsTypes.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 44100;
constexpr int kSeconds = 4;

//! The attribute set SampleClip::saveSettings wrote before this slice, sorted the
//! way attributeNames() sorts. `srcin`/`srcout` are deliberately absent.
QStringList upstreamAttributeNames()
{
	return QStringList{ "autoresize", "len", "muted", "off", "pos", "sample_rate", "src" };
}

QStringList attributeNames(const QDomElement& element)
{
	QStringList names;
	const QDomNamedNodeMap attributes = element.attributes();
	for (int i = 0; i < attributes.length(); ++i)
	{
		names << attributes.item(i).nodeName();
	}
	names.sort();
	return names;
}

QString nodeToString(const QDomNode& node)
{
	QString out;
	QTextStream stream(&out);
	node.save(stream, 2);
	stream.flush();
	return out;
}

//! A serialised node without the per-object journalling id, which no two objects
//! can share: what is left is the authored state.
QString withoutJournallingIds(const QString& text)
{
	static const QRegularExpression idPattern("id=\"[0-9]+\"");
	QString out = text;
	out.replace(idPattern, "id=\"N\"");
	return out;
}

//! The attributes of the first element with `name`, as a string map.
QMap<QString, QString> attributesOf(const QString& xml, const QString& name)
{
	QDomDocument doc;
	if (!doc.setContent(xml.toUtf8())) { return {}; }
	const QDomNodeList elements = doc.elementsByTagName(name);
	if (elements.length() == 0) { return {}; }
	const QDomNamedNodeMap attributes = elements.item(0).toElement().attributes();
	QMap<QString, QString> out;
	for (int i = 0; i < attributes.length(); ++i)
	{
		out.insert(attributes.item(i).nodeName(), attributes.item(i).nodeValue());
	}
	return out;
}

QString readWholeFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	return QString::fromUtf8(file.readAll());
}

/*! A real 16-bit mono WAV on disk.
 *
 *  SampleClip::loadSettings() only accepts a `src` whose file exists, so a round
 *  trip through the serialiser needs one.
 */
QString writeToneWav(const QString& path)
{
	const auto frames = static_cast<int>(kRate) * kSeconds;
	QByteArray pcm;
	pcm.reserve(frames * 2);
	for (int i = 0; i < frames; ++i)
	{
		const auto value = static_cast<qint16>(0.5 * 32767.0 * std::sin(2.0 * kPi * 440.0 * i / kRate));
		pcm.append(static_cast<char>(value & 0xff));
		pcm.append(static_cast<char>((value >> 8) & 0xff));
	}

	QByteArray out;
	const auto put32 = [&out](quint32 v) {
		out.append(static_cast<char>(v & 0xff));
		out.append(static_cast<char>((v >> 8) & 0xff));
		out.append(static_cast<char>((v >> 16) & 0xff));
		out.append(static_cast<char>((v >> 24) & 0xff));
	};
	const auto put16 = [&out](quint16 v) {
		out.append(static_cast<char>(v & 0xff));
		out.append(static_cast<char>((v >> 8) & 0xff));
	};

	out.append("RIFF");
	put32(36 + pcm.size());
	out.append("WAVEfmt ");
	put32(16);
	put16(1);                       // PCM
	put16(1);                       // mono
	put32(kRate);
	put32(kRate * 2);               // byte rate
	put16(2);                       // block align
	put16(16);                      // bits
	out.append("data");
	put32(pcm.size());
	out.append(pcm);

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return {}; }
	file.write(out);
	file.close();
	return path;
}

//! A sample clip on `track` whose source is the WAV at `wavPath`.
SampleClip* makeFileClip(SampleTrack& track, const QString& wavPath)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	clip->setSampleFile(wavPath);
	return clip;
}

} // namespace


class ClipSerialisationTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(m_dir.isValid());
		m_wav = writeToneWav(m_dir.filePath("tone.wav"));
		m_project = m_dir.filePath("roundtrip.mmp");
		m_projectAgain = m_dir.filePath("roundtrip-again.mmp");
		QVERIFY(QFile::exists(m_wav));
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! I9: an untrimmed clip writes exactly the attributes it wrote before this
	//! slice, and its state round-trips through the serialiser unchanged.
	void untrimmedClipSerialisesExactlyAsBefore()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeFileClip(track, m_wav);
		QCOMPARE(static_cast<int>(clip->sample().buffer()->size()), kRate * kSeconds);

		QDomDocument doc;
		QDomElement parent = doc.createElement("track");
		const QDomElement element = clip->saveState(doc, parent);
		QCOMPARE(element.nodeName(), QString("sampleclip"));
		QCOMPARE(attributeNames(element), upstreamAttributeNames());
		QVERIFY(!element.hasAttribute("srcin"));
		QVERIFY(!element.hasAttribute("srcout"));
		QCOMPARE(element.attribute("autoresize"), QString("1"));

		// load and re-save: the same authored state, attribute for attribute
		SampleTrack reloadedTrack(Engine::getSong());
		auto* reloaded = makeFileClip(reloadedTrack, m_wav);
		reloaded->restoreState(element);

		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement("track");
		const QDomElement element2 = reloaded->saveState(doc2, parent2);
		QCOMPARE(attributeNames(element2), upstreamAttributeNames());
		QCOMPARE(withoutJournallingIds(nodeToString(element2)), withoutJournallingIds(nodeToString(element)));
	}

	//! Slice 1: the window is persisted, and it comes back identical.
	void trimmedWindowRoundTrips()
	{
		const f_cnt_t in = 2 * kRate;
		const f_cnt_t out = 3 * kRate;

		SampleTrack track(Engine::getSong());
		auto* clip = makeFileClip(track, m_wav);
		clip->setSampleStartFrame(in);
		clip->setSamplePlayLength(out);
		const auto authoredLength = static_cast<f_cnt_t>(clip->length());

		QDomDocument doc;
		QDomElement parent = doc.createElement("track");
		const QDomElement element = clip->saveState(doc, parent);
		QCOMPARE(element.attribute("srcin").toULongLong(), static_cast<qulonglong>(in));
		QCOMPARE(element.attribute("srcout").toULongLong(), static_cast<qulonglong>(out));
		// §2.6: a trim is a manual edit, and the file must say so
		QCOMPARE(element.attribute("autoresize"), QString("0"));

		SampleTrack reloadedTrack(Engine::getSong());
		auto* reloaded = makeFileClip(reloadedTrack, m_wav);
		reloaded->restoreState(element);

		QCOMPARE(reloaded->sampleWindow(), SampleWindow(in, out));
		QCOMPARE(static_cast<f_cnt_t>(reloaded->sample().startFrame()), in);
		QCOMPARE(static_cast<f_cnt_t>(reloaded->sample().endFrame()), out);
		QCOMPARE(reloaded->getAutoResize(), false);
		// §2.6: `len` stays authoritative, so the clip's length is what was saved
		QCOMPARE(static_cast<f_cnt_t>(reloaded->length()), authoredLength);

		// and the reloaded clip re-saves the same window (no drift)
		QDomDocument doc2;
		QDomElement parent2 = doc2.createElement("track");
		const QDomElement element2 = reloaded->saveState(doc2, parent2);
		QCOMPARE(withoutJournallingIds(nodeToString(element2)), withoutJournallingIds(nodeToString(element)));
	}

	//! The whole-project round trip: a window set, the project saved, reloaded, and
	//! the window identical - the acceptance criterion for this slice.
	void wholeProjectWithAWindowRoundTrips()
	{
		const f_cnt_t in = kRate;
		const f_cnt_t out = 3 * kRate;

		{
			SampleTrack track(Engine::getSong());
			auto* clip = makeFileClip(track, m_wav);
			clip->setSampleStartFrame(in);
			clip->setSamplePlayLength(out);
			QVERIFY(Engine::getSong()->saveProjectFile(m_project));
		}

		QVERIFY(QFile::exists(m_project));
		const auto saved = readWholeFile(m_project);
		QVERIFY(saved.contains(QString("srcin=\"%1\"").arg(in)));
		QVERIFY(saved.contains(QString("srcout=\"%1\"").arg(out)));
		QVERIFY(saved.contains("autoresize=\"0\""));

		Engine::getSong()->loadProject(m_project);

		auto* reloaded = firstSampleClip();
		QVERIFY(reloaded != nullptr);
		QCOMPARE(reloaded->sampleWindow(), SampleWindow(in, out));
		QCOMPARE(static_cast<f_cnt_t>(reloaded->sample().startFrame()), in);
		QCOMPARE(static_cast<f_cnt_t>(reloaded->sample().endFrame()), out);

		// a second save of the loaded project carries the same window: the round
		// trip is stable, not merely lossless once
		QVERIFY(Engine::getSong()->saveProjectFile(m_projectAgain));
		const auto again = readWholeFile(m_projectAgain);
		QVERIFY(!again.isEmpty());
		const auto windowBefore = attributesOf(saved, "sampleclip");
		const auto windowAfter = attributesOf(again, "sampleclip");
		QVERIFY(!windowBefore.isEmpty());
		QCOMPARE(windowAfter.value("srcin"), windowBefore.value("srcin"));
		QCOMPARE(windowAfter.value("srcout"), windowBefore.value("srcout"));
		QCOMPARE(windowAfter.value("autoresize"), windowBefore.value("autoresize"));
		QCOMPARE(windowAfter.value("len"), windowBefore.value("len"));
	}

private:
	//! The files a round trip needs; `m_dir` outlives every test slot.
	QString m_wav;
	QString m_project;
	QString m_projectAgain;
	QTemporaryDir m_dir;

	//! The first SampleClip in the song, in track then clip order.
	static SampleClip* firstSampleClip()
	{
		for (auto* track : Engine::getSong()->tracks())
		{
			if (auto* sampleTrack = dynamic_cast<SampleTrack*>(track))
			{
				for (int i = 0; i < sampleTrack->numOfClips(); ++i)
				{
					if (auto* clip = dynamic_cast<SampleClip*>(sampleTrack->getClip(i)))
					{
						return clip;
					}
				}
			}
		}
		return nullptr;
	}
};

QTEST_GUILESS_MAIN(ClipSerialisationTest)
#include "ClipSerialisationTest.moc"
