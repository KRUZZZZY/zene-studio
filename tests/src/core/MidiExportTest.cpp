/*
 * MidiExportTest.cpp - File > Export MIDI / project.export_midi: tempo map, track identity, timing
 *
 * Q050-020. The exported Standard MIDI File is parsed here by this test's own reader (not by the
 * product's) and checked against what the test authored:
 *
 *  1. a session with mid-song tempo and metre changes writes track 0 as a conductor track whose
 *     tempo and time-signature events are the tempo map's (the same events interchange.smf_export
 *     writes), at 480 ticks per quarter note;
 *  2. two instrument tracks with different programs and channels write one MTrk each with the
 *     track name, program change, the track's own channel, and note-on/off at exact 480-ppq ticks,
 *     including notes at LMMS ticks that fall between the ticks of the old 128-ppq grid;
 *  3. a track whose output channel is "--" writes on channel 1, as the engine plays it.
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

#include <QDomDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <csignal>
#include <vector>

#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif

#include "ControlProjectAssets.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ExportFilter.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiPort.h"
#include "Note.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

struct SmfEvent
{
	quint32 tick = 0;
	int status = 0;      //!< 0xFF for meta, else the status byte with channel
	int type = 0;        //!< meta type, or the first data byte
	int data2 = 0;
	QByteArray payload;  //!< meta payload
};

struct SmfTrack { std::vector<SmfEvent> events; };

struct SmfFile
{
	int format = -1;
	int division = 0;
	std::vector<SmfTrack> tracks;
};

quint32 be32(const QByteArray& b, int o)
{
	return (quint32(uchar(b[o])) << 24) | (quint32(uchar(b[o + 1])) << 16)
		| (quint32(uchar(b[o + 2])) << 8) | quint32(uchar(b[o + 3]));
}
int be16(const QByteArray& b, int o) { return (uchar(b[o]) << 8) | uchar(b[o + 1]); }

quint32 readVlq(const QByteArray& b, int& o)
{
	quint32 v = 0;
	uchar c;
	do { c = uchar(b[o++]); v = (v << 7) | (c & 0x7F); } while (c & 0x80);
	return v;
}

//! Reads one event at o (after its delta time); running status is carried in `running`.
SmfEvent readEvent(const QByteArray& b, int& o, quint32 tick, int& running)
{
	SmfEvent e;
	e.tick = tick;
	int status = uchar(b[o]);
	if (status < 0x80) { status = running; } else { ++o; }
	e.status = status;
	if (status == 0xFF)
	{
		e.type = uchar(b[o++]);
		const quint32 len = readVlq(b, o);
		e.payload = b.mid(o, int(len));
		o += int(len);
		return e;
	}
	running = status;
	e.type = uchar(b[o++]);
	if ((status & 0xF0) != 0xC0 && (status & 0xF0) != 0xD0) { e.data2 = uchar(b[o++]); }
	return e;
}

bool parseSmf(const QByteArray& b, SmfFile* out)
{
	if (b.size() < 14 || b.left(4) != "MThd") { return false; }
	out->format = be16(b, 8);
	const int count = be16(b, 10);
	out->division = be16(b, 12);
	int o = 14;
	for (int t = 0; t < count; ++t)
	{
		if (b.mid(o, 4) != "MTrk") { return false; }
		const int end = o + 8 + int(be32(b, o + 4));
		o += 8;
		SmfTrack track;
		quint32 tick = 0;
		int running = 0;
		while (o < end)
		{
			tick += readVlq(b, o);
			track.events.push_back(readEvent(b, o, tick, running));
		}
		o = end;
		out->tracks.push_back(track);
	}
	return true;
}

int tempoMicros(const SmfEvent& e)
{
	return (uchar(e.payload[0]) << 16) | (uchar(e.payload[1]) << 8) | uchar(e.payload[2]);
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

#ifdef Q_OS_UNIX
/*! Lowers this process's file size limit for the lifetime of the object, so the kernel
 *  REFUSES a write past \a bytes (EFBIG) - a real refused write, not a simulated one. The
 *  signal that limit raises is ignored meanwhile. Both are restored on destruction. */
