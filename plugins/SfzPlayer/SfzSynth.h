/*
 * SfzSynth.h - one sfizz synthesizer, configured for the engine, and the MIDI events it plays
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_SFZ_SYNTH_H
#define LMMS_SFZ_SYNTH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "LmmsTypes.h"
#include "SampleFrame.h"

namespace sfz { class Sfizz; }

namespace lmms
{

//! One MIDI event for the SFZ engine, queued from any thread and played on the render thread.
struct SfzEvent
{
	enum class Kind : std::uint8_t
	{
		NoteOn,
		NoteOff,
		Controller,
		PitchBend,
		ChannelPressure,
		KeyPressure,
		ProgramChange,
	};

	Kind kind = Kind::NoteOn;
	std::uint8_t number = 0;  //!< key, controller number or program
	std::uint8_t value = 0;   //!< velocity, controller value or pressure
	std::int16_t bend = 0;    //!< pitch bend, -8192..8191
	std::uint32_t offset = 0; //!< frames from the start of the period the event belongs to
};

/*! One sfizz synthesizer with the engine's sample rate, polyphony and oversampling, and the SFZ it was loaded
 *  from. sfizz allows a loaded synth to be played by one render thread and nothing else; everything that
 *  reconfigures or reloads (the constructor, load()) therefore happens before the synth is handed to the
 *  render thread, and the player replaces a whole SfzSynth rather than changing a live one.
 *
 *  Render-thread members (apply, render, setVolume, setSampleQuality, setFreewheeling) do not allocate, lock or
 *  touch a file: sfizz streams sample data from its own worker threads and the render thread only ever reads
 *  what they have already delivered.
 */
class SfzSynth
{
public:
	struct Settings
	{
		int sampleRate = 44100;
		int polyphony = 64;
		int oversampling = 1; //!< 1, 2, 4 or 8
		int quality = 2;      //!< sfizz sample quality for live playback, 1..10
		//! Frames of each sample kept in memory from the start; the rest streams from the file on sfizz's workers. An export
		//! asks for WholeSamples so nothing has to be waited for while it renders faster than real time.
		std::uint32_t preloadFrames = 8192;
	};

	//! The most frames one sfizz render call is given; longer periods are rendered in pieces.
	static constexpr std::size_t ChunkFrames = 1024;
	//! A preload size no sample reaches (sfizz clamps it to the file's length): the whole sample is read at load time.
	static constexpr std::uint32_t WholeSamples = 1u << 30;

	explicit SfzSynth(const Settings& settings);
	~SfzSynth();
	SfzSynth(const SfzSynth&) = delete;
	SfzSynth& operator=(const SfzSynth&) = delete;

	//! Control thread, before the synth is published: parse \a path and preload its samples' first frames.
	bool load(const std::string& path);

	int regionCount() const;
	int groupCount() const;
	//! The frames of each sample sfizz keeps in memory from the start (the rest streams); WholeSamples during an export.
	std::uint32_t preloadFrames() const;
	//! Differs for every SfzSynth ever built: the render thread tells a replaced synth from the one it last configured.
	std::uint64_t id() const noexcept { return m_id; }

	//! Render thread: one MIDI event, \a delay frames into the next render() call.
	void apply(const SfzEvent& event, int delay) noexcept;
	//! Render thread: \a frames (at most ChunkFrames) of audio, written over \a out.
	void render(SampleFrame* out, f_cnt_t frames) noexcept;

	void setVolume(float decibels) noexcept;
	void setSampleQuality(int quality) noexcept;

private:
	std::uint64_t m_id;
	std::unique_ptr<sfz::Sfizz> m_synth;
	std::array<float, ChunkFrames> m_left{};
	std::array<float, ChunkFrames> m_right{};
	std::array<float*, 2> m_planes{};
};

} // namespace lmms

#endif // LMMS_SFZ_SYNTH_H
