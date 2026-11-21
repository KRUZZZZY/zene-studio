/*
 * ConvolutionKernel.h - an impulse response cut into FFT partitions, and the frequency-domain delay line it runs on
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

#ifndef LMMS_CONVOLUTION_KERNEL_H
#define LMMS_CONVOLUTION_KERNEL_H

#include <complex>
#include <memory>
#include <type_traits>
#include <vector>

#include <fftw3.h>

#include "ConvolutionFftw.h"

namespace lmms
{

/*! 0.5.0 L10 (MX-152): uniformly partitioned convolution (overlap-save). The impulse response is
 *  cut into blocks of Partition samples; each block's spectrum (FftSize = 2 x Partition) is kept,
 *  and every Partition input samples one FFT, a multiply-add against every block's spectrum over
 *  the delay line of past input spectra, and one inverse FFT give the next Partition output samples.
 *  The latency is one Partition.
 *
 *  A kernel is built whole on the control thread and handed to the audio thread under the engine's
 *  model-change guard; process() then allocates nothing. A kernel's delay line starts silent, so
 *  a new impulse response starts with no tail.
 *
 *  Its FFTW plans are made and destroyed inside a ConvolutionFftwScope, because FFTW's planner
 *  API may not run while the audio thread executes a plan (the scope takes the engine's
 *  model-change guard, which the render thread holds for every period it renders). The impulse
 *  response's own spectra are computed with fftwf_execute - the one thread-safe FFTW routine -
 *  so they may be computed next to the audio thread, without holding the guard for a long
 *  impulse response. */
class ConvolutionKernel
{
public:
	static constexpr int Partition = 256;
	static constexpr int FftSize = 2 * Partition;
	static constexpr int Bins = Partition + 1;

	//! The impulse response @a left / @a right (the same length; one channel each).
	ConvolutionKernel(const std::vector<float>& left, const std::vector<float>& right);
	~ConvolutionKernel();
	ConvolutionKernel(const ConvolutionKernel&) = delete;
	ConvolutionKernel& operator=(const ConvolutionKernel&) = delete;

	//! The impulse response's length in frames (its tail).
	int length() const { return m_length; }

	//! One block: @a input (Partition samples per channel) in, the next @a output block out.
	//! Audio thread: FFTW's execute on the kernel's own buffers, no allocation.
	void process(const float* const input[2], float* const output[2]) noexcept;

private:
	using Complex = std::complex<float>;
	//! FFTW's allocator is part of the planner API: freeing a buffer needs the scope too.
	struct FftwFree
	{
		void operator()(void* allocation) const noexcept
		{
			if (allocation == nullptr) { return; }
			const ConvolutionFftwScope scope;
			ConvolutionFftwScope::notePlannerCall();
			fftwf_free(allocation);
		}
	};
	//! Destroying a plan is a planner call: it needs the scope as well.
	struct FftwPlanFree
	{
		void operator()(std::remove_pointer_t<fftwf_plan>* plan) const noexcept
		{
			if (plan == nullptr) { return; }
			const ConvolutionFftwScope scope;
			ConvolutionFftwScope::notePlannerCall();
			fftwf_destroy_plan(plan);
		}
	};
	using FftwBuffer = std::unique_ptr<void, FftwFree>;
	using FftwPlanHandle = std::unique_ptr<std::remove_pointer_t<fftwf_plan>, FftwPlanFree>;

	void transformPartitions(const std::vector<float>& impulse, std::vector<Complex>* spectra);
	void convolveChannel(int channel, const float* input, float* output) noexcept;

	int m_length = 0;
	int m_partitions = 0;
	int m_head = 0;
	std::vector<Complex> m_spectra[2];   //!< m_partitions x Bins per channel
	std::vector<Complex> m_history[2];   //!< the delay line of input spectra, the same size
	std::vector<float> m_previous[2];    //!< the last input block per channel (the overlap)
	FftwBuffer m_timeIn;                 //!< FftSize
	FftwBuffer m_spectrum;               //!< Bins
	FftwBuffer m_sum;                    //!< Bins
	FftwBuffer m_timeOut;                //!< FftSize
	FftwPlanHandle m_forward;
	FftwPlanHandle m_inverse;
};

} // namespace lmms

#endif // LMMS_CONVOLUTION_KERNEL_H
