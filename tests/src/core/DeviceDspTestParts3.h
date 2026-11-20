/*
 * DeviceDspTestParts3.h - impulse-response tests for the fractional delays in Delay.h
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

// Complete fixture method definitions; included once after the fixture declaration.

namespace
{

//! Feed a unit impulse on @a impulseChannel and zeros after it, for @a frames frames, and check
//! @a expected on that channel while the other channel must stay silent.  The same scenario runs
//! with the impulse on each channel in turn, so both channels must produce the same response.
template <typename Delay, typename Configure>
void verifyFractionalImpulse(const QString& label, int frames, const std::vector<double>& expected,
	Configure configure)
{
	for (int impulseChannel = 0; impulseChannel < 2; ++impulseChannel)
	{
		Delay delay(16);
		configure(delay);

		std::array<std::vector<double>, 2> output;
		for (int n = 0; n < frames; ++n)
		{
			for (int c = 0; c < 2; ++c)
			{
				const double in = (n == 0 && c == impulseChannel) ? 1.0 : 0.0;
				output[c].push_back(delay.update(in, static_cast<ch_cnt_t>(c)));
			}
		}

		QStringList failures;
		QStringList response;
		for (int n = 0; n < frames; ++n)
		{
			const double want = n < static_cast<int>(expected.size()) ? expected[n] : 0.0;
			const double got = output[impulseChannel][n];
			response << QString::number(got, 'g', 17);
			if (std::abs(got - want) > 1e-12)
			{
				failures << QStringLiteral("frame %1 got %2 want %3")
					.arg(n).arg(got, 0, 'g', 17).arg(want, 0, 'g', 17);
			}
		}

		const int silent = 1 - impulseChannel;
		for (int n = 0; n < frames; ++n)
		{
			if (std::abs(output[silent][n]) > 1e-12)
			{
				failures << QStringLiteral("silent channel %1 frame %2 got %3")
					.arg(silent).arg(n).arg(output[silent][n], 0, 'g', 17);
			}
		}

		QVERIFY2(failures.isEmpty(),
			qPrintable(QStringLiteral("%1: channel %2: %3; actual response [%4]")
				.arg(label).arg(impulseChannel).arg(failures.join(QStringLiteral("; ")),
					response.join(QStringLiteral(", ")))));
	}
}

} // namespace

//! A 1.5-frame delay interpolates the impulse across frames 1 and 2, not 2 and 3.
void DeviceDspTest::combFeedbackFractionalHalfDelay()
{
	verifyFractionalImpulse<StereoCombFeedback>(QStringLiteral("CombFeedback 1.5"), 8,
		{0.0, 0.5, 0.5}, [](StereoCombFeedback& delay) { delay.setGain(0.0); delay.setDelay(1.5); });
}

//! A 1.25-frame delay keeps three quarters on frame 1 and one quarter on frame 2.
void DeviceDspTest::combFeedbackFractionalQuarterDelay()
{
	verifyFractionalImpulse<StereoCombFeedback>(QStringLiteral("CombFeedback 1.25"), 8,
		{0.0, 0.75, 0.25}, [](StereoCombFeedback& delay) { delay.setGain(0.0); delay.setDelay(1.25); });
}

//! A 2.75-frame delay puts one quarter on frame 2 and three quarters on frame 3.
void DeviceDspTest::combFeedbackFractionalThreeQuarterDelay()
{
	verifyFractionalImpulse<StereoCombFeedback>(QStringLiteral("CombFeedback 2.75"), 8,
		{0.0, 0.0, 0.25, 0.75}, [](StereoCombFeedback& delay) { delay.setGain(0.0); delay.setDelay(2.75); });
}

//! Integer delays already behave; they must stay a single sample at frame D.
void DeviceDspTest::combFeedbackIntegerDelayUnchanged()
{
	verifyFractionalImpulse<StereoCombFeedback>(QStringLiteral("CombFeedback 1.0"), 8,
		{0.0, 1.0}, [](StereoCombFeedback& delay) { delay.setGain(0.0); delay.setDelay(1.0); });
	verifyFractionalImpulse<StereoCombFeedback>(QStringLiteral("CombFeedback 2.0"), 8,
		{0.0, 0.0, 1.0}, [](StereoCombFeedback& delay) { delay.setGain(0.0); delay.setDelay(2.0); });
}

//! The feed-forward comb has the same fractional interpolation as the feedback comb.
void DeviceDspTest::combFeedfwdFractionalHalfDelay()
{
	verifyFractionalImpulse<StereoCombFeedfwd>(QStringLiteral("CombFeedfwd 1.5"), 8,
		{0.0, 0.5, 0.5}, [](StereoCombFeedfwd& delay) { delay.setGain(0.0); delay.setDelay(1.5); });
}

//! The allpass delay's delayed branch has the same fractional interpolation.
void DeviceDspTest::allpassFractionalHalfDelay()
{
	verifyFractionalImpulse<StereoAllpassDelay>(QStringLiteral("AllpassDelay 1.5"), 8,
		{0.0, 0.5, 0.5}, [](StereoAllpassDelay& delay) { delay.setGain(0.0); delay.setDelay(1.5); });
}

//! With gain 0 the dual tap is just the sum of two interpolated taps.  For delay d the tap puts
//! (1 - (d - floor(d))) on frame floor(d) and (d - floor(d)) on frame floor(d) + 1.
//! So 1.5 gives 0.5 on frame 1 and 0.5 on frame 2; 3.25 gives 0.75 on frame 3 and 0.25 on frame 4.
//! Summed: out[1] = 0.5, out[2] = 0.5, out[3] = 0.75, out[4] = 0.25, all other frames 0.
void DeviceDspTest::combFeedbackDualtapFractionalDelays()
{
	verifyFractionalImpulse<StereoCombFeedbackDualtap>(QStringLiteral("CombFeedbackDualtap 1.5/3.25"), 8,
		{0.0, 0.5, 0.5, 0.75, 0.25},
		[](StereoCombFeedbackDualtap& delay) { delay.setGain(0.0); delay.setDelays(1.5, 3.25); });
}

//! Below one frame the delay clamps to one frame, matching an integer 1.0 delay exactly and not
//! reading a stale, not-yet-written slot.
void DeviceDspTest::combFeedbackSubSampleClampedToOne()
{
	for (int impulseChannel = 0; impulseChannel < 2; ++impulseChannel)
	{
		StereoCombFeedback clamped(16);
		StereoCombFeedback integer(16);
		clamped.setGain(0.0);
		integer.setGain(0.0);
		clamped.setDelay(2.0);
		integer.setDelay(2.0);

		// Seed both histories identically so a sub-sample read that lands in an unwritten slot
		// exposes a stale sample instead of the clamped one-frame output.
		for (int n = 0; n < 8; ++n)
		{
			const double in = (n == 0) ? 1.0 : (n == 1 ? -0.5 : (n == 2 ? 0.25 : 0.0));
			for (int c = 0; c < 2; ++c)
			{
				clamped.update(in, static_cast<ch_cnt_t>(c));
				integer.update(in, static_cast<ch_cnt_t>(c));
			}
		}

		clamped.setDelay(0.5);
		integer.setDelay(1.0);

		for (int n = 0; n < 8; ++n)
		{
			for (int c = 0; c < 2; ++c)
			{
				const double in = (n == 0 && c == impulseChannel) ? 1.0 : 0.0;
				const double gotClamped = clamped.update(in, static_cast<ch_cnt_t>(c));
				const double gotInteger = integer.update(in, static_cast<ch_cnt_t>(c));
				const double want = (n == 1 && c == impulseChannel) ? 1.0 : 0.0;
				QVERIFY2(std::abs(gotClamped - gotInteger) <= 1e-12,
					qPrintable(QStringLiteral("delay 0.5 must equal delay 1.0: channel %1 frame %2: got %3, integer got %4")
						.arg(c).arg(n).arg(gotClamped, 0, 'g', 17).arg(gotInteger, 0, 'g', 17)));
				QVERIFY2(std::abs(gotClamped - want) <= 1e-12,
					qPrintable(QStringLiteral("delay 0.5 clamps to one frame: channel %1 frame %2: got %3, want %4")
						.arg(c).arg(n).arg(gotClamped, 0, 'g', 17).arg(want, 0, 'g', 17)));
			}
		}
	}
}

//! Reconfiguring after the line has run keeps the fractional response: advance the cursor, then
//! set 1.5 and grow the maximum delay before the impulse.
void DeviceDspTest::combFeedbackReconfiguredFractionalDelay()
{
	verifyFractionalImpulse<StereoCombFeedback>(QStringLiteral("CombFeedback reconfigured 1.5"), 8,
		{0.0, 0.5, 0.5}, [](StereoCombFeedback& delay) {
			delay.setGain(0.0);
			delay.setDelay(2.0);
			for (int n = 0; n < 8; ++n)
			{
				for (int c = 0; c < 2; ++c) { delay.update(n == 0 ? 1.0 : 0.0, static_cast<ch_cnt_t>(c)); }
			}
			delay.setDelay(1.5);
			delay.setMaxDelay(32);
		});
}

//! Gain 0.5 at 1.5 frames: with s[n] the sample written at call n and buf[k] the ring slot
//! holding s[k-1], update() lerps buf[readPos] = s[n-2] and buf[readPos + 1] = s[n-1] with
//! equal weights, so y[n] = 0.5*s[n-2] + 0.5*s[n-1].
//! y[1] = 0.5*1 = 0.5; s[1] = 0.5*y[1] = 0.25; y[2] = 0.5*(s[0] + s[1]) = 0.625;
//! s[2] = 0.5*y[2] = 0.3125; y[3] = 0.5*(s[1] + s[2]) = 0.28125.  The loop keeps decaying:
//! y[4] = 0.2265625, y[5] = 0.126953125, y[6] = 0.08837890625, y[7] = 0.0538330078125.
void DeviceDspTest::combFeedbackFractionalFeedbackPath()
{
	verifyFractionalImpulse<StereoCombFeedback>(QStringLiteral("CombFeedback gain 0.5 at 1.5"), 8,
		{0.0, 0.5, 0.625, 0.28125, 0.2265625, 0.126953125, 0.08837890625, 0.0538330078125},
		[](StereoCombFeedback& delay) { delay.setGain(0.5); delay.setDelay(1.5); });
}
