/*
 * WaveShaperControls.h - controls for WaveShaper effect
 *
 * Copyright (c) 2014 Vesa Kivimäki <contact/dot/diizy/at/nbl/dot/fi>
 * Copyright (c) 2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef WAVESHAPER_CONTROLS_H
#define WAVESHAPER_CONTROLS_H

#include "EffectControls.h"
#include "WaveShaperControlDialog.h"
#include "Graph.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

namespace lmms
{


class WaveShaperEffect;


class WaveShaperControls : public EffectControls
{
	Q_OBJECT
public:
	WaveShaperControls( WaveShaperEffect * _eff );
	~WaveShaperControls() override = default;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;
	inline QString nodeName() const override
	{
		return( "waveshapercontrols" );
	}

	virtual void setDefaultShape();

	int controlCount() override
	{
		return( 4 );
	}

	gui::EffectControlDialog* createView() override
	{
		return( new gui::WaveShaperControlDialog( this ) );
	}


	//! The immutable curve the audio thread reads, mirroring Vibed's
	//! WaveformSnapshot hand-off.
	//!
	//! One writer (the GUI thread, where every samplesChanged is emitted), one
	//! reader (the audio thread), no lock, no allocation and no logging on the
	//! audio thread: it takes the published curve with one atomic load.
	//!
	//! Only this plug-in uses it.
	class WaveCurveSnapshot
	{
	public:
		//! Number of floats in one curve.
		static constexpr int s_curveLength = 200;

		WaveCurveSnapshot()
		{
			m_published.store(new Curve{}, std::memory_order_release);
		}

		~WaveCurveSnapshot()
		{
			delete m_published.load(std::memory_order_relaxed);
			for (auto* curve : m_retired) { delete curve; }
			for (auto* curve : m_free) { delete curve; }
		}

		//! The published curve: the audio thread takes it and reads it.
		const float* samples()
		{
			// Announce the curve before the writer may reuse it: the writer
			// keeps the announced one out of the free list until the next read.
			m_readerReading.store(true, std::memory_order_seq_cst);
			Curve* published = m_published.load(std::memory_order_seq_cst);
			m_readerCurve.store(published, std::memory_order_seq_cst);
			m_readerReading.store(false, std::memory_order_seq_cst);
			return published->data.data();
		}

		//! Hand off `s_curveLength` samples as the current curve.
		void publish(const float* samples)
		{
			Curve* fresh = takeFree();
			std::copy_n(samples, s_curveLength, fresh->data.begin());
			Curve* old = m_published.exchange(fresh, std::memory_order_seq_cst);
			m_retired.push_back(old);
			reclaim();
		}

	private:
		//! One immutable curve.
		struct Curve
		{
			std::array<float, s_curveLength> data;
		};

		//! A curve nobody can be reading: a free one, or a new one.
		Curve* takeFree()
		{
			if (m_free.empty()) { return new Curve{}; }
			Curve* curve = m_free.back();
			m_free.pop_back();
			return curve;
		}

		//! The published curves the reader cannot be using any more.
		void reclaim()
		{
			// While the reader is between taking a curve and announcing it,
			// the writer cannot tell which one it took, so it reuses none.
			if (m_readerReading.load(std::memory_order_seq_cst)) { return; }
			Curve* held = m_readerCurve.load(std::memory_order_seq_cst);
			for (auto it = m_retired.begin(); it != m_retired.end(); )
			{
				if (*it == held) { ++it; continue; }
				m_free.push_back(*it);
				it = m_retired.erase(it);
			}
		}

		//! The curve the audio thread reads.
		std::atomic<Curve*> m_published{nullptr};
		//! The curve the reader last took: it may still be reading it.
		std::atomic<Curve*> m_readerCurve{nullptr};
		//! True while the reader is between taking a curve and announcing it.
		std::atomic<bool> m_readerReading{false};
		//! Published curves waiting to be reused. GUI thread only.
		std::vector<Curve*> m_retired;
		//! Curves that may be written into. GUI thread only.
		std::vector<Curve*> m_free;
	};


private slots:
	void samplesChanged( int, int );

	void resetClicked();
	void smoothClicked();

	void addOneClicked();
	void subOneClicked();

private:
	//! Re-read the graph model and hand the curve off to the audio thread.
	void publishCurve();

	WaveShaperEffect * m_effect;
	FloatModel m_inputModel;
	FloatModel m_outputModel;
	graphModel m_wavegraphModel;
	BoolModel  m_clipModel;
	WaveCurveSnapshot m_curveSnapshot;

	friend class gui::WaveShaperControlDialog;
	friend class WaveShaperEffect;

} ;


} // namespace lmms

#endif
