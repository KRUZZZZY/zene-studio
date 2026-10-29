/*
 * ConvolutionKernel.cpp - an impulse response cut into FFT partitions, and the frequency-domain delay line it runs on
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

#include "ConvolutionKernel.h"

// MSVC: zene.exe exports std::vector<float> through ValueBuffer (an LMMS_EXPORT class deriving from it). A
// translation unit that never sees that header instantiates the vector's members itself, and the plug-in's link
// then finds them twice (LNK2005 against zene.lib). Seeing the header makes this unit import them like the rest.
#include "ValueBuffer.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace lmms
{

ConvolutionKernel::ConvolutionKernel(const std::vector<float>& left, const std::vector<float>& right) :
	m_length(static_cast<int>(std::max(left.size(), right.size())))
{
	{
		// FFTW's planner API is not safe while the audio thread executes a plan, so the buffers
		// and the plans are made inside the scope that quiesces the audio thread.
		const ConvolutionFftwScope scope;
		m_timeIn.reset(fftwf_malloc(sizeof(float) * FftSize));
		m_timeOut.reset(fftwf_malloc(sizeof(float) * FftSize));
		m_spectrum.reset(fftwf_malloc(sizeof(fftwf_complex) * Bins));
		m_sum.reset(fftwf_malloc(sizeof(fftwf_complex) * Bins));
		if (!m_timeIn || !m_timeOut || !m_spectrum || !m_sum) { throw std::bad_alloc{}; }
		m_forward.reset(fftwf_plan_dft_r2c_1d(FftSize, static_cast<float*>(m_timeIn.get()),
			static_cast<fftwf_complex*>(m_spectrum.get()), FFTW_ESTIMATE));
		if (!m_forward) { throw std::bad_alloc{}; }
		m_inverse.reset(fftwf_plan_dft_c2r_1d(FftSize, static_cast<fftwf_complex*>(m_sum.get()),
			static_cast<float*>(m_timeOut.get()), FFTW_ESTIMATE));
		if (!m_inverse) { throw std::bad_alloc{}; }
		ConvolutionFftwScope::notePlannerCall();
	}
	// The impulse response's own spectra: fftwf_execute is the one thread-safe FFTW routine, so
	// this may run next to the audio thread - and the guard is not held for a long impulse.
	m_partitions = std::max(1, (m_length + Partition - 1) / Partition);
	transformPartitions(left, &m_spectra[0]);
	transformPartitions(right, &m_spectra[1]);
	for (int c = 0; c < 2; ++c)
	{
		m_history[c].assign(static_cast<std::size_t>(m_partitions) * Bins, Complex{});
		m_previous[c].assign(Partition, 0.f);
	}
}

ConvolutionKernel::~ConvolutionKernel()
{
	// The plans and their buffers are FFTW planner calls, so they die inside the scope that
	// quiesces the audio thread. The scope is declared first, so it outlives the members moved
	// out below - they are destroyed with it, in one guard, not one per member.
	const ConvolutionFftwScope scope;
	auto forward = std::move(m_forward);
	auto inverse = std::move(m_inverse);
	auto timeIn = std::move(m_timeIn);
	auto timeOut = std::move(m_timeOut);
	auto spectrum = std::move(m_spectrum);
	auto sum = std::move(m_sum);
}

void ConvolutionKernel::transformPartitions(const std::vector<float>& impulse, std::vector<Complex>* spectra)
{
	auto* timeIn = static_cast<float*>(m_timeIn.get());
	auto* spectrumBuffer = static_cast<fftwf_complex*>(m_spectrum.get());
	spectra->assign(static_cast<std::size_t>(m_partitions) * Bins, Complex{});
	for (int k = 0; k < m_partitions; ++k)
	{
		// The block in the first half, zeros in the second: the linear part of the circular product.
		std::fill(timeIn, timeIn + FftSize, 0.f);
		const auto start = static_cast<std::size_t>(k) * Partition;
		for (std::size_t i = 0; i < static_cast<std::size_t>(Partition) && start + i < impulse.size(); ++i) { timeIn[i] = impulse[start + i]; }
		fftwf_execute(m_forward.get());
		const auto* spectrum = reinterpret_cast<const Complex*>(spectrumBuffer);
		std::copy(spectrum, spectrum + Bins, spectra->data() + static_cast<std::size_t>(k) * Bins);
	}
}

void ConvolutionKernel::convolveChannel(int channel, const float* input, float* output) noexcept
{
	auto* timeIn = static_cast<float*>(m_timeIn.get());
	auto* spectrumBuffer = static_cast<fftwf_complex*>(m_spectrum.get());
	auto* sumBuffer = static_cast<fftwf_complex*>(m_sum.get());
	auto* timeOut = static_cast<float*>(m_timeOut.get());
	// Overlap-save: the previous block and this one, transformed.
	std::memcpy(timeIn, m_previous[channel].data(), sizeof(float) * Partition);
	std::memcpy(timeIn + Partition, input, sizeof(float) * Partition);
	std::memcpy(m_previous[channel].data(), input, sizeof(float) * Partition);
	fftwf_execute(m_forward.get());
	Complex* history = m_history[channel].data();
	const auto* spectrum = reinterpret_cast<const Complex*>(spectrumBuffer);
	std::copy(spectrum, spectrum + Bins, history + static_cast<std::size_t>(m_head) * Bins);
	// The sum over the partitions: input spectrum k blocks ago times the impulse's block k.
	auto* sum = reinterpret_cast<Complex*>(sumBuffer);
	std::fill(sum, sum + Bins, Complex{});
	const Complex* spectra = m_spectra[channel].data();
	for (int k = 0; k < m_partitions; ++k)
	{
		const int slot = (m_head - k + m_partitions) % m_partitions;
		const Complex* x = history + static_cast<std::size_t>(slot) * Bins;
		const Complex* h = spectra + static_cast<std::size_t>(k) * Bins;
		for (int b = 0; b < Bins; ++b) { sum[b] += x[b] * h[b]; }
	}
	fftwf_execute(m_inverse.get());
	// The second half is the valid output; FFTW's inverse is unnormalised.
	constexpr float scale = 1.f / static_cast<float>(FftSize);
	for (int i = 0; i < Partition; ++i) { output[i] = timeOut[Partition + i] * scale; }
}

void ConvolutionKernel::process(const float* const input[2], float* const output[2]) noexcept
{
	convolveChannel(0, input[0], output[0]);
	convolveChannel(1, input[1], output[1]);
	m_head = (m_head + 1) % m_partitions;
}

} // namespace lmms
