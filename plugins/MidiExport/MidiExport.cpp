/*
 * MidiExport.cpp - support for Exporting MIDI files
 *
 * Copyright (c) 2015 Mohamed Abdel Maksoud <mohamed at amaksoud.com>
 * Copyright (c) 2017 Hyunjin Song <tteu.ingog/at/gmail.com>
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



#include "MidiExport.h"

#include "TrackContainer.h"
#include "DataFile.h"
#include "InstrumentTrack.h"
#include "LocaleHelper.h"
#include "PatternTrack.h"
#include "Engine.h"
#include "MidiPort.h"
#include "Song.h"
#include "SmfInterchange.h"

#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT midiexport_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"MIDI Export",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"Filter for exporting MIDI-files from Zene Studio" ),
	"Mohamed Abdel Maksoud <mohamed at amaksoud.com> and "
	"Hyunjin Song <tteu.ingog/at/gmail.com>",
	0x0100,
	Plugin::Type::ExportFilter,
	nullptr,
	nullptr,
	nullptr,
} ;

}


MidiExport::MidiExport() : ExportFilter( &midiexport_plugin_descriptor)
{
}




ExportOutcome MidiExport::tryExport(const TrackContainer::TrackList &tracks,
			const TrackContainer::TrackList &patternStoreTracks,
			int tempo, int masterPitch, const QString &filename)
{
	QFile f(filename);
	// Exclusive create: never truncate a file that is already there (a collision
	// with a file the user keeps beside the target must not be destroyed). A
	// failed open means THIS call created nothing, so the caller must not touch
	// the path either: the outcome says which of the two happened.
	if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly))
	{
		return ExportOutcome::NotCreated;
	}
	QDataStream midiout(&f);

	QDomElement element;


	int nTracks = 1; // the conductor track
	// One growable chunk buffer for every MTrk and the header; each is written whole, never capped.
	std::vector<uint8_t> chunk;
	const auto writeChunk = [&](const auto &writable)
	{
		chunk.clear();
		writable.writeToBuffer(chunk);
		midiout.writeRawData(reinterpret_cast<const char *>(chunk.data()), int(chunk.size()));
	};

	for (const Track* track : tracks) if (track->type() == Track::Type::Instrument) nTracks++;
	for (const Track* track : patternStoreTracks) if (track->type() == Track::Type::Instrument) nTracks++;

	// midi header
	MidiFile::MIDIHeader header(nTracks);
	writeChunk(header);

	{
		MTrack conductor;
		writeConductor(conductor, tempo);
		writeChunk(conductor);
	}

	std::vector<std::vector<std::pair<int,int>>> plists;

	// midi tracks
	for (Track* track : tracks)
	{
		DataFile dataFile(DataFile::Type::SongProject);
		MTrack mtrack;

		if (track->type() == Track::Type::Instrument)
		{

			auto instTrack = dynamic_cast<InstrumentTrack *>(track);
			beginInstrumentTrack(mtrack, instTrack);
			element = instTrack->saveState(dataFile, dataFile.content());

			int base_pitch = 0;
			double base_volume = 1.0;
			int base_time = 0;

			MidiNoteVector midiClip;

			for (QDomNode n = element.firstChild(); !n.isNull(); n = n.nextSibling())
			{

				if (n.nodeName() == "instrumenttrack")
				{
					QDomElement it = n.toElement();
					base_pitch = (69 - it.attribute("basenote", "69").toInt());
					if (it.attribute("usemasterpitch", "1").toInt())
					{
						base_pitch += masterPitch;
					}
					base_volume = LocaleHelper::toDouble(it.attribute("volume", "100"))/100.0;
				}

				if (n.nodeName() == "midiclip")
				{
					base_time = n.toElement().attribute("pos", "0").toInt();
					writeMidiClip(midiClip, n, base_pitch, base_volume, base_time);
				}

			}
			processPatternNotes(midiClip, INT_MAX);
			writeMidiClipToTrack(mtrack, midiClip);
			writeChunk(mtrack);
		}

		if (track->type() == Track::Type::Pattern)
		{
			auto patternTrack = dynamic_cast<PatternTrack*>(track);
			element = patternTrack->saveState(dataFile, dataFile.content());

			std::vector<std::pair<int,int>> plist;
			for (QDomNode n = element.firstChild(); !n.isNull(); n = n.nextSibling())
			{

				if (n.nodeName() == "patternclip")
				{
					QDomElement it = n.toElement();
					int pos = it.attribute("pos", "0").toInt();
					int len = it.attribute("len", "0").toInt();
					plist.emplace_back(pos, pos+len);
				}
			}
			std::sort(plist.begin(), plist.end());
			plists.push_back(plist);

		}
	} // for each track

	// for each instrument in the pattern editor
	for (Track* track : patternStoreTracks)
	{
		DataFile dataFile(DataFile::Type::SongProject);
		MTrack mtrack;

		// begin at the first pattern track (first pattern)
		auto itr = plists.begin();

		std::vector<std::pair<int,int>> st;

		if (track->type() != Track::Type::Instrument) continue;

		auto instTrack = dynamic_cast<InstrumentTrack *>(track);
		beginInstrumentTrack(mtrack, instTrack);
		element = instTrack->saveState(dataFile, dataFile.content());

		int base_pitch = 0;
		double base_volume = 1.0;

		// for each pattern in the pattern editor
		for (QDomNode n = element.firstChild(); !n.isNull(); n = n.nextSibling())
		{
			if (n.nodeName() == "instrumenttrack")
			{
				QDomElement it = n.toElement();
				base_pitch = (69 - it.attribute("basenote", "69").toInt());
				if (it.attribute("usemasterpitch", "1").toInt())
				{
					base_pitch += masterPitch;
				}
				base_volume = LocaleHelper::toDouble(it.attribute("volume", "100")) / 100.0;
			}

			if (n.nodeName() == "midiclip")
			{
				std::vector<std::pair<int,int>> &plist = *itr;

				MidiNoteVector nv, midiClip;
				writeMidiClip(midiClip, n, base_pitch, base_volume, 0);


				// FIXME better variable names and comments
				int pos = 0;
				int len = n.toElement().attribute("steps", "1").toInt() * 12;

				// for each pattern clip of the current pattern track (in song editor)
				for (const auto& position : plist)
				{
					const auto& [start, end] = position;
					while (!st.empty() && st.back().second <= start)
					{
						writePatternClip(midiClip, nv, len, st.back().first, pos, st.back().second);
						pos = st.back().second;
						st.pop_back();
					}

					if (!st.empty() && st.back().second <= end)
					{
						writePatternClip(midiClip, nv, len, st.back().first, pos, start);
						pos = start;
						while (!st.empty() && st.back().second <= end)
						{
							st.pop_back();
						}
					}

					st.push_back(position);
					pos = start;
				}

				while (!st.empty())
				{
					writePatternClip(midiClip, nv, len, st.back().first, pos, st.back().second);
					pos = st.back().second;
					st.pop_back();
				}

				processPatternNotes(nv, pos);
				writeMidiClipToTrack(mtrack, nv);

				// next pattern track
				++itr;
			}
		}
		writeChunk(mtrack);
	}

	// The file's error is read TWICE, because the two errors live at different times:
	// BEFORE the close it is the failed write (a close that succeeds CLEARS that error -
	// Qt resets the device status), and AFTER it is the flush error (the bytes the
	// device still held) or an error the close itself reports, which only shows up then.
	// Reading the error before the close alone missed both.
	const bool writeOk = midiout.status() == QDataStream::Ok && f.error() == QFileDevice::NoError;
	f.close();
	// A half-written file - or one that did not close cleanly - is the caller's to remove
	// (Incomplete); a complete one is the caller's to replace (Written). Neither is guessed
	// from the file's presence on disk: this call knows which of the two it is.
	return exportOutcomeAfterClose(f, writeOk);
}

void MidiExport::writeMidiClip(MidiNoteVector &midiClip, const QDomNode& n,
				int base_pitch, double base_volume, int base_time)
{
	// TODO interpret steps="12" muted="0" type="1" name="Piano1"  len="2592"
	for (QDomNode nn = n.firstChild(); !nn.isNull(); nn = nn.nextSibling())
	{
		QDomElement note = nn.toElement();
		if (note.attribute("len", "0") == "0") continue;
		// TODO interpret pan="0" mixch="0" pitchrange="1"
		MidiNote mnote;
		mnote.pitch = qMax(0, qMin(127, note.attribute("key", "0").toInt() + base_pitch));
		 // Map from LMMS volume to MIDI velocity
		mnote.volume = qMin(qRound(base_volume * LocaleHelper::toDouble(note.attribute("vol", "100")) * (127.0 / 200.0)), 127);
		mnote.time = base_time + note.attribute("pos", "0").toInt();
		mnote.duration = note.attribute("len", "0").toInt();
		mnote.type = static_cast<Note::Type>(note.attribute("type", "0").toInt());
		midiClip.push_back(mnote);
	}
}



void MidiExport::beginInstrumentTrack(MTrack &mtrack, InstrumentTrack *track)
{
	// Channel: the track's MIDI output channel, 1..16 -> 0..15. "--" (0) is what the engine itself
	// plays on channel 1 (MidiPort::realOutputChannel), so the file says the same.
	// Program: the track's output program (1..128 -> 0..127); the default 1 is written as program 0
	// (Acoustic Grand Piano), the instrument a General MIDI player assumes anyway.
	mtrack.channel = static_cast<uint8_t>(track->midiPort()->realOutputChannel());
	mtrack.addName(track->name().toStdString(), 0);
	mtrack.addProgramChange(static_cast<uint8_t>(qBound(1, track->midiPort()->outputProgram(), 128) - 1), 0);
}



void MidiExport::writeConductor(MTrack &mtrack, int globalTempo)
{
	Song* song = Engine::getSong();
	const TempoMapTimeSignature signature{
		song->getTimeSigModel().numeratorModel().value(),
		song->getTimeSigModel().denominatorModel().value()};
	mtrack.addName("Zene Studio tempo map", 0);
	const auto events = interchange::exportEvents(song->tempoMap().map(), globalTempo, signature, nullptr);
	for (const auto& event : events)
	{
		const uint32_t tick = uint32_t(event.tick) * interchange::SmfTicksPerLmmsTick;
		if (event.hasTimeSignature) { mtrack.addTimeSignature(event.numerator, event.denominator, tick); }
		if (event.hasTempo) { mtrack.addTempo(event.tempo, tick); }
	}
}



void MidiExport::writeMidiClipToTrack(MTrack &mtrack, MidiNoteVector &nv)
{
	for (const auto& note : nv)
	{
		// LMMS ticks are 1/48 beat, the file's are 1/480: exact integer scaling, no rounding.
		constexpr uint32_t scale = interchange::SmfTicksPerLmmsTick;
		mtrack.addNoteTicks(note.pitch, note.volume, uint32_t(note.time) * scale,
			uint32_t(note.time + note.duration) * scale);
	}
}



void MidiExport::writePatternClip(MidiNoteVector& src, MidiNoteVector& dst,
				int len, int base, int start, int end)
{
	if (start >= end) { return; }
	start -= base;
	end -= base;
	std::sort(src.begin(), src.end());
	for (const auto& srcNote : src)
	{
		for (int time = srcNote.time + ceil((start - srcNote.time) / len) * len; time < end; time += len)
		{
			MidiNote note;
			note.duration = srcNote.duration;
			note.pitch = srcNote.pitch;
			note.time = base + time;
			note.volume = srcNote.volume;
			note.type = srcNote.type;
			dst.push_back(note);
		}
	}
}



void MidiExport::processPatternNotes(MidiNoteVector& nv, int cutPos)
{
	std::sort(nv.begin(), nv.end());
	int cur = INT_MAX, next = INT_MAX;
	for (auto it = nv.rbegin(); it != nv.rend(); ++it)
	{
		if (it->time < cur)
		{
			next = cur;
			cur = it->time;
		}
		if (it->type == Note::Type::Step)
		{
			it->duration = qMin(qMin(DefaultBeatLength, next - cur), cutPos - it->time);
		}
	}
}



void MidiExport::error()
{
	//qDebug() << "MidiExport error: " << m_error ;
}



extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model *, void * _data )
{
	return new MidiExport();
}


}


} // namespace lmms
