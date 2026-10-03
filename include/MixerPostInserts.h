/*
 * MixerPostInserts.h - a mixer channel's post-fader inserts (0.5.0 MX-082)
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
#ifndef LMMS_MIXER_POST_INSERTS_H
#define LMMS_MIXER_POST_INSERTS_H

#include <vector>

#include <QString>

#include "EffectChain.h"
#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

class MixerChannel;
class MixerRoute;
class ValueBuffer;

/*! 0.5.0 W3 (MX-082): a mixer channel's second chain, run AFTER its fader. The inherited mixer never
 *  multiplies a channel's fader into its buffer: each receiver (and the master output) applies it as it
 *  mixes. So while this chain holds devices the channel "bakes" its fader for the period - its buffer
 *  becomes the post-fader snapshot run through these devices - keeps the pre-fader signal for its
 *  pre-fader sends, and the receivers read the fader as 1 (postinserts::faderValue). Empty, it is
 *  skipped wholesale and every read below returns exactly what the mixer read before, so an older project
 *  renders bit for bit. Saved as <postinserts> under the channel only while it holds devices.
 *  Its latency is added to the channel's output in the PDC graph. The sidechain and record taps keep
 *  their places (before these devices); a feedback send keeps the pre-fader snapshot it always carried. */
class LMMS_EXPORT PostFaderInserts
{
public:
	PostFaderInserts();

	EffectChain& chain() { return m_chain; }
	const EffectChain& chain() const { return m_chain; }
	//! Whether this period's output carries the fader and these devices (the channel's worker sets it).
	bool baked() const { return m_baked; }
	//! The channel's pre-fader signal of a baked period, for its pre-fader sends.
	const SampleFrame* preFader() const { return m_preFader.data(); }

	/*! Audio thread (the channel's worker), after the fader snapshot and the post-fader taps: with
	 *  devices, bake the fader and run them over @a channel's buffer. Returns the gain the channel's meters
	 *  apply to its buffer from here on: 1 when baked, else @a volume. Allocates nothing, locks nothing. */
	float process(MixerChannel& channel, float volume, f_cnt_t frames);

	//! Write <postinserts> under @a channelElement, only while the chain holds devices.
	void save(QDomDocument& doc, QDomElement& channelElement);
	//! Read it back; a channel element without one empties the chain (the reset-on-absence rule).
	void load(const QDomElement& channelElement);
	//! The channel was cleared: no devices.
	void clear();

private:
	EffectChain m_chain{nullptr};
	std::vector<SampleFrame> m_preFader;
	bool m_baked = false;
};

//! The receivers' reads of a sender's fader and buffer, which baked post-fader inserts change.
namespace postinserts
{

//! "ch-<n>/post": the device-command target (plugin.*, chain.*) naming channel n's post-fader inserts.
inline QString targetOf(const QString& channelId) { return channelId + QStringLiteral("/post"); }
inline bool isTarget(const QString& id) { return id.endsWith(QStringLiteral("/post")); }
inline QString channelOf(const QString& target) { return target.left(target.size() - 5); }

//! The fader gain to apply for @a sender through @a route (nullptr: the master's own output): 1 when the
//! sender's inserts baked it this period (except a feedback send, whose snapshot is pre-fader), else the
//! fader's value - the read the mixer always made.
LMMS_EXPORT float faderValue(const MixerChannel& sender, const MixerRoute* route);
//! The same for the fader's sample-exact buffer: nullptr when baked.
LMMS_EXPORT ValueBuffer* faderBuffer(MixerChannel& sender, const MixerRoute* route);
//! What @a route takes from @a sender: its buffer, or the kept pre-fader signal for a pre-fader send of a
//! baked period.
LMMS_EXPORT const SampleFrame* sendSource(const MixerChannel& sender, const MixerRoute& route);

} // namespace postinserts

} // namespace lmms

#endif // LMMS_MIXER_POST_INSERTS_H
