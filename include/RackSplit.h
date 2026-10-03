/*
 * RackSplit.h - a rack's chains fed by frequency band, mid / side or left / right
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

#ifndef LMMS_RACK_SPLIT_H
#define LMMS_RACK_SPLIT_H

#include <array>

#include <QDomElement>
#include <QString>
#include <QStringList>

#include "BiquadDesign.h"
#include "LmmsTypes.h"
#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 L10 (MX-108): what each chain of a parallel rack is fed.
 *
 *  - None: every chain gets the whole signal (the rack as it was).
 *  - Bands: chain k of n gets band k of an n-way split at the crossovers (low to high).
 *  - MidSide: chain 0 gets the mid (M, M), chain 1 the side (S, -S); further chains get silence.
 *  - LeftRight: chain 0 gets (L, 0), chain 1 (0, R); further chains get silence.
 *
 *  The rack's sum puts the parts back together: M + S = L and M - S = R, and (L, 0) + (0, R) =
 *  (L, R), exactly; the bands are Linkwitz-Riley (24 dB / octave) crossovers with each lower band
 *  phase-aligned to the ones above it, so they sum flat in level (with the crossovers' phase
 *  shift, as any minimum-phase crossover has). A split rack of empty chains therefore sounds as the
 *  channel did. Only a parallel rack splits: with one chain selected, that chain gets the whole
 *  signal. */
enum class RackSplitMode
{
	None,
	Bands,
	MidSide,
	LeftRight
};

//! A rack's split: the mode and, for bands, the crossover frequencies (ascending, in Hz).
struct LMMS_EXPORT RackSplit
{
	static constexpr int MaxBands = 6;
	static constexpr float MinCrossoverHz = 20.f;
	static constexpr float MaxCrossoverHz = 20000.f;

	RackSplitMode mode = RackSplitMode::None;
	std::array<float, MaxBands - 1> crossovers{};
	int crossoverCount = 0;

	bool active() const { return mode != RackSplitMode::None; }
	//! The frequency between band @a k and band k + 1 of @a bands: the stated crossover, or - when
	//! fewer were stated - one spread evenly in octaves between 120 Hz and 6 kHz.
	float crossover(int k, int bands) const;

	//! Attributes on the rack element, written only for an active split.
	void save(QDomElement& rack) const;
	//! Reads them back; a rack element without them is no split.
	void load(const QDomElement& rack);

	static QString modeName(RackSplitMode mode);
	static bool modeFromName(const QString& name, RackSplitMode* mode);
	static const QStringList& modeNames();
};

/*! The filter in front of one chain of a split rack: a part of the stereo block, made in place.
 *  Configured on the control thread when the rack's graph is built; process() runs on the audio
 *  thread, allocation-free and lock-free (a fixed array of biquad sections in the object). */
class LMMS_EXPORT RackSplitFilter
{
public:
	//! The part chain @a position of @a count routed chains gets from @a split at @a sampleRate.
	void configure(const RackSplit& split, int position, int count, float sampleRate);
	bool active() const { return m_kind != Kind::Pass; }
	//! Whether its output can ring on after its input stops (a band's filters).
	bool rings() const { return m_kind == Kind::Band; }
	//! Replace the interleaved stereo block @a samples (2 x @a frames floats) by this chain's part.
	void process(float* samples, f_cnt_t frames) noexcept;

private:
	enum class Kind
	{
		Pass,
		Silence,
		Band,
		Mid,
		Side,
		Left,
		Right
	};

	//! The most sections a band needs: (bands - 1) 4th-order filters and the all-passes, at most
	//! 2 x 5 sections for six bands.
	static constexpr int MaxStages = 12;

	void addStage(BiquadShape shape, float hz, float sampleRate);
	void configureBands(const RackSplit& split, int position, int count, float sampleRate);
	void processBand(float* samples, f_cnt_t frames) noexcept;

	Kind m_kind = Kind::Pass;
	std::array<BiquadSection<2>, MaxStages> m_stages{};
	int m_stageCount = 0;
};

} // namespace lmms

#endif // LMMS_RACK_SPLIT_H