class FileSizeLimit
{
public:
	explicit FileSizeLimit(rlim_t bytes)
	{
		if (getrlimit(RLIMIT_FSIZE, &m_saved) != 0) { return; }
		rlimit lowered = m_saved;
		lowered.rlim_cur = bytes;
		if (setrlimit(RLIMIT_FSIZE, &lowered) != 0) { return; }
		m_previousSigxfsz = std::signal(SIGXFSZ, SIG_IGN);
		m_applied = (m_previousSigxfsz != SIG_ERR);
	}

	~FileSizeLimit()
	{
		if (!m_applied) { return; }
		std::signal(SIGXFSZ, m_previousSigxfsz);
		setrlimit(RLIMIT_FSIZE, &m_saved);
	}

	FileSizeLimit(const FileSizeLimit&) = delete;
	FileSizeLimit& operator=(const FileSizeLimit&) = delete;

	bool applied() const { return m_applied; }

private:
	rlimit m_saved{};
	void (*m_previousSigxfsz)(int) = SIG_DFL;
	bool m_applied = false;
};
#endif

} // namespace

class MidiExportTest : public QObject
{
	Q_OBJECT

	InstrumentTrack* addTrack(const QString& name, int channel, int program,
		const std::vector<std::pair<int, int>>& notes)
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		track->setName(name);
		track->midiPort()->setOutputChannel(channel);
		track->midiPort()->setOutputProgram(program);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		for (const auto& n : notes) { clip->addNote(Note(TimePos(24), TimePos(n.first), n.second, 100), false); }
		clip->rearrangeAllNotes();
		return track;
	}

	bool exportAndParse(SmfFile* file)
	{
		const QString path = m_dir.filePath(QStringLiteral("out.mid"));
		QFile::remove(path);
		const ControlResult result = run(QStringLiteral("project.export_midi"), {{QStringLiteral("path"), path}});
		if (!result.ok) { qWarning() << result.errorMessage; return false; }
		QFile f(path);
		return f.open(QIODevice::ReadOnly) && parseSmf(f.readAll(), file);
	}

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		// The test seams must never outlive the case that set them.
		control::setControlExportMidiTempPathHook(nullptr);
		setExportCloseFailureHook(nullptr);
	}

	//! Acceptance 1: mid-song tempo and metre changes are the conductor track's events.
	void tempoMapChangesAreWrittenToTheConductorTrack()
	{
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"), {{QStringLiteral("tick"), 0}, {QStringLiteral("bpm"), 140}}).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"), {{QStringLiteral("tick"), 384}, {QStringLiteral("bpm"), 90},
			{QStringLiteral("numerator"), 3}, {QStringLiteral("denominator"), 4}}).ok);
		QVERIFY(run(QStringLiteral("transport.tempo_map_add"), {{QStringLiteral("tick"), 960}, {QStringLiteral("bpm"), 175}}).ok);
		addTrack(QStringLiteral("Lead"), 1, 1, {{0, 60}});

		SmfFile file;
		QVERIFY(exportAndParse(&file));
		QCOMPARE(file.format, 1);
		QCOMPARE(file.division, 480);
		QCOMPARE(int(file.tracks.size()), 2); // conductor + one instrument

		std::vector<std::pair<quint32, int>> tempos;
		std::vector<quint32> meters;
		for (const SmfEvent& e : file.tracks[0].events)
		{
			if (e.status == 0xFF && e.type == 0x51) { tempos.emplace_back(e.tick, tempoMicros(e)); }
			if (e.status == 0xFF && e.type == 0x58)
			{
				meters.push_back(e.tick);
				if (e.tick == 3840) { QCOMPARE(int(uchar(e.payload[0])), 3); QCOMPARE(int(uchar(e.payload[1])), 2); }
			}
		}
		// LMMS ticks 0/384/960 -> file ticks 0/3840/9600; 60e6/bpm rounded.
		QCOMPARE(int(tempos.size()), 3);
		QCOMPARE(tempos[0], (std::pair<quint32, int>(0, 428571)));
		QCOMPARE(tempos[1], (std::pair<quint32, int>(3840, 666667)));
		QCOMPARE(tempos[2], (std::pair<quint32, int>(9600, 342857)));
		QVERIFY(std::find(meters.begin(), meters.end(), 3840u) != meters.end());
		// The instrument track carries no tempo events of its own.
		for (const SmfEvent& e : file.tracks[1].events) { QVERIFY(!(e.status == 0xFF && e.type == 0x51)); }
	}

	//! Acceptance 2: two tracks, two patches, two channels, exact timing.
	void instrumentTracksCarryNameProgramChannelAndExactTiming()
	{
		// Positions 5 and 7 are 1/48-beat ticks that the old 128-ppq grid truncated (5*128/48 = 13.33).
		addTrack(QStringLiteral("Piano"), 1, 1, {{0, 60}, {5, 64}});
		addTrack(QStringLiteral("Strings"), 3, 49, {{7, 67}});

		SmfFile file;
		QVERIFY(exportAndParse(&file));
		QCOMPARE(file.division, 480);
		QCOMPARE(int(file.tracks.size()), 3);

		const auto check = [&](int index, const char* name, int channel, int program,
			const std::vector<std::tuple<quint32, quint32, int>>& notes)
		{
			const SmfTrack& t = file.tracks[index];
			bool sawName = false, sawProgram = false;
			std::vector<std::tuple<quint32, quint32, int>> found;
			for (const SmfEvent& e : t.events)
			{
				if (e.status == 0xFF && e.type == 0x03) { sawName = (e.payload == name); }
				if ((e.status & 0xF0) == 0xC0) { sawProgram = true; QCOMPARE(e.status & 0x0F, channel); QCOMPARE(e.type, program); QCOMPARE(e.tick, 0u); }
				if ((e.status & 0xF0) == 0x90)
				{
					QCOMPARE(e.status & 0x0F, channel);
					found.emplace_back(e.tick, 0u, e.type);
				}
				if ((e.status & 0xF0) == 0x80)
				{
					QCOMPARE(e.status & 0x0F, channel);
					for (auto& f : found) { if (std::get<2>(f) == e.type && std::get<1>(f) == 0u) { std::get<1>(f) = e.tick; break; } }
				}
			}
			QVERIFY2(sawName, name);
			QVERIFY2(sawProgram, name);
			QCOMPARE(found, notes);
		};
		// start tick * 10, end = (start + 24) * 10.
		check(1, "Piano", 0, 0, {{0, 240, 60}, {50, 290, 64}});
		check(2, "Strings", 2, 48, {{70, 310, 67}});
	}

	//! Output channel "--" (0): written on channel 1, as MidiPort::realOutputChannel plays it.
	void anUnsetChannelIsWrittenOnChannelOne()
	{
		addTrack(QStringLiteral("Unset"), 0, 1, {{0, 60}});
		SmfFile file;
		QVERIFY(exportAndParse(&file));
		bool saw = false;
		for (const SmfEvent& e : file.tracks[1].events)
		{
			if ((e.status & 0xF0) == 0x90) { saw = true; QCOMPARE(e.status & 0x0F, 0); }
			if ((e.status & 0xF0) == 0xC0) { QCOMPARE(e.type, 0); }
		}
		QVERIFY(saw);
	}

	//! Round 2: a track denser than the old 50 KiB cap is written whole, never truncated or overrun.
	void aDenseTrackIsWrittenWhole()
	{
		// 6,000 sequential notes, 48 LMMS ticks apart and 24 long: about 60,000 bytes of events at 480 ppq.
		std::vector<std::pair<int, int>> notes;
		for (int k = 0; k < 6000; ++k) { notes.emplace_back(k * 48, 60 + k % 12); }
		addTrack(QStringLiteral("Dense"), 1, 1, notes);

		SmfFile file;
		QVERIFY(exportAndParse(&file));
		QCOMPARE(int(file.tracks.size()), 2);
		int ons = 0, offs = 0;
		quint32 lastOff = 0;
		for (const SmfEvent& e : file.tracks[1].events)
		{
			if ((e.status & 0xF0) == 0x90) { ++ons; }
			if ((e.status & 0xF0) == 0x80) { ++offs; lastOff = e.tick; }
		}
		QCOMPARE(ons, 6000);
		QCOMPARE(offs, 6000);
		QCOMPARE(lastOff, quint32((5999 * 48 + 24) * 10));
		QVERIFY(!file.tracks[1].events.empty());
		QCOMPARE(file.tracks[1].events.back().status, 0xFF);
		QCOMPARE(file.tracks[1].events.back().type, 0x2F);
	}

	//! A file already present at the path the exporter is asked to write must not
	//! be truncated: creation is exclusive, so a collision is a failed export.
	void collidingPathIsNotClobberedByExport()
	{
		const QString path = m_dir.filePath(QStringLiteral("collide.mid"));
		const QByteArray userBytes("USER-FILE-AT-COLLIDING-PATH");
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write(userBytes) == userBytes.size());
		}
		// The export is refused: the path was taken, so it was never written to.
		Engine::getSong()->exportProjectMidi(path);
		QFile kept(path);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), userBytes);
	}

	/*! A file that occupies the TEMPORARY path the export writes to must survive the call
	 *  whole: it is neither written to nor removed, and the previous export at the target
	 *  survives too. The collision is forced through the product's test seam - a file is put
	 *  at the temporary path exactly where the export claims it - so the case is a fact of
	 *  the test rather than a race. */
	void aCollidingTemporaryPathKeepsTheUserFileAndThePreviousExport()
	{
		// The export works at all in this process (the filter loads), so the refusal below
		// is the collision and nothing else.
		const ControlResult works = run(QStringLiteral("project.export_midi"),
			{{QStringLiteral("path"), m_dir.filePath(QStringLiteral("works.mid"))}});
		QVERIFY2(works.ok, qPrintable(works.errorMessage));

		const QString path = m_dir.filePath(QStringLiteral("collision.mid"));
		const QByteArray previous("PREVIOUS-EXPORT-BYTES");
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write(previous) == previous.size());
		}
		const QByteArray userBytes("USER-FILE-AT-THE-TEMPORARY-PATH");
		QString candidate;
		bool wroteUserFile = false;
		control::setControlExportMidiTempPathHook([&](const QString& tempPath) {
			candidate = tempPath;
			QFile file(tempPath);
			if (file.open(QIODevice::WriteOnly))
			{
				wroteUserFile = (file.write(userBytes) == userBytes.size());
			}
		});
		const ControlResult result = run(QStringLiteral("project.export_midi"),
			{{QStringLiteral("path"), path}});
		control::setControlExportMidiTempPathHook(nullptr);

		QVERIFY(!candidate.isEmpty());
		QVERIFY(wroteUserFile);
		QVERIFY2(!result.ok, "the export reported success although its temporary path was taken");
		// The file that was already there is untouched: not written to, not removed.
		QFile kept(candidate);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), userBytes);
		// The previous export is still there, byte for byte.
		QFile previousFile(path);
		QVERIFY(previousFile.open(QIODevice::ReadOnly));
		QCOMPARE(previousFile.readAll(), previous);
	}

	/*! An output file that did not close cleanly (a flush error, or an error the close
	 *  itself reports) is an INCOMPLETE export: the command is refused, the partial
	 *  temporary file is removed, and the previous export at the target survives byte for
	 *  byte. The close is forced through the product's test seam, so the case is a fact of
	 *  the test rather than a broken filesystem. */
	void aFailedCloseLeavesThePreviousExportInPlace()
	{
		// The export works at all in this process (the filter loads), so the refusal below
		// is the failed close and nothing else.
		const ControlResult works = run(QStringLiteral("project.export_midi"),
			{{QStringLiteral("path"), m_dir.filePath(QStringLiteral("works.mid"))}});
		QVERIFY2(works.ok, qPrintable(works.errorMessage));

		const QString path = m_dir.filePath(QStringLiteral("close-failure.mid"));
		const QByteArray previous("PREVIOUS-EXPORT-BYTES");
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write(previous) == previous.size());
		}

		QString tempPath;
		control::setControlExportMidiTempPathHook([&](const QString& p) { tempPath = p; });
		// The close reports a failure.
		setExportCloseFailureHook([] { return true; });
		const ControlResult result = run(QStringLiteral("project.export_midi"),
			{{QStringLiteral("path"), path}});
		setExportCloseFailureHook(nullptr);
		control::setControlExportMidiTempPathHook(nullptr);

		QVERIFY2(!result.ok, "the export reported success although its file did not close cleanly");
		QVERIFY(!tempPath.isEmpty());
		// The partial output this call created is removed.
		QVERIFY2(!QFileInfo::exists(tempPath), "the partial temporary file was left behind");
		// The previous export is still there, byte for byte.
		QFile kept(path);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), previous);
	}

	/*! The destination cannot be created at all (its directory does not exist). The call created
	 *  nothing, so the exporter's outcome is NotCreated - never a written file. The caller relies
	 *  on that answer, so it is checked on the exporter itself, not only through the command. */
	void anUncreatableDestinationIsNotReportedAsWritten()
	{
		const QString path = m_dir.filePath(QStringLiteral("no-such-directory/export.mid"));
		const ExportOutcome outcome = Engine::getSong()->exportProjectMidi(path);
		QVERIFY2(outcome == ExportOutcome::NotCreated,
			"an export whose file could not be created did not report NotCreated");
		QVERIFY(!QFileInfo::exists(path));
	}

	/*! A write the disk REFUSES is a failed export, never a success. The file size limit makes the
	 *  kernel refuse the export's writes for real once the file passes 4 bytes (the header alone is
	 *  longer), so the case is a fact of the test rather than a full disk. The refusal is reported,
	 *  the partial file this call created is removed, and the previous export at the target
	 *  survives byte for byte. */
	void aRefusedWriteIsAFailedExportNotASuccess()
	{
#ifdef Q_OS_UNIX
		// The export works at all in this process (the filter loads), so the refusal below
		// is the refused write and nothing else.
		const ControlResult works = run(QStringLiteral("project.export_midi"),
			{{QStringLiteral("path"), m_dir.filePath(QStringLiteral("works.mid"))}});
		QVERIFY2(works.ok, qPrintable(works.errorMessage));

		const QString path = m_dir.filePath(QStringLiteral("write-refused.mid"));
		const QByteArray previous("PREVIOUS-EXPORT-BYTES");
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write(previous) == previous.size());
		}

		QString tempPath;
		control::setControlExportMidiTempPathHook([&](const QString& p) { tempPath = p; });
		// Held until the end of this case: the kernel refuses the export's writes past 4 bytes.
		// No product write happens after the export call, so the limit is not in the way of any
		// assertion below; it is restored on every exit path by the guard's destructor.
		FileSizeLimit limit(4);
		QVERIFY2(limit.applied(), "could not lower the file size limit for the refused-write case");
		const ControlResult result = run(QStringLiteral("project.export_midi"),
			{{QStringLiteral("path"), path}});
		control::setControlExportMidiTempPathHook(nullptr);

		QVERIFY2(!result.ok, "the export reported success although the disk refused its write");
		QVERIFY(!tempPath.isEmpty());
		// The partial output this call created is removed.
		QVERIFY2(!QFileInfo::exists(tempPath), "the partial temporary file was left behind");
		// The previous export is still there, byte for byte.
		QFile kept(path);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), previous);
