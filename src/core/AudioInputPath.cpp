/*
 * AudioInputPath.cpp - the capture-IN path's configuration and the live state a
 *                      capture backend publishes (0.3.0, feature row 64).
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
 */

#include "AudioInputPath.h"

#include <algorithm>
#include <mutex>

#include "ConfigManager.h"
#include "MultiTrackRecorder.h"

namespace lmms
{

namespace
{

//! The config-file class every key of this path lives under.
constexpr auto kConfigClass = "audioinput";
constexpr auto kDeviceKey = "device";
constexpr auto kChannelsKey = "channels";
constexpr auto kLeftKey = "left";
constexpr auto kRightKey = "right";

int clampChannels(int channels)
{
	return std::clamp(channels, AudioInputPath::MinChannels, AudioInputPath::MaxChannels);
}

} // namespace


//! The live half of the input path. The strings and flags are the UI thread's;
//! the two counters are the capture thread's, which is why they are atomics and
//! why they are kept OUT of the struct the mutex protects: the capture thread
//! must be able to count a frame without taking a lock (the realtime rule), and
//! a 64-bit counter read while it is being written would tear.
struct AudioInputPath::State
{
	std::mutex mutex;
	Live live;                                   //!< the non-atomic part
	std::atomic<std::uint64_t> framesCaptured{0};  //!< capture thread
	std::atomic<std::uint64_t> overruns{0};        //!< capture thread
};


AudioInputPath::State& AudioInputPath::state()
{
	// Constructed on first use. It holds no Qt object and reads no config, so
	// the construction order of anything else cannot make this a null
	// dereference (the trap RetroMidiCapture's header documents for
	// ConfigManager at static-initialisation time).
	static State instance;
	return instance;
}


AudioInputPath::Plan AudioInputPath::configuredPlan()
{
	ConfigManager* config = ConfigManager::inst();
	Plan plan;
	plan.device = config->value(kConfigClass, kDeviceKey);
	plan.channels = clampChannels(config->value(kConfigClass, kChannelsKey,
		QString::number(DefaultChannels)).toInt());
	plan.left = config->value(kConfigClass, kLeftKey, QStringLiteral("0")).toInt();
	plan.right = config->value(kConfigClass, kRightKey, QStringLiteral("1")).toInt();
	// A pair that does not fit the width is clamped HERE rather than refused:
	// this accessor never fails, and the honoured form is what it returns.
	plan.left = std::clamp(plan.left, 0, plan.channels - 1);
	plan.right = std::clamp(plan.right, 0, plan.channels - 1);
	return plan;
}


bool AudioInputPath::validatePlan(const Plan& plan, Plan* normalised, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error != nullptr) { *error = message; }
		return false;
	};

	if (plan.channels < MinChannels || plan.channels > MaxChannels)
	{
		return fail(QStringLiteral("input channel count %1 is outside the supported range "
			"[%2, %3]").arg(plan.channels).arg(MinChannels).arg(MaxChannels));
	}
	if (plan.left < 0 || plan.left >= plan.channels || plan.right < 0 || plan.right >= plan.channels)
	{
		return fail(QStringLiteral("the bus pair (%1, %2) must name two captured channels in "
			"[0, %3)").arg(plan.left).arg(plan.right).arg(plan.channels));
	}

	if (normalised != nullptr)
	{
		*normalised = plan;
		normalised->channels = clampChannels(plan.channels);
	}
	if (error != nullptr) { error->clear(); }
	return true;
}


bool AudioInputPath::writePlan(const Plan& plan, QString* error)
{
	Plan normalised;
	if (!validatePlan(plan, &normalised, error)) { return false; }

	ConfigManager* config = ConfigManager::inst();
	config->setValue(kConfigClass, kDeviceKey, normalised.device);
	config->setValue(kConfigClass, kChannelsKey, QString::number(normalised.channels));
	config->setValue(kConfigClass, kLeftKey, QString::number(normalised.left));
	config->setValue(kConfigClass, kRightKey, QString::number(normalised.right));
	config->saveConfigFile();
	if (error != nullptr) { error->clear(); }
	return true;
}


int AudioInputPath::configuredChannelCount()
{
	return configuredPlan().channels;
}


int AudioInputPath::recordRouteCapacity()
{
	// The route bound, deliberately NOT the input count: a route is a file plus
	// an input channel, several routes may record the same channel, and the
	// number of files a session wants is a property of the session rather than
	// of the interface. MultiTrackRecorder::MaxRoutes is where the bound and its
	// memory cost are argued.
	return static_cast<int>(MultiTrackRecorder::MaxRoutes);
}


AudioInputPath::Live AudioInputPath::live()
{
	State& shared = state();
	Live copy;
	{
		const std::lock_guard<std::mutex> guard(shared.mutex);
		copy = shared.live;
	}
	// The counters are the capture thread's, outside the lock: a value written
	// while the strings were being copied is neither lost nor torn.
	copy.framesCaptured = shared.framesCaptured.load(std::memory_order_relaxed);
	copy.overruns = shared.overruns.load(std::memory_order_relaxed);
	return copy;
}


void AudioInputPath::setCaptureCapable(bool capable)
{
	State& shared = state();
	const std::lock_guard<std::mutex> guard(shared.mutex);
	shared.live.captureCapable = capable;
}


void AudioInputPath::publishOpen(const Plan& plan, int grantedChannels, int rate,
	const QString& sampleFormat)
{
	State& shared = state();
	const std::lock_guard<std::mutex> guard(shared.mutex);
	shared.live.captureCapable = true;
	shared.live.open = true;
	shared.live.device = plan.device;
	shared.live.reason.clear();
	shared.live.channels = clampChannels(grantedChannels);
	shared.live.rate = rate;
	shared.live.left = std::clamp(plan.left, 0, shared.live.channels - 1);
	shared.live.right = std::clamp(plan.right, 0, shared.live.channels - 1);
	shared.live.sampleFormat = sampleFormat;
}


void AudioInputPath::publishUnavailable(const QString& device, const QString& reason)
{
	State& shared = state();
	const std::lock_guard<std::mutex> guard(shared.mutex);
	shared.live.captureCapable = true;
	shared.live.open = false;
	shared.live.device = device;
	shared.live.reason = reason;
	shared.live.channels = 0;
	shared.live.rate = 0;
}


void AudioInputPath::publishClosed(const QString& reason)
{
	State& shared = state();
	const std::lock_guard<std::mutex> guard(shared.mutex);
	shared.live.open = false;
	shared.live.reason = reason;
	shared.live.channels = 0;
}


void AudioInputPath::addCapturedFrames(std::uint64_t frames) noexcept
{
	// Capture thread. One relaxed add, on the counter the mutex deliberately
	// does not protect: it never blocks and never allocates.
	state().framesCaptured.fetch_add(frames, std::memory_order_relaxed);
}


void AudioInputPath::addOverrun() noexcept
{
	state().overruns.fetch_add(1, std::memory_order_relaxed);
}


int AudioInputPath::recordableChannelCount()
{
	const Live current = live();
	if (current.open && current.channels >= MinChannels)
	{
		return current.channels;
	}
	// No capture open: the stereo engine bus is what a route can read, and
	// saying 0 here would refuse every route on a box with no capture device,
	// which is not what "no capture" means - the bus still carries silence.
	return DefaultChannels;
}


} // namespace lmms
