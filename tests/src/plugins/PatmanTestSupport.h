/*
 * PatmanTestSupport.h - fixtures shared by the Patman plugin tests
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef PATMAN_TEST_SUPPORT_H
#define PATMAN_TEST_SUPPORT_H

#include <QByteArray>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <span>
#include <vector>

#include "AudioEngine.h"
#include "AudioResampler.h"
#include "Engine.h"
#include "ExportRenderSettings.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Patman.h"
#include "SampleFrame.h"

namespace lmms
{

namespace patman_test
{

constexpr std::size_t MaxArrayAllocationForTest = 64U * 1024U * 1024U;

//! Set while a load test must refuse a huge array allocation. The test binary
//! replaces operator new[]/delete[] with versions that read this flag.
inline std::atomic<bool>& rejectLargeArrayAllocations()
{
	static std::atomic<bool> reject{false};
	return reject;
}

class LargeArrayAllocationGuard
{
public:
	LargeArrayAllocationGuard() { rejectLargeArrayAllocations().store(true); }
	~LargeArrayAllocationGuard() { rejectLargeArrayAllocations().store(false); }
};

constexpr qsizetype PatchHeaderSize = 239;
constexpr qsizetype PatchSampleHeaderSize = 96;

inline void writeLittleEndian16(QByteArray& data, qsizetype offset, std::uint16_t value)
{
	const int at = static_cast<int>(offset);
	for (int i = 0; i < 2; ++i)
	{
		data[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
	}
}

inline void writeLittleEndian32(QByteArray& data, qsizetype offset, std::uint32_t value)
{
	const int at = static_cast<int>(offset);
	for (int i = 0; i < 4; ++i)
	{
		data[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
	}
}

//! A valid GF1 patch. The default four-frame sample keeps the load tests
//! small; the render tests ask for a longer square wave so two consecutive
//! render blocks are both audible.
inline auto makePatch(qsizetype sampleFrames = 4) -> QByteArray
{
	auto patch = QByteArray(PatchHeaderSize + PatchSampleHeaderSize + sampleFrames, '\0');
	std::memcpy(patch.data(), "GF1PATCH110\0ID#000002", 22);
	patch[198] = 1;

	const qsizetype sample = PatchHeaderSize;
	writeLittleEndian32(patch, sample + 8, static_cast<std::uint32_t>(sampleFrames));
	writeLittleEndian32(patch, sample + 16, static_cast<std::uint32_t>(sampleFrames));
	writeLittleEndian16(patch, sample + 20, 22050);
	writeLittleEndian32(patch, sample + 30, 440000);
	const int sampleAt = static_cast<int>(PatchHeaderSize + PatchSampleHeaderSize);
	for (qsizetype i = 0; i < sampleFrames; ++i)
	{
		patch[sampleAt + static_cast<int>(i)] = static_cast<char>((i % 8) < 4 ? 64 : -64);
	}
	return patch;
}

inline bool writePatch(const QString& filename, qsizetype sampleFrames = 4)
{
	QFile file(filename);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	const auto patch = makePatch(sampleFrames);
	if (file.write(patch) != static_cast<qint64>(patch.size())) { return false; }
	file.close();
	return true;
}

//! The patch path the instrument publishes: the part of the patch state the
//! audio thread reads (playNoteImpl checks it before it renders).
inline auto patmanSource(PatmanInstrument& instrument) -> QString
{
	QDomDocument doc;
	QDomElement element = doc.createElement("instrument");
	doc.appendChild(element);
	instrument.saveSettings(doc, element);
	return element.attribute("src");
}

//! The engine entry point the SRC setting uses: it prepares every retained
//! converter (pool, sample voices, instrument notes, play handles) and only
//! then publishes and adopts the new quality.
inline auto switchQuality(SrcQuality quality) -> bool
{
	AudioEngine* const engine = Engine::audioEngine();
	return engine != nullptr
		&& engine->applyConverterModeToRetainedStates(AudioResampler::modeForSrcQuality(quality), quality);
}

inline auto acquireNote(InstrumentTrack& track, f_cnt_t frames) -> NotePlayHandle*
{
	return NotePlayHandleManager::acquire(&track, 0, frames, Note(TimePos(), TimePos(), 60, 100));
}

//! Render exactly one block. Clear first so a short or failed render is
//! reported as silence instead of stale samples from the previous block.
inline void renderBlock(PatmanInstrument& instrument, NotePlayHandle* note, std::vector<SampleFrame>& block)
{
	std::fill(block.begin(), block.end(), SampleFrame{});
	instrument.playNote(note, std::span<SampleFrame>(block.data(), block.size()));
}

inline auto hasAudio(const std::vector<SampleFrame>& block) -> bool
{
	for (const SampleFrame& frame : block)
	{
		if (frame.left() != 0.0f || frame.right() != 0.0f) { return true; }
	}
	return false;
}

inline auto blocksMatch(const std::vector<SampleFrame>& left, const std::vector<SampleFrame>& right) -> bool
{
	return left.size() == right.size()
		&& std::memcmp(left.data(), right.data(), left.size() * sizeof(SampleFrame)) == 0;
}

//! One real Patman note, primed on block 0, with block 1 rendered outside the
//! realtime scope. Every note of the same pitch and patch renders block 1 bit
//! for bit, so this is the deterministic oracle for one quality.
inline auto referenceBlock(InstrumentTrack& track, PatmanInstrument& instrument, f_cnt_t frames)
	-> std::vector<SampleFrame>
{
	std::vector<SampleFrame> block(static_cast<std::size_t>(frames));
	NotePlayHandle* const note = acquireNote(track, frames * 4);
	renderBlock(instrument, note, block); // block 0 primes the converter
	renderBlock(instrument, note, block); // block 1 is the oracle
	instrument.deleteNotePluginData(note);
	note->m_pluginData = nullptr;
	NotePlayHandleManager::release(note);
	return block;
}

} // namespace patman_test

} // namespace lmms

#endif // PATMAN_TEST_SUPPORT_H
