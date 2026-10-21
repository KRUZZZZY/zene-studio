/*
 * SamplerVoicePool.h - the sampler's fixed set of playback voices and its voice-reuse policy
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_SAMPLER_VOICE_POOL_H
#define LMMS_SAMPLER_VOICE_POOL_H

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "AudioResampler.h"
#include "Sample.h"

namespace lmms
{

/*! The playback voices of one AudioFileProcessor, all built up front so a note start allocates nothing.
 *
 *  Voice-reuse policy (Q050-103). The user sets a voice limit (1..MaxVoices). While fewer notes than the
 *  limit are sounding a new note takes a free voice. At the limit the pool reuses a voice instead:
 *    1. the voice whose note was released the longest ago (it is only playing its release tail);
 *    2. with no released voice, the voice whose note started the longest ago.
 *  Ties cannot occur: every voice carries a strictly increasing order number. The chosen voice is not
 *  cut off - it is marked "stolen" and its own note fades it to silence over StealFadeMs, so reuse does
 *  not click - and the new note takes a spare voice (FadeHeadroom of them) and starts from a freshly
 *  reset playback state and its own envelope position. Held notes that were not chosen are untouched.
 *
 *  Threading. Notes of one instrument render on several engine threads at once, and one note's voice is
 *  only ever written by that note. Everything the pool shares between notes is a std::atomic; nothing
 *  blocks, allocates or loops unboundedly (the scans are Capacity long, the claim retry is four).
 *  A note proves it still owns its voice with the token it was given; a voice whose note ended or was
 *  reused has a different token. The stolen mark carries the victim's token, so a mark left on a voice
 *  that was since handed to another note never matches the new owner.
 */
class SamplerVoicePool
{
public:
	static constexpr int MaxVoices = 64;      //!< the largest voice limit a user can set
	static constexpr int FadeHeadroom = 16;   //!< spare voices that stolen notes fade out on
	static constexpr int Capacity = MaxVoices + FadeHeadroom;
	static constexpr float StealFadeMs = 3.f;

	struct Claim
	{
		int index = -1;
		std::uint64_t token = 0;
		bool valid() const noexcept { return index >= 0; }
	};

	struct Voice
	{
		explicit Voice(AudioResampler::Mode mode) : state(mode) {}

		Sample::PlaybackState state;           //!< written only by the voice's current note
		f_cnt_t stealFramesDone = 0;           //!< same: how far the stolen note's fade has run
		std::atomic<std::uint64_t> token{0};   //!< 0 = free
		std::atomic<std::uint64_t> startOrder{0};
		std::atomic<std::uint64_t> releaseOrder{0}; //!< 0 = the note is still held
		std::atomic<std::uint64_t> stolenToken{0};  //!< the token this voice was chosen for reuse under
	};

	explicit SamplerVoicePool(AudioResampler::Mode mode)
	{
		m_voices.reserve(Capacity);
		for (int i = 0; i < Capacity; ++i) { m_voices.push_back(std::make_unique<Voice>(mode)); }
	}

	Voice& voice(int index) noexcept { return *m_voices[static_cast<std::size_t>(index)]; }

	/*! Start a note: a free voice, or - at \a limit - the reuse policy's choice faded out and a spare taken.
	 *  Returns an invalid claim only when every voice is taken (limit + FadeHeadroom stolen notes at once). */
	Claim acquire(int limit) noexcept
	{
		const int cap = std::clamp(limit, 1, static_cast<int>(MaxVoices));
		for (int attempt = 0; attempt < 4; ++attempt)
		{
			const Census now = census();
			if (now.active < cap || markVictim(now)) { break; }
			// Another note chose the same voice first: look again.
		}
		return claimFreeVoice();
	}

	//! True while \a claim still names the note's own voice.
	bool owns(const Claim& claim) const noexcept
	{
		return claim.valid() && m_voices[static_cast<std::size_t>(claim.index)]->token.load(std::memory_order_acquire) == claim.token;
	}

