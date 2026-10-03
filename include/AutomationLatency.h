/*
 * AutomationLatency.h - automation read where the latent audio is (0.5.0 MX-160)
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
#ifndef LMMS_AUTOMATION_LATENCY_H
#define LMMS_AUTOMATION_LATENCY_H

#include <atomic>

#include "ProjectSection.h"
#include "TimePos.h"
#include "lmms_export.h"

class QTimer;

namespace lmms
{

class AutomatableModel;
class AutomationClip;
class Clip;

/*! 0.5.0 W3 (MX-160): latency-compensated automation. The audio that reaches a device inside a latent
 *  chain is late by the latency upstream of it - the devices before it, plus the PDC alignment point of
 *  its mixer channel - so a curve read at the timeline position moves the parameter early. With this on,
 *  every automation clip is read at curve(t - upstream latency of the parameter it drives).
 *
 *  Opt-in per project: saved as <automation-latency enabled="1"/> only while on, so every older project
 *  loads off and renders bit for bit (with it off every clip's lag is 0 and both read paths do the
 *  arithmetic they always did). The control thread computes each clip's upstream latency (refresh(),
 *  kept current by a poll while on) into the clip's atomic; the audio thread only reads that int. */
class LMMS_EXPORT AutomationLatency : public ProjectSection
{
public:
	static AutomationLatency& instance();

	bool enabled() const { return m_enabled.load(std::memory_order_acquire); }
	//! Control thread: switch it on (every clip's latency is computed now, then kept current) or off.
	void setEnabled(bool on);
	//! Control thread: recompute every automation clip's upstream latency (0 for every clip while off).
	void refresh();
	//! Control thread: frames of latency the audio has been through when it reaches @a model's place - the
	//! running devices before its device in the chain, plus where that chain starts after PDC (a mixer
	//! channel's alignment point; 0 for a track's own chain, which PDC delays after it). 0 for a parameter
	//! that sits before any latent device (an instrument's, a track's fader).
	static int upstreamFrames(const AutomatableModel* model);
	//! Control thread: how many automation clips read with a lag now, and the largest latency among them.
	static int compensatedClips(int* largestFrames);

	//! Audio thread: where the per-tick read of @a clip happens at song tick @a time - @a time itself unless
	//! compensation is on and @a clip is an automation clip with latency upstream, then that many ticks earlier.
	TimePos readTime(const Clip* clip, TimePos time) const;
	//! Audio thread: move a block start (@a start ticks + @a frameOffsetInTick frames) back by @a clip's
	//! latency (a null clip, or compensation off: unchanged). A start moved before the song's start becomes
	//! tick -1, which every clip starts after, so the caller's own "not started yet" test skips it.
	void shiftBlock(const AutomationClip* clip, double framesPerTick, TimePos* start, int* frameOffsetInTick) const;

	//! The arithmetic (pure, unit-tested): @a latencyFrames as whole ticks at @a framesPerTick, rounded.
	static int ticksFor(int latencyFrames, double framesPerTick);
	//! The arithmetic (pure, unit-tested): a block start moved back by @a latencyFrames; false before 0.
	static bool shiftFrames(int latencyFrames, double framesPerTick, TimePos* start, int* frameOffsetInTick);

	QString elementName() const override { return QStringLiteral("automation-latency"); }
	bool shouldPersist() const override { return enabled(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

private:
	AutomationLatency() = default;
	//! Control thread: start or stop the poll that keeps the clips' latencies current.
	void poll(bool on);
	//! Control thread: refresh() as though compensation were @a on (setEnabled publishes the switch after it).
	void refreshAs(bool on);
	//! Control thread: walk every clip now, as though compensation were @a on (refreshAs() without its mid-load guard).
	void refreshClips(bool on);
	//! Control thread: walk them once the project being loaded has resolved its clips (Song::projectLoaded).
	void refreshOnceLoaded();

	std::atomic<bool> m_enabled{false};
	QTimer* m_poll = nullptr;
};

} // namespace lmms

#endif // LMMS_AUTOMATION_LATENCY_H
