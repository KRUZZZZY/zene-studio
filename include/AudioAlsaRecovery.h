/*
 * AudioAlsaRecovery.h - bounded, stop-aware ALSA suspend recovery
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#ifndef LMMS_AUDIO_ALSA_RECOVERY_H
#define LMMS_AUDIO_ALSA_RECOVERY_H

#include <cerrno>

namespace lmms::alsa::detail
{

//! The outcome of resumePcmWithBudget().
struct ResumeOutcome
{
	//! The last snd_pcm_resume() code; -EAGAIN when the whole budget was spent
	//! retrying a device that never resumed (the caller falls back to prepare).
	int error = 0;
	//! True when the retry was abandoned because the caller is stopping. The
	//! device is being closed, so nothing is recovered and nothing is reported.
	bool stopped = false;
};

/*! Retries snd_pcm_resume() for a device that answers -EAGAIN (a suspend it has not
 *  finished), at most @a attempts times, and stops at once when @a stopRequested() is
 *  true. @a wait runs between attempts; the caller supplies it, and the caller owns
 *  the budget, so the retry policy is tested without a suspended device and without
 *  a sleep. A -EAGAIN result means the budget is spent: the caller falls back to
 *  snd_pcm_prepare(), the ALSA recovery for a device that will not resume. The wait
 *  is skipped after the last attempt, so the budget is not followed by one more sleep. */
template <typename Resume, typename Wait, typename StopRequested>
ResumeOutcome resumePcmWithBudget(Resume&& resume, Wait&& wait, StopRequested&& stopRequested,
	int attempts)
{
	ResumeOutcome outcome;
	for (int attempt = 0; attempt < attempts; ++attempt)
	{
		if (stopRequested())
		{
			outcome.stopped = true;
			return outcome;
		}
		outcome.error = resume();
		if (outcome.error != -EAGAIN)
		{
			return outcome;
		}
		if (attempt + 1 < attempts)
		{
			wait();
		}
	}
	return outcome;
}

} // namespace lmms::alsa::detail

#endif // LMMS_AUDIO_ALSA_RECOVERY_H
