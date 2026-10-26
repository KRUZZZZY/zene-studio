/*
 * RecordOptions.cpp - count-in, pre-roll and the record mode
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
#include "RecordOptions.h"

#include <algorithm>
#include <atomic>
#include <limits>

#include <QJsonArray>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Timeline.h"

namespace lmms::recordoptions
{

namespace
{

const QString kSection = QStringLiteral("recording");

//! The count-in of the running pass: [from, until) in ticks; until < 0 when there is none.
std::atomic<tick_t> s_countInFrom{0};
std::atomic<tick_t> s_countInUntil{-1};
bool s_gateOpened = false;  //!< UI thread: this pass opened the punch gate itself

int configInt(const char* key, int fallback)
{
	return ConfigManager::inst()->value(kSection, QLatin1String(key), QString::number(fallback)).toInt();
}

} // namespace

const QStringList& modeNames()
{
	static const QStringList names{QStringLiteral("overdub"), QStringLiteral("replace"), QStringLiteral("keep_history")};
	return names;
}

Options options()
{
	Options out;
	out.countInBars = std::clamp(configInt("countin", 0), 0, 4);
	out.preRollBars = std::clamp(configInt("preroll", 0), 0, 8);
	// The stored numbers are the enum's and never change; a value no build knows reads as the default.
	const int stored = configInt("mode", 0);
	out.mode = stored == static_cast<int>(Mode::Replace) ? Mode::Replace
		: stored == static_cast<int>(Mode::KeepHistory) ? Mode::KeepHistory : Mode::Overdub;
	return out;
}

void setOptions(const Options& next)
{
	ConfigManager::inst()->setValue(kSection, QStringLiteral("countin"), QString::number(std::clamp(next.countInBars, 0, 4)));
	ConfigManager::inst()->setValue(kSection, QStringLiteral("preroll"), QString::number(std::clamp(next.preRollBars, 0, 8)));
	ConfigManager::inst()->setValue(kSection, QStringLiteral("mode"), QString::number(static_cast<int>(next.mode)));
}

QJsonObject toJson(const Options& o)
{
	// Theme 7: what Replace erases and keeps, stated with the mode (the same for both, so a client can show it
	// before switching). Q050-094: the mode, its label and its scope for MIDI and for audio, and every choice.
	return {{QStringLiteral("count_in_bars"), o.countInBars}, {QStringLiteral("pre_roll_bars"), o.preRollBars},
		{QStringLiteral("mode"), modeNames().at(static_cast<int>(o.mode))}, {QStringLiteral("mode_label"), modeLabel(o.mode)},
		{QStringLiteral("mode_scope"), modeScope(o.mode)}, {QStringLiteral("midi_scope"), midiScope(o.mode)},
		{QStringLiteral("audio_scope"), audioScope(o.mode)}, {QStringLiteral("modes"), QJsonArray::fromStringList(modeNames())},
		{QStringLiteral("replace_erases"), QStringLiteral("overlapped_notes_any_pitch")},
		{QStringLiteral("replace_keeps"), QJsonArray{QStringLiteral("controllers"), QStringLiteral("automation")}}};
}

QString modeLabel(Mode mode)
{
	return mode == Mode::Replace ? QObject::tr("Replace") : mode == Mode::KeepHistory ? QObject::tr("Keep History") : QObject::tr("Overdub");
}

QString midiScope(Mode mode)
{
	switch (mode)
	{
	case Mode::Replace:
		return QObject::tr("a recorded note erases the notes already there that it overlaps, at any pitch. "
			"Controllers and automation are kept.");
	case Mode::KeepHistory:
		return QObject::tr("a MIDI pass keeps the notes it plays over, as Overdub does (MIDI take lanes are not kept yet).");
	case Mode::Overdub:
		break;
	}
	return QObject::tr("a MIDI pass keeps the notes it plays over.");
}

QString audioScope(Mode mode)
{
	switch (mode)
	{
	case Mode::Replace:
		return QObject::tr("an audio pass replaces the clip's audio; undo brings the earlier audio back.");
	case Mode::KeepHistory:
		return QObject::tr("the clip's earlier audio and the new pass each become a take lane; the new pass is the active take "
			"and any earlier one can be chosen again.");
	case Mode::Overdub:
		break;
	}
	return QObject::tr("an audio pass is mixed into the clip's audio, so the old audio stays audible beside the new.");
}

QString modeScope(Mode mode)
{
	return QObject::tr("%1: MIDI: %2 Audio: %3").arg(modeLabel(mode), midiScope(mode), audioScope(mode));
}

QString modeChoice(Mode mode)
{
	switch (mode)
	{
	case Mode::Replace:
		return QObject::tr("Replace (MIDI erases the notes it overlaps, any pitch, keeping controllers and automation; "
			"audio replaces the clip's audio)");
	case Mode::KeepHistory:
		return QObject::tr("Keep History (audio keeps the earlier audio and each pass as take lanes; MIDI keeps the notes)");
	case Mode::Overdub:
		break;
	}
	return QObject::tr("Overdub (MIDI keeps the notes already there; audio mixes into the clip's audio)");
}

std::shared_ptr<const SampleBuffer> overdubMix(const SampleBuffer& existing, const SampleBuffer& take)
{
	const int rate = existing.sampleRate();
	const int takeRate = take.sampleRate();
	// The take's length at the existing buffer's rate.
	const std::size_t takeFrames = takeRate == rate || takeRate <= 0 ? take.size()
		: static_cast<std::size_t>(static_cast<double>(take.size()) * rate / takeRate + 0.5);
	std::vector<SampleFrame> mixed(std::max(existing.size(), takeFrames));
	std::copy(existing.begin(), existing.end(), mixed.begin());
	for (std::size_t i = 0; i < takeFrames && !take.empty(); ++i)
	{
		SampleFrame frame = take.data()[std::min(i, take.size() - 1)];
		if (takeRate != rate && takeRate > 0)
		{
			const double at = static_cast<double>(i) * takeRate / rate;
			const auto whole = static_cast<std::size_t>(at);
			const auto next = std::min(whole + 1, take.size() - 1);
			const auto fraction = static_cast<float>(at - static_cast<double>(whole));
			const SampleFrame& a = take.data()[std::min(whole, take.size() - 1)];
			const SampleFrame& b = take.data()[next];
			frame = SampleFrame(a.left() + (b.left() - a.left()) * fraction, a.right() + (b.right() - a.right()) * fraction);
		}
		mixed[i] = mixed[i] + frame;
	}
	return std::make_shared<const SampleBuffer>(std::move(mixed), rate);
}

int replaceOverlapped(MidiClip* clip, const Note& recorded, const std::vector<int>& beforePass)
{
	if (clip == nullptr || options().mode != Mode::Replace) { return 0; }
	int removed = 0;
	const NoteVector existing = clip->notes();  // a copy: removing changes the clip's list
	for (Note* note : existing)
	{
		const bool before = std::find(beforePass.begin(), beforePass.end(), note->id()) != beforePass.end();
		const bool overlaps = note->pos() < recorded.endPos() && recorded.pos() < note->endPos();
		if (!before || !overlaps) { continue; }
		clip->removeNote(note);
		++removed;
	}
	return removed;
}

tick_t beginPass(Song* song)
{
	const Options o = options();
	const int bars = o.countInBars + o.preRollBars;
	if (bars == 0) { return -1; }
	const tick_t start = song->getPlayPos(Song::PlayMode::Song).getTicks();
	const tick_t back = std::max<tick_t>(0, start - static_cast<tick_t>(bars) * song->ticksPerBar());
	// Captured from the play position on: the punch gate opens there, unless a region is armed.
	if (!song->getTimeline(Song::PlayMode::Song).punchArmed())
	{
		Engine::audioEngine()->setPunchWindow(true, start, std::numeric_limits<tick_t>::max() / 2);
		s_gateOpened = true;
	}
	s_countInFrom.store(back, std::memory_order_relaxed);
	s_countInUntil.store(o.countInBars > 0 ? std::min(start, back + static_cast<tick_t>(o.countInBars) * song->ticksPerBar()) : -1,
		std::memory_order_relaxed);
	song->setPlayPos(back, Song::PlayMode::Song);
	return start;
}

void endPass(Song* song)
{
	s_countInUntil.store(-1, std::memory_order_relaxed);
	if (!s_gateOpened) { return; }
	s_gateOpened = false;
	const Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
	Engine::audioEngine()->setPunchWindow(timeline.punchArmed(), timeline.punchBegin(), timeline.punchEnd());
}

bool countingIn(tick_t tick)
{
	const tick_t until = s_countInUntil.load(std::memory_order_relaxed);
	return until >= 0 && tick >= s_countInFrom.load(std::memory_order_relaxed) && tick < until;
}

} // namespace lmms::recordoptions
