// NeuralAmp harness CPU clock. GPL-2.0-or-later.
#pragma once

#include "HarnessClock.h"
#include <stdexcept>

inline double namThreadCpuMicroseconds()
{
	double value = 0.0;
	if (!lmms::nam::tryThreadCpuTimeUs(value))
	{
		throw std::runtime_error("thread CPU clock failed");
	}
	return value;
}