#else
		QSKIP("refusing a write needs a POSIX file size limit (RLIMIT_FSIZE)");
#endif
	}

	//! Zero pattern steps: writePatternClip() divides by `steps * 12`. The loader keeps a clip's step count at
	//! one bar or more (MidiClip::loadSettings), so the export never divides by zero. This runs the pattern path
	//! with a note in a pattern-store clip restored from a project element that says steps="0".
	void aZeroStepPatternExportsWithoutDividingByZero()
	{
		// The pattern track goes in first: the store track then gets its clip for pattern 0.
		auto* pattern = dynamic_cast<PatternTrack*>(Track::create(Track::Type::Pattern, Engine::getSong()));
		QVERIFY(pattern != nullptr);
		auto* store = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::patternStore()));
		QVERIFY(store != nullptr);
		store->setName(QStringLiteral("Store"));
		store->midiPort()->setOutputChannel(1);
		auto* storeClip = dynamic_cast<MidiClip*>(store->getClip(0));
		QVERIFY(storeClip != nullptr);
		storeClip->addNote(Note(TimePos(24), TimePos(0), 60, 100), false);
		storeClip->rearrangeAllNotes();

		QDomDocument doc;
		QDomElement parent = doc.createElement("track");
		QDomElement authored = storeClip->saveState(doc, parent);
		authored.setAttribute("steps", "0");
		storeClip->restoreState(authored);

		QVERIFY(pattern->createClip(TimePos(0)) != nullptr);

		SmfFile file;
		QVERIFY(exportAndParse(&file));
		QCOMPARE(int(file.tracks.size()), 2); // conductor + the pattern-store instrument
		int ons = 0;
		for (const SmfEvent& e : file.tracks[1].events) { if ((e.status & 0xF0) == 0x90) { ++ons; } }
		QCOMPARE(ons, 1); // one note at tick 0 in a one-bar pattern
	}

private:
	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(MidiExportTest)
#include "MidiExportTest.moc"