	//! True once the policy chose this note's voice for reuse: the note must fade out and free it.
	bool isStolen(const Claim& claim) const noexcept
	{
		return claim.valid()
			&& m_voices[static_cast<std::size_t>(claim.index)]->stolenToken.load(std::memory_order_acquire) == claim.token;
	}

	//! The note has been released: from now on its voice is the first to be reused.
	void markReleased(const Claim& claim) noexcept
	{
		if (!owns(claim)) { return; }
		Voice& v = *m_voices[static_cast<std::size_t>(claim.index)];
		if (v.releaseOrder.load(std::memory_order_acquire) == 0)
		{
			v.releaseOrder.store(m_order.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
		}
	}

	//! The note is done with its voice (it ended, or its fade-out finished).
	void free(const Claim& claim) noexcept
	{
		if (!claim.valid()) { return; }
		std::uint64_t expected = claim.token;
		m_voices[static_cast<std::size_t>(claim.index)]->token.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
	}

	//! The voices a note is sounding on right now (stolen ones still fading included). For tests and the GUI.
	int activeVoices() const noexcept
	{
		int count = 0;
		for (const auto& v : m_voices) { count += v->token.load(std::memory_order_acquire) != 0 ? 1 : 0; }
		return count;
	}

private:
	//! The best voice to reuse of one kind: the smallest order number wins.
	struct Candidate
	{
		std::uint64_t order = ~std::uint64_t{0};
		int index = -1;
		std::uint64_t token = 0;

		void offer(std::uint64_t candidateOrder, int candidateIndex, std::uint64_t candidateToken) noexcept
		{
			if (candidateOrder >= order) { return; }
			order = candidateOrder;
			index = candidateIndex;
			token = candidateToken;
		}
	};

	struct Census
	{
		int active = 0;     //!< voices sounding and not already chosen for reuse
		Candidate released; //!< the voice whose note was released the longest ago
		Candidate held;     //!< the voice whose note started the longest ago
	};

	Census census() const noexcept
	{
		Census result;
		for (int i = 0; i < Capacity; ++i)
		{
			const Voice& v = *m_voices[static_cast<std::size_t>(i)];
			const std::uint64_t token = v.token.load(std::memory_order_acquire);
			if (token == 0 || v.stolenToken.load(std::memory_order_acquire) == token) { continue; }
			++result.active;
			const std::uint64_t releasedAt = v.releaseOrder.load(std::memory_order_acquire);
			if (releasedAt != 0) { result.released.offer(releasedAt, i, token); }
			else { result.held.offer(v.startOrder.load(std::memory_order_acquire), i, token); }
		}
		return result;
	}

	//! Marks the policy's victim for reuse. False when another note marked the same voice first.
	bool markVictim(const Census& now) noexcept
	{
		const Candidate& victim = now.released.index >= 0 ? now.released : now.held;
		if (victim.index < 0) { return true; }
		std::uint64_t expected = 0;
		return m_voices[static_cast<std::size_t>(victim.index)]->stolenToken
			.compare_exchange_strong(expected, victim.token, std::memory_order_acq_rel);
	}

	Claim claimFreeVoice() noexcept
	{
		for (int i = 0; i < Capacity; ++i)
		{
			Voice& v = *m_voices[static_cast<std::size_t>(i)];
			std::uint64_t expected = 0;
			const std::uint64_t token = m_nextToken.fetch_add(1, std::memory_order_relaxed);
			if (!v.token.compare_exchange_strong(expected, token, std::memory_order_acq_rel)) { continue; }

			// This note owns the voice from here on: nothing else writes its state.
			v.state.reset();
			v.stealFramesDone = 0;
			v.stolenToken.store(0, std::memory_order_relaxed);
			v.releaseOrder.store(0, std::memory_order_relaxed);
			v.startOrder.store(m_order.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
			return Claim{i, token};
		}
		return {};
	}

	std::vector<std::unique_ptr<Voice>> m_voices;
	std::atomic<std::uint64_t> m_order{0};
	std::atomic<std::uint64_t> m_nextToken{1};
};

} // namespace lmms

#endif // LMMS_SAMPLER_VOICE_POOL_H
