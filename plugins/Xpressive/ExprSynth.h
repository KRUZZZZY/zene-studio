/*
 * exprfront.h - header file to a Frontend to ExprTk
 *
 * Copyright (c) 2016-2017 Orr Dvori
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

#ifndef EXPRSYNTH_H
#define EXPRSYNTH_H

#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include "Graph.h"

namespace lmms
{


class ExprFrontData;
class FloatModel;
class NotePlayHandle;
class SampleFrame;


class ExprFront
{
public:
	using ff1data_functor = float (*)(void*, float);
	ExprFront(const char* expr, int last_func_samples);
	~ExprFront();
	ExprFront(const ExprFront&) = delete;
	ExprFront& operator=(const ExprFront&) = delete;
	bool compile();
	inline bool isValid() { return m_valid; }
	float evaluate();
	bool add_variable(const char* name, float & ref);
	bool add_constant(const char* name, float  ref);
	bool add_cyclic_vector(const char* name, const float* data, size_t length, bool interp = false);
	void setIntegrate(const f_cnt_t* frameCounter, unsigned int sample_rate);
	ExprFrontData* getData() { return m_data; }
private:
	ExprFrontData *m_data = nullptr;
	bool m_valid;
	
	static const int max_float_integer_mask=(1<<(std::numeric_limits<float>::digits))-1;

};

class WaveSample
{
public:
	WaveSample(int length)
	{
		m_length = length;
		m_samples = new float[m_length];
		for(int i = 0 ; i < m_length ; ++i)
			m_samples[i] = 0;
	}
	WaveSample(const graphModel * graph)
	{
		m_length = graph->length();
		m_samples = new float[m_length];
		memcpy(m_samples, graph->samples(), m_length * sizeof(float));
	}
	WaveSample(const WaveSample&) = delete;
	WaveSample& operator=(const WaveSample&) = delete;
	// Waits for any render period in progress: playing notes read m_samples directly.
	void copyFrom(const graphModel * graph);
	~WaveSample()
	{
		delete [] m_samples;
	}
	inline void setInterpolate(bool _interpolate) { m_interpolate = _interpolate; }
	float *m_samples;
	int m_length;
	bool m_interpolate = false;
};

class ExprSynth
{
public:
	ExprSynth(const WaveSample* gW1, const WaveSample* gW2, const WaveSample* gW3, ExprFront* exprO1, ExprFront* exprO2, NotePlayHandle* nph,
			const sample_rate_t sample_rate, const FloatModel* pan1, const FloatModel* pan2, float rel_trans);
	virtual ~ExprSynth();
	ExprSynth(const ExprSynth&) = delete;
	ExprSynth& operator=(const ExprSynth&) = delete;

	void renderOutput(f_cnt_t frames, SampleFrame* buf );

	//! Whether a render error is waiting to be reported. The audio thread records it
	//! (it must not log there), the main thread reports it.
	bool hasPendingError() const { return m_pendingErrorSet.load(std::memory_order_acquire); }
	//! Message of the render error that is waiting to be reported.
	const char* pendingError() const { return m_pendingError; }

private:
	//! Records a render error for a later report. Runs on the audio thread, so it
	//! must not allocate: the message goes into a fixed size buffer.
	void recordRenderError(const char* message);

	ExprFront *m_exprO1, *m_exprO2;
	const WaveSample *m_W1, *m_W2, *m_W3;
	f_cnt_t m_note_sample;
	f_cnt_t m_note_rel_sample;
	//! Whether m_note_rel_sample holds the release position. Sample zero is a valid
	//! release position, so it cannot be used as the "not set yet" marker.
	bool m_note_rel_sample_set;
	float m_note_sample_sec;
	float m_note_rel_sec;
	float m_frequency;
	float m_released;
	NotePlayHandle* m_nph;
	const sample_rate_t m_sample_rate;
	const FloatModel *m_pan1,*m_pan2;
	float m_rel_transition;
	float m_rel_inc;
	//! Render error waiting to be reported. Fixed size, so that recording it on the
	//! audio thread never allocates.
	char m_pendingError[128] = {};
	std::atomic_bool m_pendingErrorSet{false};

} ;



inline float positiveFraction(float x)
{
	if (std::isnan(x) || std::isinf(x))
		return 0;
	const float fractionalPart = std::fmod(x, 1.0f);
	if (fractionalPart == 0.0f)
		return 0.0f;
	if (fractionalPart < 0.0f)
	{
		const float wrapped = fractionalPart + 1.0f;
		return wrapped < 1.0f ? wrapped : 0.0f;
	}
	return fractionalPart;
}

template <typename T>
inline void clearArray(T* arr,unsigned int size)
{
	const T* const arr_end = arr + size;
	while(arr < arr_end)
	{
		*arr=0;
		++arr;
	}
}


} // namespace lmms

#endif
