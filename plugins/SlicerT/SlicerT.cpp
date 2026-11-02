/*
 * SlicerT.cpp - simple slicer plugin
 *
 * Copyright (c) 2023 Daniel Kauss Serna <daniel.kauss.serna@gmail.com>
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

#include "SlicerT.h"

#include <QDomElement>
#include <algorithm>
#include <cmath>
#include <fftw3.h>
#include <numbers>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "PathUtil.h"
#include "SlicerTView.h"
#include "Song.h"
#include "embed.h"
#include "interpolation.h"
#include "plugin_export.h"

namespace lmms {

extern "C" {
Plugin::Descriptor PLUGIN_EXPORT slicert_plugin_descriptor = {
	LMMS_STRINGIFY(PLUGIN_NAME),
	"SlicerT",
	QT_TRANSLATE_NOOP("PluginBrowser", "Basic Slicer"),
	"Daniel Kauss Serna <daniel.kauss.serna@gmail.com>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader("logo"),
	nullptr,
	nullptr,
};
} // end extern

namespace {

//! A 32-bit integer hash (the "lowbias32" mix) mapped to -1..1.
float unitHash(std::uint32_t x) noexcept
{
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return static_cast<float>(x) * (2.0f / 4294967295.0f) - 1.0f;
}

//! One channel of a two-pole state-variable filter (trapezoidal integration, Q = 0.707), low-pass or high-pass.
float svfTick(std::array<float, 2>& state, float x, float a1, float a2, float a3, float k, bool lowpass) noexcept
{
	const float v3 = x - state[1];
	const float v1 = a1 * state[0] + a2 * v3;
	const float v2 = state[1] + a2 * state[0] + a3 * v3;
	state[0] = 2.0f * v1 - state[0];
	state[1] = 2.0f * v2 - state[1];
	return lowpass ? v2 : x - k * v1 - v2;
}

int sliceControlIndex(const QString& name) noexcept
{
	for (std::size_t i = 0; i < slicer::SliceControlCount; ++i)
	{
		if (name == QLatin1String(slicer::SliceControlSpecs[i].name)) { return static_cast<int>(i); }
	}
	return -1;
}

QString sliceAttributeName(std::size_t control, std::size_t slice)
{
	static const char* const prefixes[slicer::SliceControlCount] = {"sliceGain_", "slicePan_", "sliceFilter_",
		"sliceRandom_"};
	return QString::fromLatin1(prefixes[control]) + QString::number(slice);
}

} // namespace

// ################################# SlicerT ####################################

SlicerT::SlicerT(InstrumentTrack* instrumentTrack)
	: Instrument(instrumentTrack, &slicert_plugin_descriptor)
	, m_noteThreshold(0.6f, 0.0f, 2.0f, 0.01f, this, tr("Note threshold"))
	, m_fadeOutFrames(10.0f, 0.0f, 100.0f, 0.1f, this, tr("FadeOut"))
	, m_originalBPM(1, 1, 999, this, tr("Original bpm"))
	, m_sliceSnap(this, tr("Slice snap"))
	, m_enableSync(false, this, tr("BPM sync"))
	, m_originalSample()
	, m_parentTrack(instrumentTrack)
{
	m_sliceSnap.addItem("Off");
	m_sliceSnap.addItem("1/1");
	m_sliceSnap.addItem("1/2");
	m_sliceSnap.addItem("1/4");
	m_sliceSnap.addItem("1/8");
	m_sliceSnap.addItem("1/16");
	m_sliceSnap.addItem("1/32");
	m_sliceSnap.setValue(0);
	resetSliceControls();
}

void SlicerT::applyConverterMode(AudioResampler::Mode mode)
{
	for (PlaybackSlot& slot : m_playbackSlots) { slot.state.setConverterMode(mode); }
}

auto SlicerT::prepareConverterMode(AudioResampler::Mode mode) noexcept -> bool
{
	for (std::size_t slot = 0; slot < m_playbackSlots.size(); ++slot)
	{
		if (m_playbackSlots[slot].state.prepareConverterMode(mode)) { continue; }
		for (std::size_t done = 0; done < slot; ++done)
		{
			m_playbackSlots[done].state.discardPreparedConverterMode();
		}
		return false;
	}
	return true;
}

void SlicerT::adoptPreparedConverterMode() noexcept
{
	for (PlaybackSlot& slot : m_playbackSlots) { slot.state.adoptPreparedConverterMode(); }
}

void SlicerT::discardPreparedConverterMode() noexcept
{
	for (PlaybackSlot& slot : m_playbackSlots) { slot.state.discardPreparedConverterMode(); }
}

SlicerT::PlaybackSlot* SlicerT::acquirePlaybackSlot(int startFrame) noexcept
{
	for (PlaybackSlot& slot : m_playbackSlots)
	{
		bool expected = false;
		if (slot.inUse.compare_exchange_strong(expected, true, std::memory_order_acquire,
			std::memory_order_relaxed))
		{
			slot.state.setFrameIndex(startFrame);
			// This trigger's own random draws (-1..1): a hash of a running trigger count, so repeated
			// triggers differ, any thread can take one without a lock, and nothing allocates.
			const std::uint32_t trigger = m_triggerCounter.fetch_add(1, std::memory_order_relaxed);
			slot.voice = SliceVoice{};
			slot.voice.randomGain = unitHash(trigger * 2);
			slot.voice.randomPan = unitHash(trigger * 2 + 1);
			return &slot;
		}
	}
	return nullptr;
}

void SlicerT::playNoteImpl(NotePlayHandle* handle, std::span<SampleFrame> out)
{
	if (m_originalSample.sampleSize() <= 1) { return; }

	int noteIndex = handle->key() - m_parentTrack->baseNote();
	const f_cnt_t frames = handle->framesLeftForCurrentPeriod();
	const f_cnt_t offset = handle->noteOffset();
	const int bpm = Engine::getSong()->getTempo();
	const float pitchRatio = 1 / std::exp2(m_parentTrack->pitchModel()->value() / 1200);

	float speedRatio = static_cast<float>(m_originalBPM.value()) / bpm;
	if (!m_enableSync.value()) { speedRatio = 1; }
	speedRatio *= pitchRatio;

	float sliceStart, sliceEnd;
	int sliceIndex = -1; // the base note plays the whole sample: no per-slice controls
	if (noteIndex == 0) // full sample at base note
	{
		sliceStart = 0;
		sliceEnd = 1;
	}
	else if (noteIndex > 0 && static_cast<std::size_t>(noteIndex) < m_slicePoints.size())
	{
		noteIndex -= 1;
		sliceIndex = noteIndex;
		sliceStart = m_slicePoints[noteIndex];
		sliceEnd = m_slicePoints[noteIndex + 1];
	}
	else
	{
		publishPlayhead(-1, 0, 0);
		return;
	}

	const auto startFrame = static_cast<int>(sliceStart * m_originalSample.sampleSize());
	if (handle->m_pluginData == &m_noPlaybackSlotTag)
	{
		publishPlayhead(-1, 0, 0);
		return;
	}
	if (handle->m_pluginData == nullptr)
	{
		auto* slot = acquirePlaybackSlot(startFrame);
		if (slot == nullptr)
		{
			handle->m_pluginData = &m_noPlaybackSlotTag;
			Engine::audioEngine()->recordSlicerTStateOverflow();
			publishPlayhead(-1, 0, 0);
			return;
		}
		handle->m_pluginData = slot;
	}

	auto* playbackSlot = static_cast<PlaybackSlot*>(handle->m_pluginData);
	auto* playbackState = &playbackSlot->state;
	const auto endFrame = sliceEnd * m_originalSample.sampleSize();
	const auto framesLeft = endFrame - playbackState->frameIndex();

	if (framesLeft > 0
		&& m_originalSample.play(out.data() + offset, playbackState, frames, Sample::Loop::Off, speedRatio))
	{
		// exponential fade out, applyRelease() not used since it extends the note length
		// At least one frame: a FadeOut of 0 divided 0 by 0 on the slice's last frame, a NaN in the output.
		const int fadeOutFrames = std::max(1, static_cast<int>(m_fadeOutFrames.value() / 1000.0f
			* Engine::audioEngine()->outputSampleRate()));
		for (auto i = std::size_t{0}; i < frames; i++)
		{
			float fadeValue = static_cast<float>(framesLeft * speedRatio - static_cast<int>(i)) / fadeOutFrames;
			fadeValue = std::clamp(fadeValue, 0.0f, 1.0f);
			fadeValue = cosinusInterpolate(0, 1, fadeValue);

			out[i + offset][0] *= fadeValue;
			out[i + offset][1] *= fadeValue;
		}

		if (sliceIndex >= 0 && static_cast<std::size_t>(sliceIndex) < slicer::MaxSlices)
		{
			applySliceControls(playbackSlot->voice, sliceIndex, out.data() + offset, frames);
		}

		const auto currentNote = static_cast<float>(playbackState->frameIndex()) / m_originalSample.sampleSize();
		publishPlayhead(currentNote, sliceStart, sliceEnd);
	}
	else { publishPlayhead(-1, 0, 0); }
}

void SlicerT::deleteNotePluginData(NotePlayHandle* handle)
{
	if (handle->m_pluginData == &m_noPlaybackSlotTag)
	{
		handle->m_pluginData = nullptr;
		return;
	}
	if (handle->m_pluginData != nullptr)
	{
		auto* slot = static_cast<PlaybackSlot*>(handle->m_pluginData);
		slot->state.reset();
		slot->inUse.store(false, std::memory_order_release);
		handle->m_pluginData = nullptr;
	}
	publishPlayhead(-1, 0, 0);
}

void SlicerT::applySliceControls(SliceVoice& voice, int slice, SampleFrame* block, f_cnt_t frames) noexcept
{
	using namespace slicer;
	const SliceBank& bank = m_sliceControls[static_cast<std::size_t>(slice)];
	const float gainDb = bank[GainControl].load(std::memory_order_relaxed);
	const float pan = bank[PanControl].load(std::memory_order_relaxed);
	const float filter = bank[FilterControl].load(std::memory_order_relaxed);
	const float random = bank[RandomControl].load(std::memory_order_relaxed);
	if (gainDb == 0.f && pan == 0.f && filter == 0.f && random == 0.f) { return; }

	if (filter != 0.f)
	{
		const float sampleRate = static_cast<float>(Engine::audioEngine()->outputSampleRate());
		const bool lowpass = filter < 0.f;
		const float amount = std::abs(filter);
		const float cutoff = lowpass
			? LowpassCeilingHz * std::pow(LowpassFloorHz / LowpassCeilingHz, amount)
			: HighpassFloorHz * std::pow(HighpassCeilingHz / HighpassFloorHz, amount);
		const float g = std::tan(std::numbers::pi_v<float> * std::min(cutoff, sampleRate * 0.45f) / sampleRate);
		const float k = 1.41421356f; // 1 / Q, Q = 0.707
		const float a1 = 1.0f / (1.0f + g * (g + k));
		const float a2 = g * a1;
		const float a3 = g * a2;
		for (f_cnt_t i = 0; i < frames; ++i)
		{
			block[i][0] = svfTick(voice.filterState[0], block[i][0], a1, a2, a3, k, lowpass);
			block[i][1] = svfTick(voice.filterState[1], block[i][1], a1, a2, a3, k, lowpass);
		}
	}

	const float gain = std::pow(10.0f, (gainDb + voice.randomGain * random * RandomGainDb) / 20.0f);
	const float balance = std::clamp(pan + voice.randomPan * random * RandomPan, -1.0f, 1.0f);
	const float left = gain * (balance > 0.f ? 1.0f - balance : 1.0f);
	const float right = gain * (balance < 0.f ? 1.0f + balance : 1.0f);
	if (left == 1.0f && right == 1.0f) { return; }
	for (f_cnt_t i = 0; i < frames; ++i)
	{
		block[i][0] *= left;
		block[i][1] *= right;
	}
}

void SlicerT::resetSliceControls() noexcept
{
	for (SliceBank& bank : m_sliceControls)
	{
		for (std::size_t c = 0; c < slicer::SliceControlCount; ++c)
		{
			bank[c].store(static_cast<float>(slicer::SliceControlSpecs[c].neutral), std::memory_order_relaxed);
		}
	}
}

// slice point pointIndex (1..n-2) was deleted: slices pointIndex-1 and pointIndex are one slice now, which
// keeps the earlier one's controls.
void SlicerT::sliceMerged(int pointIndex) noexcept
{
	const auto first = static_cast<std::size_t>(std::max(pointIndex, 1));
	for (std::size_t i = first; i + 1 < slicer::MaxSlices; ++i)
	{
		for (std::size_t c = 0; c < slicer::SliceControlCount; ++c)
		{
			m_sliceControls[i][c].store(m_sliceControls[i + 1][c].load(std::memory_order_relaxed),
				std::memory_order_relaxed);
		}
	}
	for (std::size_t c = 0; c < slicer::SliceControlCount; ++c)
	{
		m_sliceControls[slicer::MaxSlices - 1][c].store(
			static_cast<float>(slicer::SliceControlSpecs[c].neutral), std::memory_order_relaxed);
	}
}

// a slice point was inserted at pointIndex: slice pointIndex-1 is split in two, and both halves keep its controls.
void SlicerT::sliceSplit(int pointIndex) noexcept
{
	const auto first = static_cast<std::size_t>(std::max(pointIndex, 1));
	for (std::size_t i = slicer::MaxSlices - 1; i >= first; --i)
	{
		for (std::size_t c = 0; c < slicer::SliceControlCount; ++c)
		{
			m_sliceControls[i][c].store(m_sliceControls[i - 1][c].load(std::memory_order_relaxed),
				std::memory_order_relaxed);
		}
	}
}

int SlicerT::sliceCount() const
{
	if (m_slicePoints.size() < 2) { return 0; }
	return static_cast<int>(std::min(m_slicePoints.size() - 1, slicer::MaxSlices));
}

double SlicerT::slicePoint(int index) const
{
	if (index < 0 || static_cast<std::size_t>(index) >= m_slicePoints.size()) { return -1.0; }
	return static_cast<double>(m_slicePoints[static_cast<std::size_t>(index)]);
}

double SlicerT::sliceControl(int slice, const QString& name) const
{
	const int control = sliceControlIndex(name);
	if (control < 0 || slice < 0 || slice >= sliceCount()) { return std::nan(""); }
	return static_cast<double>(
		m_sliceControls[static_cast<std::size_t>(slice)][static_cast<std::size_t>(control)].load(
			std::memory_order_relaxed));
}

bool SlicerT::setSliceControl(int slice, const QString& name, double value)
{
	const int control = sliceControlIndex(name);
	if (control < 0 || slice < 0 || slice >= sliceCount()) { return false; }
	const auto& spec = slicer::SliceControlSpecs[static_cast<std::size_t>(control)];
	// Compared as it is stored, a float: the range's own ends are accepted.
	const auto stored = static_cast<float>(value);
	if (!std::isfinite(value) || stored < static_cast<float>(spec.min) || stored > static_cast<float>(spec.max))
	{
		return false;
	}
	m_sliceControls[static_cast<std::size_t>(slice)][static_cast<std::size_t>(control)].store(
		stored, std::memory_order_relaxed);
	emit dataChanged();
	return true;
}

void SlicerT::publishPlayhead(float current, float start, float end) noexcept
{
	m_playheadCurrent.store(current, std::memory_order_relaxed);
	m_playheadStart.store(start, std::memory_order_relaxed);
	m_playheadEnd.store(end, std::memory_order_relaxed);
}

void SlicerT::pollPlayhead()
{
	const float current = m_playheadCurrent.load(std::memory_order_relaxed);
	const float start = m_playheadStart.load(std::memory_order_relaxed);
	const float end = m_playheadEnd.load(std::memory_order_relaxed);
	if (current == m_shownCurrent && start == m_shownStart && end == m_shownEnd) { return; }
	m_shownCurrent = current;
	m_shownStart = start;
	m_shownEnd = end;
	emit isPlaying(current, start, end);
}

// uses the spectral flux to determine the change in magnitude
// resources:
// http://www.iro.umontreal.ca/~pift6080/H09/documents/papers/bello_onset_tutorial.pdf
void SlicerT::findSlices()
{
	if (m_originalSample.sampleSize() <= 1) { return; }
	m_slicePoints = {};
	resetSliceControls(); // the slices are new: so are their controls

	const int windowSize = 512;
	const float minBeatLength = 0.05f; // in seconds, ~ 1/4 length at 220 bpm

	int sampleRate = m_originalSample.sampleRate();
	int minDist = sampleRate * minBeatLength;

	float maxMag = -1;
	std::vector<float> singleChannel(m_originalSample.sampleSize(), 0);
	for (auto i = std::size_t{0}; i < m_originalSample.sampleSize(); i++)
	{
		singleChannel[i] = (m_originalSample.data()[i][0] + m_originalSample.data()[i][1]) / 2;
		maxMag = std::max(maxMag, singleChannel[i]);
	}

	// normalize and find 0 crossings
	std::vector<int> zeroCrossings;
	float lastValue = 1;
	for (auto i = std::size_t{0}; i < singleChannel.size(); i++)
	{
		singleChannel[i] /= maxMag;
		if ((lastValue >= 0) != (singleChannel[i] >= 0))
		{
			zeroCrossings.push_back(i);
			lastValue = singleChannel[i];
		}
	}

	std::vector<float> prevMags(windowSize / 2, 0);
	std::vector<float> fftIn(windowSize, 0);
	std::array<fftwf_complex, windowSize> fftOut;

	fftwf_plan fftPlan = fftwf_plan_dft_r2c_1d(windowSize, fftIn.data(), fftOut.data(), FFTW_ESTIMATE);

	int lastPoint = -minDist - 1; // to always store 0 first
	float spectralFlux = 0;
	float prevFlux = 1E-10f; // small value, no divison by zero

	for (int i = 0; i < static_cast<int>(singleChannel.size()) - windowSize; i += windowSize)
	{
		// fft
		std::copy_n(singleChannel.data() + i, windowSize, fftIn.data());
		fftwf_execute(fftPlan);

		// calculate spectral flux in regard to last window
		for (int j = 0; j < windowSize / 2; j++) // only use niquistic frequencies
		{
			float real = fftOut[j][0];
			float imag = fftOut[j][1];
			float magnitude = std::sqrt(real * real + imag * imag);

			// using L2-norm (euclidean distance)
			float diff = std::abs(magnitude - prevMags[j]);
			spectralFlux += diff;

			prevMags[j] = magnitude;
		}

		if (spectralFlux / prevFlux > 1.0f + m_noteThreshold.value() && i - lastPoint > minDist)
		{
			m_slicePoints.push_back(i);
			lastPoint = i;
			if (m_slicePoints.size() > 128) { break; } // no more keys on the keyboard
		}

		prevFlux = spectralFlux;
		spectralFlux = 1E-10f; // again for no divison by zero
	}

	m_slicePoints.push_back(m_originalSample.sampleSize());

	for (float& sliceValue : m_slicePoints)
	{
		auto closestZeroCrossing = std::lower_bound(zeroCrossings.begin(), zeroCrossings.end(), sliceValue);
		if (closestZeroCrossing == zeroCrossings.end()) { continue; }
		if (std::abs(sliceValue - *closestZeroCrossing) < windowSize) { sliceValue = *closestZeroCrossing; }
	}

	float beatsPerMin = m_originalBPM.value() / 60.0f;
	float samplesPerBeat = m_originalSample.sampleRate() / beatsPerMin * 4.0f;
	int noteSnap = m_sliceSnap.value();
	int sliceLock = samplesPerBeat / std::exp2(noteSnap + 1);
	if (noteSnap == 0) { sliceLock = 1; }
	for (float& sliceValue : m_slicePoints)
	{
		sliceValue += sliceLock / 2.f;
		sliceValue -= static_cast<int>(sliceValue) % sliceLock;
	}

	m_slicePoints.erase(std::unique(m_slicePoints.begin(), m_slicePoints.end()), m_slicePoints.end());

	for (float& sliceIndex : m_slicePoints)
	{
		sliceIndex /= m_originalSample.sampleSize();
	}

	m_slicePoints[0] = 0;
	m_slicePoints[m_slicePoints.size() - 1] = 1;

	emit dataChanged();
}

// find the bpm of the sample by assuming its in 4/4 time signature ,
// and lies in the 100 - 200 bpm range
void SlicerT::findBPM()
{
	if (m_originalSample.sampleSize() <= 1) { return; }

	float sampleRate = m_originalSample.sampleRate();
	float totalFrames = m_originalSample.sampleSize();
	float sampleLength = totalFrames / sampleRate;

	float bpmEstimate = 240.0f / sampleLength;

	while (bpmEstimate < 100)
	{
		bpmEstimate *= 2;
	}

	while (bpmEstimate > 200)
	{
		bpmEstimate /= 2;
	}

	m_originalBPM.setValue(bpmEstimate);
	m_originalBPM.setInitValue(bpmEstimate);
}

std::vector<Note> SlicerT::getMidi()
{
	std::vector<Note> outputNotes;

	float speedRatio = static_cast<float>(m_originalBPM.value()) / Engine::getSong()->getTempo();
	float outFrames = m_originalSample.sampleSize() * speedRatio;

	float framesPerTick = Engine::framesPerTick();
	float totalTicks = outFrames / framesPerTick;
	float lastEnd = 0;

	for (auto i = std::size_t{0}; i < m_slicePoints.size() - 1; i++)
	{
		float sliceStart = lastEnd;
		float sliceEnd = totalTicks * m_slicePoints[i + 1];

		Note sliceNote = Note();
		sliceNote.setKey(i + m_parentTrack->baseNote() + 1);
		sliceNote.setPos(sliceStart);
		sliceNote.setLength(sliceEnd - sliceStart + 1); // + 1 so that the notes allign
		outputNotes.push_back(sliceNote);

		lastEnd = sliceEnd;
	}

	return outputNotes;
}

void SlicerT::updateFile(QString file)
{
	if (auto buffer = SampleBuffer::fromFile(file)) { m_originalSample = Sample(std::move(buffer)); }

	findBPM();
	findSlices();

	emit dataChanged();
}

void SlicerT::loadFile(const QString& file)
{
	updateFile(file);
}

void SlicerT::updateSlices()
{
	findSlices();
}

void SlicerT::saveSettings(QDomDocument& document, QDomElement& element)
{
	element.setAttribute("version", "1");
	element.setAttribute("src", m_originalSample.sampleFile());
	if (m_originalSample.sampleFile().isEmpty())
	{
		element.setAttribute("sampledata", m_originalSample.toBase64());
	}

	element.setAttribute("totalSlices", static_cast<int>(m_slicePoints.size()));
	for (auto i = std::size_t{0}; i < m_slicePoints.size(); i++)
	{
		element.setAttribute(QString("slice_%1").arg(i), m_slicePoints[i]);
	}

	// Q050-263: only controls that are not neutral are written, so a project that never touched them saves
	// exactly what it always did, and a project without them loads with every slice neutral.
	for (std::size_t slice = 0; slice < slicer::MaxSlices; ++slice)
	{
		for (std::size_t c = 0; c < slicer::SliceControlCount; ++c)
		{
			const float value = m_sliceControls[slice][c].load(std::memory_order_relaxed);
			if (value != static_cast<float>(slicer::SliceControlSpecs[c].neutral))
			{
				element.setAttribute(sliceAttributeName(c, slice), value);
			}
		}
	}

	m_fadeOutFrames.saveSettings(document, element, "fadeOut");
	m_noteThreshold.saveSettings(document, element, "threshold");
	m_originalBPM.saveSettings(document, element, "origBPM");
	m_enableSync.saveSettings(document, element, "syncEnable");
}

void SlicerT::loadSettings(const QDomElement& element)
{
	if (auto srcFile = element.attribute("src"); !srcFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(srcFile)).exists())
		{
			auto buffer = SampleBuffer::fromFile(srcFile);
			m_originalSample = Sample(std::move(buffer));
		}
		else
		{
			QString message = tr("Sample not found: %1").arg(srcFile);
			Engine::getSong()->collectError(message);
		}
	}
	else if (auto sampleData = element.attribute("sampledata"); !sampleData.isEmpty())
	{
		auto buffer = SampleBuffer::fromBase64(sampleData);
		m_originalSample = Sample(std::move(buffer));
	}

	if (!element.attribute("totalSlices").isEmpty())
	{
		int totalSlices = element.attribute("totalSlices").toInt();
		m_slicePoints = {};
		for (int i = 0; i < totalSlices; i++)
		{
			m_slicePoints.push_back(element.attribute(QString("slice_%1").arg(i)).toFloat());
		}
	}

	// Q050-263: every slice starts neutral, so state without the attributes (an old project, or an undo to
	// before they were set) leaves nothing behind.
	resetSliceControls();
	for (std::size_t slice = 0; slice < slicer::MaxSlices; ++slice)
	{
		for (std::size_t c = 0; c < slicer::SliceControlCount; ++c)
		{
			const QString name = sliceAttributeName(c, slice);
			if (!element.hasAttribute(name)) { continue; }
			const auto& spec = slicer::SliceControlSpecs[c];
			const float value = element.attribute(name).toFloat();
			if (!std::isfinite(value)) { continue; }
			m_sliceControls[slice][c].store(
				std::clamp(value, static_cast<float>(spec.min), static_cast<float>(spec.max)),
				std::memory_order_relaxed);
		}
	}

	m_fadeOutFrames.loadSettings(element, "fadeOut");
	m_noteThreshold.loadSettings(element, "threshold");
	m_originalBPM.loadSettings(element, "origBPM");
	m_enableSync.loadSettings(element, "syncEnable");

	emit dataChanged();
}

QString SlicerT::nodeName() const
{
	return slicert_plugin_descriptor.name;
}

gui::PluginView* SlicerT::instantiateView(QWidget* parent)
{
	return new gui::SlicerTView(this, parent);
}

extern "C" {
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* m, void*)
{
	return new SlicerT(static_cast<InstrumentTrack*>(m));
}
} // extern
} // namespace lmms
