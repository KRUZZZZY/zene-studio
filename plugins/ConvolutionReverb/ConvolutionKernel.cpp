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

#include <algorithm>
#include <cstring>

namespace lmms
{

ConvolutionKernel::ConvolutionKernel(const std::vector<float>& left, const std::vector<float>& right) :
	m_length(static_cast<int>(std::max(left.size(), right.size())))
{
	m_timeIn = static_cast<float*>(fftwf_malloc(sizeof(float) * FftSize));
	m_timeOut = static_cast<float*>(fftwf_malloc(sizeof(float) * FftSize));
	m_spectrum = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * Bins));
	m_sum = static_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * Bins));
	m_forward = fftwf_plan_dft_r2c_1d(FftSize, m_timeIn, m_spectrum, FFTW_ESTIMATE);
	m_inverse = fftwf_plan_dft_c2r_1d(FftSize, m_sum, m_timeOut, FFTW_ESTIMATE);
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
	fftwf_destroy_plan(m_forward);
	fftwf_destroy_plan(m_inverse);
	fftwf_free(m_timeIn);
	fftwf_free(m_timeOut);
	fftwf_free(m_spectrum);
	fftwf_free(m_sum);
}

void ConvolutionKernel::transformPartitions(const std::vector<float>& impulse, std::vector<Complex>* spectra)
{
	spectra->assign(static_cast<std::size_t>(m_partitions) * Bins, Complex{});
	for (int k = 0; k < m_partitions; ++k)
	{
		// The block in the first half, zeros in the second: the linear part of the circular product.
		std::fill(m_timeIn, m_timeIn + FftSize, 0.f);
		const auto start = static_cast<std::size_t>(k) * Partition;
		for (std::size_t i = 0; i < static_cast<std::size_t>(Partition) && start + i < impulse.size(); ++i) { m_timeIn[i] = impulse[start + i]; }
		fftwf_execute(m_forward);
		const auto* spectrum = reinterpret_cast<const Complex*>(m_spectrum);
		std::copy(spectrum, spectrum + Bins, spectra->data() + static_cast<std::size_t>(k) * Bins);
	}
}

void ConvolutionKernel::convolveChannel(int channel, const float* input, float* output) noexcept
{
	// Overlap-save: the previous block and this one, transformed.
	std::memcpy(m_timeIn, m_previous[channel].data(), sizeof(float) * Partition);
	std::memcpy(m_timeIn + Partition, input, sizeof(float) * Partition);
	std::memcpy(m_previous[channel].data(), input, sizeof(float) * Partition);
	fftwf_execute(m_forward);
	Complex* history = m_history[channel].data();
	const auto* spectrum = reinterpret_cast<const Complex*>(m_spectrum);
	std::copy(spectrum, spectrum + Bins, history + static_cast<std::size_t>(m_head) * Bins);
	// The sum over the partitions: input spectrum k blocks ago times the impulse's block k.
	auto* sum = reinterpret_cast<Complex*>(m_sum);
	std::fill(sum, sum + Bins, Complex{});
	const Complex* spectra = m_spectra[channel].data();
	for (int k = 0; k < m_partitions; ++k)
	{
		const int slot = (m_head - k + m_partitions) % m_partitions;
		const Complex* x = history + static_cast<std::size_t>(slot) * Bins;
		const Complex* h = spectra + static_cast<std::size_t>(k) * Bins;
		for (int b = 0; b < Bins; ++b) { sum[b] += x[b] * h[b]; }
	}
	fftwf_execute(m_inverse);
	// The second half is the valid output; FFTW's inverse is unnormalised.
	constexpr float scale = 1.f / static_cast<float>(FftSize);
	for (int i = 0; i < Partition; ++i) { output[i] = m_timeOut[Partition + i] * scale; }
}

void ConvolutionKernel::process(const float* const input[2], float* const output[2]) noexcept
{
	convolveChannel(0, input[0], output[0]);
	convolveChannel(1, input[1], output[1]);
	m_head = (m_head + 1) % m_partitions;
}

} // namespace lmms
