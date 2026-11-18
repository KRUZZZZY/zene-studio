/*
 * SidPlaybackScheduler.h - cycle-budgeted SID register writes
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_SID_PLAYBACK_SCHEDULER_H
#define LMMS_SID_PLAYBACK_SCHEDULER_H

#include <algorithm>
#include <array>
#include <cstddef>

namespace lmms::sidplayback
{

inline constexpr int NumSidRegisters = 0x19;
inline constexpr int RegisterWriteDelay = 9;
inline constexpr int WaveformReadDelay = 4;
inline constexpr std::array<unsigned char, NumSidRegisters> RegisterWriteOrder{
	0x15, 0x16, 0x18, 0x17,
	0x05, 0x06, 0x02, 0x03, 0x00, 0x01, 0x04,
	0x0c, 0x0d, 0x09, 0x0a, 0x07, 0x08, 0x0b,
	0x13, 0x14, 0x10, 0x11, 0x0e, 0x0f, 0x12};

enum class RegisterWriteStage
{
	BeforeRegister,
	WaveformDelay,
	RegisterDelay,
};

struct RegisterWriteProgress
{
	std::size_t registerIndex = 0;
	int cyclesRemaining = 0;
	RegisterWriteStage stage = RegisterWriteStage::BeforeRegister;
};

inline auto isWaveformRegister(unsigned char address) -> bool
{
	return address == 4 || address == 11 || address == 18;
}

template<typename Sid>
auto clockScheduledCycles(Sid* sid, short*& output, int& samples, int& total,
	int& cycleBudget, int& cyclesRemaining) -> bool
{
	const int requested = std::min(cycleBudget, cyclesRemaining);
	int delta = requested;
	const int produced = sid->clock(delta, output, samples);
	const int consumed = requested - delta;
	total += produced;
	output += produced;
	samples -= produced;
	cycleBudget -= consumed;
	cyclesRemaining -= consumed;
	return consumed > 0 || produced > 0;
}

template<typename Sid>
auto beginRegister(const unsigned char* registers, Sid* sid,
	const std::array<unsigned char, NumSidRegisters>& order, int cycleBudget,
	int samples, RegisterWriteProgress& progress) -> bool
{
	if (cycleBudget <= 0 || samples <= 0) { return false; }
	const unsigned char address = order[progress.registerIndex];
	if (isWaveformRegister(address))
	{
		progress.stage = RegisterWriteStage::WaveformDelay;
		progress.cyclesRemaining = WaveformReadDelay;
		return true;
	}
	sid->write(address, registers[address]);
	progress.stage = RegisterWriteStage::RegisterDelay;
	progress.cyclesRemaining = RegisterWriteDelay;
	return true;
}


template<typename Sid>
auto processWaveformDelay(const unsigned char* registers, Sid* sid,
	const std::array<unsigned char, NumSidRegisters>& order, short*& output,
	int& samples, int& total, int& cycleBudget, RegisterWriteProgress& progress) -> bool
{
	if (progress.cyclesRemaining > 0)
	{
		const bool progressed = clockScheduledCycles(sid, output, samples, total,
			cycleBudget, progress.cyclesRemaining);
		if (progress.cyclesRemaining > 0)
		{
			return progressed && cycleBudget > 0 && samples > 0;
		}
	}
	if (cycleBudget <= 0 || samples <= 0) { return false; }
	const unsigned char address = order[progress.registerIndex];
	sid->write(address, registers[address]);
	progress.stage = RegisterWriteStage::RegisterDelay;
	progress.cyclesRemaining = RegisterWriteDelay;
	return true;
}

template<typename Sid>
auto processRegisterDelay(Sid* sid, short*& output, int& samples, int& total,
	int& cycleBudget, RegisterWriteProgress& progress) -> bool
{
	if (progress.cyclesRemaining > 0)
	{
		const bool progressed = clockScheduledCycles(sid, output, samples, total,
			cycleBudget, progress.cyclesRemaining);
		if (progress.cyclesRemaining > 0)
		{
			return progressed && cycleBudget > 0 && samples > 0;
		}
	}
	++progress.registerIndex;
	progress.stage = RegisterWriteStage::BeforeRegister;
	return true;
}

template<typename Sid>
auto fillBuffer(const unsigned char* registers, Sid* sid,
	const std::array<unsigned char, NumSidRegisters>& order, int cycleBudget,
	short* output, int samples, RegisterWriteProgress& progress) -> int
{
	int total = 0;
	while (progress.registerIndex < order.size())
	{
		bool keepGoing = false;
		switch (progress.stage)
		{
		case RegisterWriteStage::BeforeRegister:
			keepGoing = beginRegister(registers, sid, order, cycleBudget, samples, progress);
			break;
		case RegisterWriteStage::WaveformDelay:
			keepGoing = processWaveformDelay(registers, sid, order, output, samples,
				total, cycleBudget, progress);
			break;
		case RegisterWriteStage::RegisterDelay:
			keepGoing = processRegisterDelay(sid, output, samples, total, cycleBudget, progress);
			break;
		}
		if (!keepGoing) { return total; }
	}

	progress = {};
	if (cycleBudget > 0 && samples > 0) { total += sid->clock(cycleBudget, output, samples); }
	return total;
}

} // namespace lmms::sidplayback

#endif // LMMS_SID_PLAYBACK_SCHEDULER_H
